#include "orm-cxx/database/RelationStatements.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "SqlAliases.hpp"

namespace
{
auto join(const std::vector<std::string>& values, std::string_view separator) -> std::string
{
    std::string result;

    for (const auto& value : values)
    {
        if (not result.empty())
        {
            result += separator;
        }

        result += value;
    }

    return result;
}

auto findRelation(const orm::model::ModelInfo& modelInfo,
                  const std::string& fieldName) -> const orm::model::RelationInfo&
{
    const auto relation =
        std::ranges::find_if(modelInfo.relationsInfo, [&fieldName](const auto& candidate)
                             { return candidate.fieldName == fieldName or candidate.columnName == fieldName; });

    if (relation == modelInfo.relationsInfo.end())
    {
        throw std::invalid_argument{"Unknown relation field: " + fieldName};
    }

    return *relation;
}

auto requireKeySize(const orm::db::binding::PrimaryKey& key, const std::vector<const orm::model::ColumnInfo*>& columns,
                    std::string_view endpoint) -> void
{
    if (key.size() != columns.size())
    {
        throw std::invalid_argument{std::format("Incomplete {} relation primary key", endpoint)};
    }
}

auto addValueParameter(const orm::db::SqlDialect& dialect, orm::db::Statement& statement, std::string name,
                       const orm::query::QueryValue& value) -> std::string
{
    statement.parameters.push_back(
        orm::db::StatementParameter{.name = name, .value = orm::db::binding::toQueryValue(value)});

    return dialect.bindMarker(name);
}

auto renderOwnerKeyFilter(const orm::db::SqlDialect& dialect, orm::db::Statement& statement,
                          const std::vector<std::string>& expressions,
                          const std::vector<orm::db::binding::PrimaryKey>& keys) -> std::string
{
    if (keys.empty())
    {
        return "0 = 1";
    }

    std::vector<std::string> keyPredicates;
    keyPredicates.reserve(keys.size());

    for (std::size_t keyIndex = 0; keyIndex < keys.size(); ++keyIndex)
    {
        if (keys[keyIndex].size() != expressions.size())
        {
            throw std::invalid_argument{"Incomplete owner relation primary key"};
        }

        std::vector<std::string> columnPredicates;
        columnPredicates.reserve(expressions.size());

        for (std::size_t columnIndex = 0; columnIndex < expressions.size(); ++columnIndex)
        {
            const auto parameterName = std::format("orm_rel_key_{}_{}", keyIndex, columnIndex);
            const auto parameter = addValueParameter(dialect, statement, parameterName, keys[keyIndex][columnIndex]);
            columnPredicates.push_back(std::format("{} = {}", expressions[columnIndex], parameter));
        }

        keyPredicates.push_back("(" + join(columnPredicates, " AND ") + ")");
    }

    return "(" + join(keyPredicates, " OR ") + ")";
}

auto quoteIdentifiers(const orm::db::SqlDialect& dialect,
                      const std::vector<std::string>& identifiers) -> std::vector<std::string>
{
    std::vector<std::string> quoted;
    quoted.reserve(identifiers.size());

    for (const auto& identifier : identifiers)
    {
        quoted.push_back(dialect.quoteIdentifier(identifier));
    }

    return quoted;
}

auto stripTerminator(std::string sql) -> std::string
{
    while (not sql.empty() and (sql.back() == ';' or sql.back() == ' ' or sql.back() == '\n' or sql.back() == '\r'))
    {
        sql.pop_back();
    }

    return sql;
}
} // namespace

namespace orm::db::relations
{
auto createTableStatements(const SqlDialect& dialect, const model::ModelInfo& ownerInfo) -> std::vector<std::string>
{
    const auto ownerPrimaryKey = binding::getPrimaryKeyColumns(ownerInfo);
    std::vector<std::string> statements;

    for (const auto& relation : ownerInfo.relationsInfo)
    {
        if (relation.kind != model::RelationKind::ManyToMany or not relation.junction.has_value() or
            not relation.junction->owningSide)
        {
            continue;
        }

        const auto& junction = relation.junction.value();
        const auto& targetInfo = relation.targetModel();
        const auto targetPrimaryKey = binding::getPrimaryKeyColumns(targetInfo);

        if (junction.ownerColumns.size() != ownerPrimaryKey.size() or
            junction.targetColumns.size() != targetPrimaryKey.size())
        {
            throw std::invalid_argument{"Junction column count does not match endpoint primary keys: " +
                                        junction.tableName};
        }

        std::vector<std::string> definitions;

        for (std::size_t i = 0; i < ownerPrimaryKey.size(); ++i)
        {
            definitions.push_back(std::format("\t{} {} NOT NULL", dialect.quoteIdentifier(junction.ownerColumns[i]),
                                              dialect.toSqlType(ownerPrimaryKey[i]->type)));
        }

        for (std::size_t i = 0; i < targetPrimaryKey.size(); ++i)
        {
            definitions.push_back(std::format("\t{} {} NOT NULL", dialect.quoteIdentifier(junction.targetColumns[i]),
                                              dialect.toSqlType(targetPrimaryKey[i]->type)));
        }

        auto allJunctionColumns = junction.ownerColumns;
        allJunctionColumns.insert(allJunctionColumns.end(), junction.targetColumns.begin(),
                                  junction.targetColumns.end());
        definitions.push_back(
            std::format("\tPRIMARY KEY ({})", join(quoteIdentifiers(dialect, allJunctionColumns), ", ")));

        std::vector<std::string> ownerColumnNames;
        std::vector<std::string> targetColumnNames;
        ownerColumnNames.reserve(ownerPrimaryKey.size());
        targetColumnNames.reserve(targetPrimaryKey.size());

        for (const auto* column : ownerPrimaryKey)
        {
            ownerColumnNames.push_back(dialect.quoteIdentifier(column->name));
        }

        for (const auto* column : targetPrimaryKey)
        {
            targetColumnNames.push_back(dialect.quoteIdentifier(column->name));
        }

        definitions.push_back(std::format("\tFOREIGN KEY ({}) REFERENCES {} ({}) ON DELETE CASCADE",
                                          join(quoteIdentifiers(dialect, junction.ownerColumns), ", "),
                                          dialect.quoteIdentifier(ownerInfo.tableName), join(ownerColumnNames, ", ")));
        definitions.push_back(std::format("\tFOREIGN KEY ({}) REFERENCES {} ({}) ON DELETE CASCADE",
                                          join(quoteIdentifiers(dialect, junction.targetColumns), ", "),
                                          dialect.quoteIdentifier(targetInfo.tableName),
                                          join(targetColumnNames, ", ")));

        statements.push_back(std::format("{}\n{}\n);", dialect.renderCreateTablePrefix(junction.tableName, true),
                                         join(definitions, ",\n")));
    }

    return statements;
}

auto dropTableStatements(const SqlDialect& dialect, const model::ModelInfo& ownerInfo) -> std::vector<std::string>
{
    std::vector<std::string> statements;

    for (auto relation = ownerInfo.relationsInfo.rbegin(); relation != ownerInfo.relationsInfo.rend(); ++relation)
    {
        if (relation->kind == model::RelationKind::ManyToMany and relation->junction.has_value() and
            relation->junction->owningSide)
        {
            statements.push_back(dialect.renderDropTable(relation->junction->tableName, true));
        }
    }

    return statements;
}

auto linkStatement(const SqlDialect& dialect, const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
                   const binding::PrimaryKey& ownerKey, const binding::PrimaryKey& targetKey) -> Statement
{
    const auto ownerPrimaryKey = binding::getPrimaryKeyColumns(ownerInfo);
    const auto& targetInfo = relation.targetModel();
    const auto targetPrimaryKey = binding::getPrimaryKeyColumns(targetInfo);
    requireKeySize(ownerKey, ownerPrimaryKey, "owner");
    requireKeySize(targetKey, targetPrimaryKey, "target");
    Statement statement;

    if (relation.kind == model::RelationKind::ManyToMany)
    {
        if (not relation.junction.has_value())
        {
            throw std::invalid_argument{"ManyToMany relation has no junction mapping: " + relation.fieldName};
        }

        const auto& junction = relation.junction.value();
        std::vector<std::string> placeholders;

        for (std::size_t i = 0; i < ownerKey.size(); ++i)
        {
            placeholders.push_back(
                addValueParameter(dialect, statement, std::format("orm_rel_owner_{}", i), ownerKey[i]));
        }

        for (std::size_t i = 0; i < targetKey.size(); ++i)
        {
            placeholders.push_back(
                addValueParameter(dialect, statement, std::format("orm_rel_target_{}", i), targetKey[i]));
        }

        auto allColumns = junction.ownerColumns;
        allColumns.insert(allColumns.end(), junction.targetColumns.begin(), junction.targetColumns.end());
        statement.sql = dialect.renderInsertIfAbsent(InsertIfAbsentSpec{
            .tableName = junction.tableName,
            .columns = allColumns,
            .valueExpressions = placeholders,
            .conflictColumns = allColumns,
        });

        return statement;
    }

    if (relation.kind != model::RelationKind::OneToMany)
    {
        throw std::invalid_argument{"link requires a collection relation: " + relation.fieldName};
    }

    const auto& mappedRelation = findRelation(targetInfo, relation.mappedBy);
    std::vector<std::string> assignments;
    std::vector<std::string> changedPredicates;
    std::vector<std::string> targetPredicates;

    for (std::size_t i = 0; i < ownerPrimaryKey.size(); ++i)
    {
        const auto localColumn = std::format("{}_{}", mappedRelation.columnName, ownerPrimaryKey[i]->name);
        const auto quotedLocalColumn = dialect.quoteIdentifier(localColumn);
        const auto parameter = addValueParameter(dialect, statement, std::format("orm_rel_owner_{}", i), ownerKey[i]);
        assignments.push_back(std::format("{} = {}", quotedLocalColumn, parameter));
        changedPredicates.push_back(std::format("({0} IS NULL OR {0} != {1})", quotedLocalColumn, parameter));
    }

    for (std::size_t i = 0; i < targetPrimaryKey.size(); ++i)
    {
        const auto parameter = addValueParameter(dialect, statement, std::format("orm_rel_target_{}", i), targetKey[i]);
        targetPredicates.push_back(
            std::format("{} = {}", dialect.quoteIdentifier(targetPrimaryKey[i]->name), parameter));
    }

    statement.sql =
        std::format("UPDATE {} SET {} WHERE ({}) AND ({});", dialect.quoteIdentifier(targetInfo.tableName),
                    join(assignments, ", "), join(targetPredicates, " AND "), join(changedPredicates, " OR "));

    return statement;
}

auto unlinkStatement(const SqlDialect& dialect, const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
                     const binding::PrimaryKey& ownerKey, const binding::PrimaryKey& targetKey) -> Statement
{
    const auto ownerPrimaryKey = binding::getPrimaryKeyColumns(ownerInfo);
    const auto& targetInfo = relation.targetModel();
    const auto targetPrimaryKey = binding::getPrimaryKeyColumns(targetInfo);
    requireKeySize(ownerKey, ownerPrimaryKey, "owner");
    requireKeySize(targetKey, targetPrimaryKey, "target");
    Statement statement;

    if (relation.kind == model::RelationKind::ManyToMany)
    {
        if (not relation.junction.has_value())
        {
            throw std::invalid_argument{"ManyToMany relation has no junction mapping: " + relation.fieldName};
        }

        const auto& junction = relation.junction.value();
        std::vector<std::string> predicates;

        for (std::size_t i = 0; i < ownerKey.size(); ++i)
        {
            const auto parameter =
                addValueParameter(dialect, statement, std::format("orm_rel_owner_{}", i), ownerKey[i]);
            predicates.push_back(std::format("{} = {}", dialect.quoteIdentifier(junction.ownerColumns[i]), parameter));
        }

        for (std::size_t i = 0; i < targetKey.size(); ++i)
        {
            const auto parameter =
                addValueParameter(dialect, statement, std::format("orm_rel_target_{}", i), targetKey[i]);
            predicates.push_back(std::format("{} = {}", dialect.quoteIdentifier(junction.targetColumns[i]), parameter));
        }

        statement.sql = std::format("DELETE FROM {} WHERE {};", dialect.quoteIdentifier(junction.tableName),
                                    join(predicates, " AND "));

        return statement;
    }

    if (relation.kind != model::RelationKind::OneToMany)
    {
        throw std::invalid_argument{"unlink requires a collection relation: " + relation.fieldName};
    }

    const auto& mappedRelation = findRelation(targetInfo, relation.mappedBy);

    if (not mappedRelation.nullable)
    {
        throw std::invalid_argument{"Cannot unlink a non-nullable OneToMany relation: " + relation.fieldName};
    }

    std::vector<std::string> assignments;
    std::vector<std::string> ownerPredicates;
    std::vector<std::string> targetPredicates;

    for (std::size_t i = 0; i < ownerPrimaryKey.size(); ++i)
    {
        const auto localColumn = std::format("{}_{}", mappedRelation.columnName, ownerPrimaryKey[i]->name);
        const auto quotedLocalColumn = dialect.quoteIdentifier(localColumn);
        const auto nullParameterName = std::format("orm_rel_null_{}", i);
        statement.parameters.push_back(
            StatementParameter{.name = nullParameterName, .value = std::nullopt, .nullType = ownerPrimaryKey[i]->type});
        assignments.push_back(std::format("{} = {}", quotedLocalColumn, dialect.bindMarker(nullParameterName)));
        const auto ownerParameter =
            addValueParameter(dialect, statement, std::format("orm_rel_owner_{}", i), ownerKey[i]);
        ownerPredicates.push_back(std::format("{} = {}", quotedLocalColumn, ownerParameter));
    }

    for (std::size_t i = 0; i < targetPrimaryKey.size(); ++i)
    {
        const auto targetParameter =
            addValueParameter(dialect, statement, std::format("orm_rel_target_{}", i), targetKey[i]);
        targetPredicates.push_back(
            std::format("{} = {}", dialect.quoteIdentifier(targetPrimaryKey[i]->name), targetParameter));
    }

    statement.sql =
        std::format("UPDATE {} SET {} WHERE ({}) AND ({});", dialect.quoteIdentifier(targetInfo.tableName),
                    join(assignments, ", "), join(targetPredicates, " AND "), join(ownerPredicates, " AND "));

    return statement;
}

auto collectionSelectStatement(const SqlDialect& dialect, const model::ModelInfo& ownerInfo,
                               const model::RelationInfo& relation, std::string targetSelectSql,
                               const std::vector<binding::PrimaryKey>& ownerKeys, bool joinedValues) -> Statement
{
    const auto ownerPrimaryKey = binding::getPrimaryKeyColumns(ownerInfo);
    const auto& targetInfo = relation.targetModel();
    const auto targetPrimaryKey = binding::getPrimaryKeyColumns(targetInfo);
    targetSelectSql = stripTerminator(std::move(targetSelectSql));
    Statement statement;
    std::vector<std::string> ownerExpressions;
    std::vector<std::string> selectedOwnerFields;
    constexpr std::string_view targetAlias = "orm_relation_target";
    constexpr std::string_view junctionAlias = "orm_relation_junction";
    const auto quotedTargetAlias = dialect.quoteIdentifier(targetAlias);
    const auto quotedJunctionAlias = dialect.quoteIdentifier(junctionAlias);

    if (relation.kind == model::RelationKind::OneToMany)
    {
        const auto& mappedRelation = findRelation(targetInfo, relation.mappedBy);

        for (const auto* ownerColumn : ownerPrimaryKey)
        {
            const auto mappedAlias =
                joinedValues ?
                    aliases::joinedRelationColumn(mappedRelation.columnName, ownerColumn->name) :
                    aliases::unjoinedRelationColumn(targetInfo.tableName, mappedRelation.columnName, ownerColumn->name);
            const auto expression = aliases::qualifiedIdentifier(dialect, targetAlias, mappedAlias);
            ownerExpressions.push_back(expression);
            selectedOwnerFields.push_back(std::format(
                "{} AS {}", expression, dialect.quoteIdentifier(binding::relationOwnerAlias(*ownerColumn))));
        }

        const auto where = renderOwnerKeyFilter(dialect, statement, ownerExpressions, ownerKeys);
        statement.sql = std::format("SELECT {}, {}.* FROM ({}) AS {} WHERE {};", join(selectedOwnerFields, ", "),
                                    quotedTargetAlias, targetSelectSql, quotedTargetAlias, where);

        return statement;
    }

    if (relation.kind != model::RelationKind::ManyToMany or not relation.junction.has_value())
    {
        throw std::invalid_argument{"include requires a collection relation: " + relation.fieldName};
    }

    const auto& junction = relation.junction.value();

    if (junction.ownerColumns.size() != ownerPrimaryKey.size() or
        junction.targetColumns.size() != targetPrimaryKey.size())
    {
        throw std::invalid_argument{"Junction column count does not match endpoint primary keys: " +
                                    junction.tableName};
    }

    std::vector<std::string> joinPredicates;

    for (std::size_t i = 0; i < ownerPrimaryKey.size(); ++i)
    {
        const auto expression = aliases::qualifiedIdentifier(dialect, junctionAlias, junction.ownerColumns[i]);
        ownerExpressions.push_back(expression);
        selectedOwnerFields.push_back(std::format(
            "{} AS {}", expression, dialect.quoteIdentifier(binding::relationOwnerAlias(*ownerPrimaryKey[i]))));
    }

    for (std::size_t i = 0; i < targetPrimaryKey.size(); ++i)
    {
        const auto targetOutputColumn = aliases::modelColumn(targetInfo.tableName, targetPrimaryKey[i]->name);
        joinPredicates.push_back(
            std::format("{} = {}", aliases::qualifiedIdentifier(dialect, targetAlias, targetOutputColumn),
                        aliases::qualifiedIdentifier(dialect, junctionAlias, junction.targetColumns[i])));
    }

    const auto where = renderOwnerKeyFilter(dialect, statement, ownerExpressions, ownerKeys);
    statement.sql =
        std::format("SELECT {}, {}.* FROM {} AS {} JOIN ({}) AS {} ON {} WHERE {};", join(selectedOwnerFields, ", "),
                    quotedTargetAlias, dialect.quoteIdentifier(junction.tableName), quotedJunctionAlias,
                    targetSelectSql, quotedTargetAlias, join(joinPredicates, " AND "), where);

    return statement;
}

} // namespace orm::db::relations

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

auto findRelation(orm::model::ModelView model, std::string_view fieldName) -> const orm::model::RelationView&
{
    const auto* relation = model.findRelation(fieldName);
    if (relation == nullptr)
    {
        throw std::invalid_argument{"Unknown relation field: " + std::string{fieldName}};
    }

    return *relation;
}

auto requireKeySize(const orm::db::binding::PrimaryKey& key, const std::vector<const orm::model::ColumnView*>& columns,
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
        orm::db::StatementParameter{.name = name, .value = orm::db::binding::toQueryValue(value), .nullType = std::nullopt});

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
                      std::span<const std::string_view> identifiers) -> std::vector<std::string>
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
auto createTableStatements(const SqlDialect& dialect, model::ModelView owner) -> std::vector<std::string>
{
    const auto ownerPrimaryKey = binding::getPrimaryKeyColumns(owner);
    std::vector<std::string> statements;

    for (const auto& relation : owner->relations)
    {
        if (relation.kind != model::RelationKind::ManyToMany or not relation.junction.isConfigured() or
            not relation.junction.owningSide)
        {
            continue;
        }

        const auto& junction = relation.junction;
        const auto target = owner.resolveTarget(relation);
        if (target == nullptr)
        {
            throw std::logic_error{"Collection relation target is not available in the schema"};
        }
        const auto targetPrimaryKey = binding::getPrimaryKeyColumns(target);

        if (junction.ownerColumns.size() != ownerPrimaryKey.size() or
            junction.targetColumns.size() != targetPrimaryKey.size())
        {
            throw std::invalid_argument{"Junction column count does not match endpoint primary keys: " +
                                        std::string{junction.tableName}};
        }

        std::vector<std::string> definitions;

        for (std::size_t i = 0; i < ownerPrimaryKey.size(); ++i)
        {
            definitions.push_back(std::format("\t{} {} NOT NULL", dialect.quoteIdentifier(junction.ownerColumns[i]),
                                              dialect.toSqlType(ownerPrimaryKey[i]->type.value())));
        }

        for (std::size_t i = 0; i < targetPrimaryKey.size(); ++i)
        {
            definitions.push_back(std::format("\t{} {} NOT NULL", dialect.quoteIdentifier(junction.targetColumns[i]),
                                              dialect.toSqlType(targetPrimaryKey[i]->type.value())));
        }

        std::vector<std::string_view> allJunctionColumns{junction.ownerColumns.begin(), junction.ownerColumns.end()};
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
                                          dialect.quoteIdentifier(owner->tableName), join(ownerColumnNames, ", ")));
        definitions.push_back(std::format("\tFOREIGN KEY ({}) REFERENCES {} ({}) ON DELETE CASCADE",
                                          join(quoteIdentifiers(dialect, junction.targetColumns), ", "),
                                          dialect.quoteIdentifier(target->tableName), join(targetColumnNames, ", ")));

        statements.push_back(std::format("{}\n{}\n);", dialect.renderCreateTablePrefix(junction.tableName, true),
                                         join(definitions, ",\n")));
    }

    return statements;
}

auto dropTableStatements(const SqlDialect& dialect, model::ModelView owner) -> std::vector<std::string>
{
    std::vector<std::string> statements;

    for (auto relation = owner->relations.rbegin(); relation != owner->relations.rend(); ++relation)
    {
        if (relation->kind == model::RelationKind::ManyToMany and relation->junction.isConfigured() and
            relation->junction.owningSide)
        {
            statements.push_back(dialect.renderDropTable(relation->junction.tableName, true));
        }
    }

    return statements;
}

auto linkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                   const binding::PrimaryKey& ownerKey, const binding::PrimaryKey& targetKey) -> Statement
{
    const auto ownerPrimaryKey = binding::getPrimaryKeyColumns(owner);
    const auto target = owner.resolveTarget(relation);
    if (target == nullptr)
    {
        throw std::logic_error{"Collection relation target is not available in the schema"};
    }
    const auto targetPrimaryKey = binding::getPrimaryKeyColumns(target);
    requireKeySize(ownerKey, ownerPrimaryKey, "owner");
    requireKeySize(targetKey, targetPrimaryKey, "target");
    Statement statement;

    if (relation.kind == model::RelationKind::ManyToMany)
    {
        const auto junction = owner.resolveJunction(relation);
        if (not junction.isConfigured())
        {
            throw std::invalid_argument{"ManyToMany relation has no junction mapping: " +
                                        std::string{relation.fieldName}};
        }

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

        std::vector<std::string> allColumns;
        allColumns.reserve(junction.ownerColumns.size() + junction.targetColumns.size());
        std::ranges::transform(junction.ownerColumns, std::back_inserter(allColumns),
                               [](std::string_view column) { return std::string{column}; });
        std::ranges::transform(junction.targetColumns, std::back_inserter(allColumns),
                               [](std::string_view column) { return std::string{column}; });
        statement.sql = dialect.renderInsertIfAbsent(InsertIfAbsentSpec{
            .tableName = std::string{junction.tableName},
            .columns = allColumns,
            .valueExpressions = placeholders,
            .conflictColumns = allColumns,
        });

        return statement;
    }

    if (relation.kind != model::RelationKind::OneToMany)
    {
        throw std::invalid_argument{"link requires a collection relation: " + std::string{relation.fieldName}};
    }

    const auto& mappedRelation = findRelation(target, relation.mappedBy);
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
        std::format("UPDATE {} SET {} WHERE ({}) AND ({});", dialect.quoteIdentifier(target->tableName),
                    join(assignments, ", "), join(targetPredicates, " AND "), join(changedPredicates, " OR "));

    return statement;
}

auto unlinkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                     const binding::PrimaryKey& ownerKey, const binding::PrimaryKey& targetKey) -> Statement
{
    const auto ownerPrimaryKey = binding::getPrimaryKeyColumns(owner);
    const auto target = owner.resolveTarget(relation);
    if (target == nullptr)
    {
        throw std::logic_error{"Collection relation target is not available in the schema"};
    }
    const auto targetPrimaryKey = binding::getPrimaryKeyColumns(target);
    requireKeySize(ownerKey, ownerPrimaryKey, "owner");
    requireKeySize(targetKey, targetPrimaryKey, "target");
    Statement statement;

    if (relation.kind == model::RelationKind::ManyToMany)
    {
        const auto junction = owner.resolveJunction(relation);
        if (not junction.isConfigured())
        {
            throw std::invalid_argument{"ManyToMany relation has no junction mapping: " +
                                        std::string{relation.fieldName}};
        }

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
        throw std::invalid_argument{"unlink requires a collection relation: " + std::string{relation.fieldName}};
    }

    const auto& mappedRelation = findRelation(target, relation.mappedBy);

    if (not mappedRelation.nullable)
    {
        throw std::invalid_argument{"Cannot unlink a non-nullable OneToMany relation: " +
                                    std::string{relation.fieldName}};
    }

    std::vector<std::string> assignments;
    std::vector<std::string> ownerPredicates;
    std::vector<std::string> targetPredicates;

    for (std::size_t i = 0; i < ownerPrimaryKey.size(); ++i)
    {
        const auto localColumn = std::format("{}_{}", mappedRelation.columnName, ownerPrimaryKey[i]->name);
        const auto quotedLocalColumn = dialect.quoteIdentifier(localColumn);
        const auto nullParameterName = std::format("orm_rel_null_{}", i);
        statement.parameters.push_back(StatementParameter{
            .name = nullParameterName, .value = std::nullopt, .nullType = ownerPrimaryKey[i]->type.value()});
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
        std::format("UPDATE {} SET {} WHERE ({}) AND ({});", dialect.quoteIdentifier(target->tableName),
                    join(assignments, ", "), join(targetPredicates, " AND "), join(ownerPredicates, " AND "));

    return statement;
}

auto collectionSelectStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                               std::string targetSelectSql, const std::vector<binding::PrimaryKey>& ownerKeys,
                               bool joinedValues) -> Statement
{
    const auto ownerPrimaryKey = binding::getPrimaryKeyColumns(owner);
    const auto target = owner.resolveTarget(relation);
    if (target == nullptr)
    {
        throw std::logic_error{"Collection relation target is not available in the schema"};
    }
    const auto targetPrimaryKey = binding::getPrimaryKeyColumns(target);
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
        const auto& mappedRelation = findRelation(target, relation.mappedBy);

        for (const auto* ownerColumn : ownerPrimaryKey)
        {
            const auto mappedAlias =
                joinedValues ?
                    aliases::joinedRelationColumn(mappedRelation.columnName, ownerColumn->name) :
                    aliases::unjoinedRelationColumn(target->tableName, mappedRelation.columnName, ownerColumn->name);
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

    const auto junction = owner.resolveJunction(relation);
    if (relation.kind != model::RelationKind::ManyToMany or not junction.isConfigured())
    {
        throw std::invalid_argument{"include requires a collection relation: " + std::string{relation.fieldName}};
    }

    if (junction.ownerColumns.size() != ownerPrimaryKey.size() or
        junction.targetColumns.size() != targetPrimaryKey.size())
    {
        throw std::invalid_argument{"Junction column count does not match endpoint primary keys: " +
                                    std::string{junction.tableName}};
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
        const auto targetOutputColumn = aliases::modelColumn(target->tableName, targetPrimaryKey[i]->name);
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

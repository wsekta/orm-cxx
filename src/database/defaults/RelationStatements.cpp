#include "orm-cxx/database/RelationStatements.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "../sqlite/SqliteTypeTranslator.hpp"

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

auto findRelation(const orm::model::ModelInfo& modelInfo, const std::string& fieldName)
    -> const orm::model::RelationInfo&
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

auto requireKeySize(const orm::db::binding::PrimaryKey& key,
                    const std::vector<const orm::model::ColumnInfo*>& columns, std::string_view endpoint) -> void
{
    if (key.size() != columns.size())
    {
        throw std::invalid_argument{std::format("Incomplete {} relation primary key", endpoint)};
    }
}

auto addValueParameter(orm::db::Statement& statement, std::string name,
                       const orm::query::QueryValue::Value& value) -> std::string
{
    statement.parameters.push_back(
        orm::db::StatementParameter{.name = name, .value = orm::db::binding::toQueryValue(value)});

    return ":" + name;
}

auto renderOwnerKeyFilter(orm::db::Statement& statement, const std::vector<std::string>& expressions,
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
            const auto parameter = addValueParameter(statement, parameterName, keys[keyIndex][columnIndex]);
            columnPredicates.push_back(std::format("{} = {}", expressions[columnIndex], parameter));
        }

        keyPredicates.push_back("(" + join(columnPredicates, " AND ") + ")");
    }

    return "(" + join(keyPredicates, " OR ") + ")";
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
auto createTableStatements(const model::ModelInfo& ownerInfo) -> std::vector<std::string>
{
    sqlite::SqliteTypeTranslator translator;
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
            definitions.push_back(std::format("\t{} {} NOT NULL", junction.ownerColumns[i],
                                              translator.toSqlType(ownerPrimaryKey[i]->type)));
        }

        for (std::size_t i = 0; i < targetPrimaryKey.size(); ++i)
        {
            definitions.push_back(std::format("\t{} {} NOT NULL", junction.targetColumns[i],
                                              translator.toSqlType(targetPrimaryKey[i]->type)));
        }

        auto allJunctionColumns = junction.ownerColumns;
        allJunctionColumns.insert(allJunctionColumns.end(), junction.targetColumns.begin(), junction.targetColumns.end());
        definitions.push_back(std::format("\tPRIMARY KEY ({})", join(allJunctionColumns, ", ")));

        std::vector<std::string> ownerColumnNames;
        std::vector<std::string> targetColumnNames;

        for (const auto* column : ownerPrimaryKey)
        {
            ownerColumnNames.push_back(column->name);
        }

        for (const auto* column : targetPrimaryKey)
        {
            targetColumnNames.push_back(column->name);
        }

        definitions.push_back(std::format("\tFOREIGN KEY ({}) REFERENCES {} ({}) ON DELETE CASCADE",
                                          join(junction.ownerColumns, ", "), ownerInfo.tableName,
                                          join(ownerColumnNames, ", ")));
        definitions.push_back(std::format("\tFOREIGN KEY ({}) REFERENCES {} ({}) ON DELETE CASCADE",
                                          join(junction.targetColumns, ", "), targetInfo.tableName,
                                          join(targetColumnNames, ", ")));

        statements.push_back(std::format("CREATE TABLE IF NOT EXISTS {} (\n{}\n);", junction.tableName,
                                         join(definitions, ",\n")));
    }

    return statements;
}

auto dropTableStatements(const model::ModelInfo& ownerInfo) -> std::vector<std::string>
{
    std::vector<std::string> statements;

    for (auto relation = ownerInfo.relationsInfo.rbegin(); relation != ownerInfo.relationsInfo.rend(); ++relation)
    {
        if (relation->kind == model::RelationKind::ManyToMany and relation->junction.has_value() and
            relation->junction->owningSide)
        {
            statements.push_back(std::format("DROP TABLE IF EXISTS {};", relation->junction->tableName));
        }
    }

    return statements;
}

auto linkStatement(const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
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
            placeholders.push_back(addValueParameter(statement, std::format("orm_rel_owner_{}", i), ownerKey[i]));
        }

        for (std::size_t i = 0; i < targetKey.size(); ++i)
        {
            placeholders.push_back(addValueParameter(statement, std::format("orm_rel_target_{}", i), targetKey[i]));
        }

        auto allColumns = junction.ownerColumns;
        allColumns.insert(allColumns.end(), junction.targetColumns.begin(), junction.targetColumns.end());
        statement.sql = std::format("INSERT INTO {} ({}) VALUES ({}) ON CONFLICT ({}) DO NOTHING;",
                                    junction.tableName, join(allColumns, ", "), join(placeholders, ", "),
                                    join(allColumns, ", "));

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
        const auto parameter =
            addValueParameter(statement, std::format("orm_rel_owner_{}", i), ownerKey[i]);
        assignments.push_back(std::format("{} = {}", localColumn, parameter));
        changedPredicates.push_back(std::format("({0} IS NULL OR {0} != {1})", localColumn, parameter));
    }

    for (std::size_t i = 0; i < targetPrimaryKey.size(); ++i)
    {
        const auto parameter =
            addValueParameter(statement, std::format("orm_rel_target_{}", i), targetKey[i]);
        targetPredicates.push_back(std::format("{} = {}", targetPrimaryKey[i]->name, parameter));
    }

    statement.sql = std::format("UPDATE {} SET {} WHERE ({}) AND ({});", targetInfo.tableName,
                                join(assignments, ", "), join(targetPredicates, " AND "),
                                join(changedPredicates, " OR "));

    return statement;
}

auto unlinkStatement(const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
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
                addValueParameter(statement, std::format("orm_rel_owner_{}", i), ownerKey[i]);
            predicates.push_back(std::format("{} = {}", junction.ownerColumns[i], parameter));
        }

        for (std::size_t i = 0; i < targetKey.size(); ++i)
        {
            const auto parameter =
                addValueParameter(statement, std::format("orm_rel_target_{}", i), targetKey[i]);
            predicates.push_back(std::format("{} = {}", junction.targetColumns[i], parameter));
        }

        statement.sql =
            std::format("DELETE FROM {} WHERE {};", junction.tableName, join(predicates, " AND "));

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
        const auto nullParameterName = std::format("orm_rel_null_{}", i);
        statement.parameters.push_back(StatementParameter{.name = nullParameterName,
                                                           .value = std::nullopt,
                                                           .nullType = ownerPrimaryKey[i]->type});
        assignments.push_back(std::format("{} = :{}", localColumn, nullParameterName));
        const auto ownerParameter =
            addValueParameter(statement, std::format("orm_rel_owner_{}", i), ownerKey[i]);
        ownerPredicates.push_back(std::format("{} = {}", localColumn, ownerParameter));
    }

    for (std::size_t i = 0; i < targetPrimaryKey.size(); ++i)
    {
        const auto targetParameter =
            addValueParameter(statement, std::format("orm_rel_target_{}", i), targetKey[i]);
        targetPredicates.push_back(std::format("{} = {}", targetPrimaryKey[i]->name, targetParameter));
    }

    statement.sql = std::format("UPDATE {} SET {} WHERE ({}) AND ({});", targetInfo.tableName,
                                join(assignments, ", "), join(targetPredicates, " AND "),
                                join(ownerPredicates, " AND "));

    return statement;
}

auto collectionSelectStatement(const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
                               std::string targetSelectSql, const std::vector<binding::PrimaryKey>& ownerKeys,
                               bool joinedValues) -> Statement
{
    const auto ownerPrimaryKey = binding::getPrimaryKeyColumns(ownerInfo);
    const auto& targetInfo = relation.targetModel();
    const auto targetPrimaryKey = binding::getPrimaryKeyColumns(targetInfo);
    targetSelectSql = stripTerminator(std::move(targetSelectSql));
    Statement statement;
    std::vector<std::string> ownerExpressions;
    std::vector<std::string> selectedOwnerFields;

    if (relation.kind == model::RelationKind::OneToMany)
    {
        const auto& mappedRelation = findRelation(targetInfo, relation.mappedBy);

        for (const auto* ownerColumn : ownerPrimaryKey)
        {
            const auto mappedAlias =
                joinedValues ? std::format("{}_{}", mappedRelation.columnName, ownerColumn->name) :
                               std::format("{}_{}_{}", targetInfo.tableName, mappedRelation.columnName,
                                           ownerColumn->name);
            const auto expression = std::format("orm_relation_target.{}", mappedAlias);
            ownerExpressions.push_back(expression);
            selectedOwnerFields.push_back(
                std::format("{} AS {}", expression, binding::relationOwnerAlias(*ownerColumn)));
        }

        const auto where = renderOwnerKeyFilter(statement, ownerExpressions, ownerKeys);
        statement.sql = std::format("SELECT {}, orm_relation_target.* FROM ({}) AS orm_relation_target WHERE {};",
                                    join(selectedOwnerFields, ", "), targetSelectSql, where);

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
        const auto expression = std::format("orm_relation_junction.{}", junction.ownerColumns[i]);
        ownerExpressions.push_back(expression);
        selectedOwnerFields.push_back(
            std::format("{} AS {}", expression, binding::relationOwnerAlias(*ownerPrimaryKey[i])));
    }

    for (std::size_t i = 0; i < targetPrimaryKey.size(); ++i)
    {
        joinPredicates.push_back(std::format("orm_relation_target.{}_{} = orm_relation_junction.{}",
                                             targetInfo.tableName, targetPrimaryKey[i]->name,
                                             junction.targetColumns[i]));
    }

    const auto where = renderOwnerKeyFilter(statement, ownerExpressions, ownerKeys);
    statement.sql = std::format(
        "SELECT {}, orm_relation_target.* FROM {} AS orm_relation_junction JOIN ({}) AS orm_relation_target ON {} "
        "WHERE {};",
        join(selectedOwnerFields, ", "), junction.tableName, targetSelectSql, join(joinPredicates, " AND "), where);

    return statement;
}
} // namespace orm::db::relations

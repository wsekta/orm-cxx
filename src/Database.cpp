#include "orm-cxx/database.hpp"

#include <format>
#include <regex>
#include <stdexcept>

namespace
{
const std::regex sqliteRegex(R"(sqlite3\:\/\/.*)");
}

namespace orm
{
auto detail::normalizeAffectedRows(long long affectedRows) -> std::size_t
{
    if (affectedRows < 0)
    {
        throw std::runtime_error{"Database backend did not report affected row count"};
    }

    return static_cast<std::size_t>(affectedRows);
}

Database::Database() : backendType{db::BackendType::Empty} {}

auto Database::connect(const std::string& connectionString) -> void
{
    if (std::regex_match(connectionString, sqliteRegex))
    {
        backendType = db::BackendType::Sqlite;
    }

    sql.open(connectionString);

    if (backendType == db::BackendType::Sqlite)
    {
        sql << "PRAGMA foreign_keys = ON;";
    }
}

auto Database::disconnect() -> void
{
    backendType = db::BackendType::Empty;

    sql.close();
}

auto Database::getBackendType() -> db::BackendType
{
    return backendType;
}

auto Database::beginTransaction() -> void
{
    transaction = std::make_unique<soci::transaction>(sql);
}

auto Database::commitTransaction() -> void
{
    transaction->commit();
    transaction.reset();
}

auto Database::rollbackTransaction() -> void
{
    transaction->rollback();
    transaction.reset();
}

auto Database::executeMutation(const db::Statement& statement) -> std::size_t
{
    soci::values parameterValues;
    detail::bindStatementParameters(parameterValues, statement.parameters);

    auto executeAndGetAffectedRows = [](soci::statement& preparedStatement) -> std::size_t
    {
        preparedStatement.execute(true);

        return detail::normalizeAffectedRows(preparedStatement.get_affected_rows());
    };

    if (statement.parameters.empty())
    {
        soci::statement preparedStatement = (sql.prepare << statement.sql);

        return executeAndGetAffectedRows(preparedStatement);
    }

    soci::statement preparedStatement = (sql.prepare << statement.sql, soci::use(parameterValues));

    return executeAndGetAffectedRows(preparedStatement);
}

auto Database::relationEndpointExists(const model::ModelInfo& modelInfo,
                                      const db::binding::PrimaryKey& key) -> bool
{
    const auto primaryKeyColumns = db::binding::getPrimaryKeyColumns(modelInfo);

    if (primaryKeyColumns.size() != key.size())
    {
        throw std::invalid_argument{"Incomplete relation endpoint primary key"};
    }

    db::Statement statement;
    std::string where;

    for (std::size_t i = 0; i < primaryKeyColumns.size(); ++i)
    {
        if (not where.empty())
        {
            where += " AND ";
        }

        const auto parameterName = std::format("orm_endpoint_{}", i);
        where += std::format("{} = :{}", primaryKeyColumns[i]->name, parameterName);
        statement.parameters.push_back(db::StatementParameter{
            .name = parameterName, .value = db::binding::toQueryValue(key[i])});
    }

    statement.sql = std::format("SELECT COUNT(*) FROM {} WHERE {};", modelInfo.tableName, where);
    soci::values parameterValues;
    detail::bindStatementParameters(parameterValues, statement.parameters);
    long long count{};
    sql << statement.sql, soci::use(parameterValues), soci::into(count);

    return count > 0;
}

auto Database::tableExists(std::string_view tableName) -> bool
{
    if (backendType != db::BackendType::Sqlite)
    {
        throw std::invalid_argument{"Relation-table endpoint validation is not supported by this backend"};
    }

    auto name = std::string{tableName};
    int count{};
    sql << "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = :name;", soci::use(name, "name"),
        soci::into(count);

    return count > 0;
}

auto Database::ensureRelationTableEndpointsExist(const model::ModelInfo& ownerInfo) -> void
{
    for (const auto& relation : ownerInfo.relationsInfo)
    {
        if (relation.kind != model::RelationKind::ManyToMany or not relation.junction.has_value() or
            not relation.junction->owningSide)
        {
            continue;
        }

        if (not tableExists(ownerInfo.tableName) or not tableExists(relation.targetModel().tableName))
        {
            throw std::invalid_argument{"ManyToMany endpoint tables must exist before creating relation tables"};
        }
    }
}
} // namespace orm

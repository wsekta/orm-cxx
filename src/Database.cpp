#include "orm-cxx/database.hpp"

#include <regex>
#include <stdexcept>

namespace
{
const std::regex sqliteRegex(R"(sqlite3\:\/\/.*)");
}

namespace orm
{
Database::Database() : backendType{db::BackendType::Empty} {}

auto Database::connect(const std::string& connectionString) -> void
{
    if (std::regex_match(connectionString, sqliteRegex))
    {
        backendType = db::BackendType::Sqlite;
    }

    sql.open(connectionString);
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

        const auto affectedRows = preparedStatement.get_affected_rows();
        if (affectedRows < 0)
        {
            throw std::runtime_error{"Database backend did not report affected row count"};
        }

        return static_cast<std::size_t>(affectedRows);
    };

    if (statement.parameters.empty())
    {
        soci::statement preparedStatement = (sql.prepare << statement.sql);

        return executeAndGetAffectedRows(preparedStatement);
    }

    soci::statement preparedStatement = (sql.prepare << statement.sql, soci::use(parameterValues));

    return executeAndGetAffectedRows(preparedStatement);
}
} // namespace orm

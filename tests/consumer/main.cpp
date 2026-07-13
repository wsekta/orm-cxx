#include "orm-cxx/database.hpp"

namespace consumer_models
{
struct ConsumerModel
{
    int id;
};
} // namespace consumer_models

namespace
{
class ConsumerDialect final : public orm::db::SqlDialect
{
public:
    auto quoteIdentifier(std::string_view identifier) const -> std::string override
    {
        return std::string{identifier};
    }

    auto bindMarker(std::string_view logicalName) const -> std::string override
    {
        return ":" + std::string{logicalName};
    }

    auto toSqlType(orm::model::ColumnType /*type*/) const -> std::string override
    {
        return "INTEGER";
    }

    auto renderCreateTablePrefix(std::string_view tableName, bool /*ifNotExists*/) const -> std::string override
    {
        return "CREATE TABLE " + std::string{tableName} + " (";
    }

    auto renderDropTable(std::string_view tableName, bool /*ifExists*/) const -> std::string override
    {
        return "DROP TABLE " + std::string{tableName} + ";";
    }

    auto renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string override
    {
        return std::string{columnName} + " INTEGER PRIMARY KEY";
    }

    auto renderPagination(const orm::db::PaginationSpec& /*pagination*/) const -> std::string override
    {
        return {};
    }

    auto renderInsertIfAbsent(const orm::db::InsertIfAbsentSpec& /*insert*/) const -> std::string override
    {
        return {};
    }
};
} // namespace

int main()
{
    orm::Database database;

    const orm::db::CommandGeneratorFactory factory;
    const auto* sqlite = factory.findBackend("sqlite3://:memory:");

#if ORM_CXX_ENABLE_SQLITE_BACKEND
    if (sqlite == nullptr)
    {
        return 1;
    }
#else
    if (sqlite != nullptr)
    {
        return 1;
    }
#endif

    const ConsumerDialect dialect;

    if (not orm::db::relations::createTableStatements(dialect,
                                                      orm::Model<consumer_models::ConsumerModel>::getModelInfo())
                .empty())
    {
        return 2;
    }

    return 0;
}

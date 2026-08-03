#include <utility>

#include "orm-cxx/database/RelationStatements.hpp"
#include "orm-cxx/database/sqlite/SqliteDialect.hpp"

namespace
{
auto sqliteDialect() -> const orm::db::SqlDialect&
{
    static const orm::db::sqlite::SqliteDialect dialect;

    return dialect;
}
} // namespace

namespace orm::db::relations
{
auto createTableStatements(model::ModelView owner) -> std::vector<std::string>
{
    return createTableStatements(sqliteDialect(), owner);
}

auto dropTableStatements(model::ModelView owner) -> std::vector<std::string>
{
    return dropTableStatements(sqliteDialect(), owner);
}

auto linkStatement(model::ModelView owner, model::RelationView relation, const binding::PrimaryKey& ownerKey,
                   const binding::PrimaryKey& targetKey) -> Statement
{
    return linkStatement(sqliteDialect(), owner, relation, ownerKey, targetKey);
}

auto unlinkStatement(model::ModelView owner, model::RelationView relation, const binding::PrimaryKey& ownerKey,
                     const binding::PrimaryKey& targetKey) -> Statement
{
    return unlinkStatement(sqliteDialect(), owner, relation, ownerKey, targetKey);
}

auto collectionSelectStatement(model::ModelView owner, model::RelationView relation, std::string targetSelectSql,
                               const std::vector<binding::PrimaryKey>& ownerKeys, bool joinedValues) -> Statement
{
    return collectionSelectStatement(sqliteDialect(), owner, relation, std::move(targetSelectSql), ownerKeys,
                                     joinedValues);
}
} // namespace orm::db::relations

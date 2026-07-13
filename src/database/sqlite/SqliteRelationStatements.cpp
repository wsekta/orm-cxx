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
auto createTableStatements(const model::ModelInfo& ownerInfo) -> std::vector<std::string>
{
    return createTableStatements(sqliteDialect(), ownerInfo);
}

auto dropTableStatements(const model::ModelInfo& ownerInfo) -> std::vector<std::string>
{
    return dropTableStatements(sqliteDialect(), ownerInfo);
}

auto linkStatement(const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
                   const binding::PrimaryKey& ownerKey, const binding::PrimaryKey& targetKey) -> Statement
{
    return linkStatement(sqliteDialect(), ownerInfo, relation, ownerKey, targetKey);
}

auto unlinkStatement(const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
                     const binding::PrimaryKey& ownerKey, const binding::PrimaryKey& targetKey) -> Statement
{
    return unlinkStatement(sqliteDialect(), ownerInfo, relation, ownerKey, targetKey);
}

auto collectionSelectStatement(const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
                               std::string targetSelectSql, const std::vector<binding::PrimaryKey>& ownerKeys,
                               bool joinedValues) -> Statement
{
    return collectionSelectStatement(sqliteDialect(), ownerInfo, relation, std::move(targetSelectSql), ownerKeys,
                                     joinedValues);
}
} // namespace orm::db::relations

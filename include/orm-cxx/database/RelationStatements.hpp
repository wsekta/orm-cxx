#pragma once

#include <string>
#include <vector>

#include "Statement.hpp"
#include "binding/PrimaryKey.hpp"
#include "orm-cxx/model/ModelInfo.hpp"

namespace orm::db::relations
{
[[nodiscard]] auto createTableStatements(const model::ModelInfo& ownerInfo) -> std::vector<std::string>;
[[nodiscard]] auto dropTableStatements(const model::ModelInfo& ownerInfo) -> std::vector<std::string>;

[[nodiscard]] auto linkStatement(const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
                                 const binding::PrimaryKey& ownerKey, const binding::PrimaryKey& targetKey)
    -> Statement;
[[nodiscard]] auto unlinkStatement(const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
                                   const binding::PrimaryKey& ownerKey, const binding::PrimaryKey& targetKey)
    -> Statement;

[[nodiscard]] auto collectionSelectStatement(const model::ModelInfo& ownerInfo,
                                             const model::RelationInfo& relation, std::string targetSelectSql,
                                             const std::vector<binding::PrimaryKey>& ownerKeys, bool joinedValues)
    -> Statement;
} // namespace orm::db::relations

#pragma once

#include <string>
#include <vector>

#include "binding/PrimaryKey.hpp"
#include "orm-cxx/model/ModelInfo.hpp"
#include "SqlDialect.hpp"
#include "Statement.hpp"

namespace orm::db::relations
{
[[nodiscard]] auto createTableStatements(const SqlDialect& dialect,
                                         const model::ModelInfo& ownerInfo) -> std::vector<std::string>;
#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
[[nodiscard]] auto createTableStatements(const model::ModelInfo& ownerInfo) -> std::vector<std::string>;
#endif
[[nodiscard]] auto dropTableStatements(const SqlDialect& dialect,
                                       const model::ModelInfo& ownerInfo) -> std::vector<std::string>;
#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
[[nodiscard]] auto dropTableStatements(const model::ModelInfo& ownerInfo) -> std::vector<std::string>;
#endif

[[nodiscard]] auto linkStatement(const SqlDialect& dialect, const model::ModelInfo& ownerInfo,
                                 const model::RelationInfo& relation, const binding::PrimaryKey& ownerKey,
                                 const binding::PrimaryKey& targetKey) -> Statement;
#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
[[nodiscard]] auto linkStatement(const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
                                 const binding::PrimaryKey& ownerKey,
                                 const binding::PrimaryKey& targetKey) -> Statement;
#endif
[[nodiscard]] auto unlinkStatement(const SqlDialect& dialect, const model::ModelInfo& ownerInfo,
                                   const model::RelationInfo& relation, const binding::PrimaryKey& ownerKey,
                                   const binding::PrimaryKey& targetKey) -> Statement;
#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
[[nodiscard]] auto unlinkStatement(const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
                                   const binding::PrimaryKey& ownerKey,
                                   const binding::PrimaryKey& targetKey) -> Statement;
#endif

[[nodiscard]] auto collectionSelectStatement(const SqlDialect& dialect, const model::ModelInfo& ownerInfo,
                                             const model::RelationInfo& relation, std::string targetSelectSql,
                                             const std::vector<binding::PrimaryKey>& ownerKeys,
                                             bool joinedValues) -> Statement;
#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
[[nodiscard]] auto collectionSelectStatement(const model::ModelInfo& ownerInfo, const model::RelationInfo& relation,
                                             std::string targetSelectSql,
                                             const std::vector<binding::PrimaryKey>& ownerKeys,
                                             bool joinedValues) -> Statement;
#endif
} // namespace orm::db::relations

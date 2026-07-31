#pragma once

#include <string>
#include <vector>

#include "binding/PrimaryKey.hpp"
#include "orm-cxx/model/ModelView.hpp"
#include "SqlDialect.hpp"
#include "Statement.hpp"

namespace orm::db::relations
{
[[nodiscard]] auto createTableStatements(const SqlDialect& dialect, model::ModelView owner) -> std::vector<std::string>;
#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
[[nodiscard]] auto createTableStatements(model::ModelView owner) -> std::vector<std::string>;
#endif
[[nodiscard]] auto dropTableStatements(const SqlDialect& dialect, model::ModelView owner) -> std::vector<std::string>;
#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
[[nodiscard]] auto dropTableStatements(model::ModelView owner) -> std::vector<std::string>;
#endif

[[nodiscard]] auto linkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                                 const binding::PrimaryKey& ownerKey,
                                 const binding::PrimaryKey& targetKey) -> Statement;
#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
[[nodiscard]] auto linkStatement(model::ModelView owner, model::RelationView relation,
                                 const binding::PrimaryKey& ownerKey,
                                 const binding::PrimaryKey& targetKey) -> Statement;
#endif
[[nodiscard]] auto unlinkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                                   const binding::PrimaryKey& ownerKey,
                                   const binding::PrimaryKey& targetKey) -> Statement;
#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
[[nodiscard]] auto unlinkStatement(model::ModelView owner, model::RelationView relation,
                                   const binding::PrimaryKey& ownerKey,
                                   const binding::PrimaryKey& targetKey) -> Statement;
#endif

[[nodiscard]] auto collectionSelectStatement(const SqlDialect& dialect, model::ModelView owner,
                                             model::RelationView relation, std::string targetSelectSql,
                                             const std::vector<binding::PrimaryKey>& ownerKeys,
                                             bool joinedValues) -> Statement;
#if defined(ORM_CXX_ENABLE_SQLITE_BACKEND) && ORM_CXX_ENABLE_SQLITE_BACKEND
[[nodiscard]] auto collectionSelectStatement(model::ModelView owner, model::RelationView relation,
                                             std::string targetSelectSql,
                                             const std::vector<binding::PrimaryKey>& ownerKeys,
                                             bool joinedValues) -> Statement;
#endif
} // namespace orm::db::relations

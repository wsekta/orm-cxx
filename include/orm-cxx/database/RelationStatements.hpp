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
[[nodiscard]] auto dropTableStatements(const SqlDialect& dialect, model::ModelView owner) -> std::vector<std::string>;

[[nodiscard]] auto linkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                                 const binding::PrimaryKey& ownerKey,
                                 const binding::PrimaryKey& targetKey) -> Statement;
[[nodiscard]] auto unlinkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                                   const binding::PrimaryKey& ownerKey,
                                   const binding::PrimaryKey& targetKey) -> Statement;

[[nodiscard]] auto collectionSelectStatement(const SqlDialect& dialect, model::ModelView owner,
                                             model::RelationView relation, std::string targetSelectSql,
                                             const std::vector<binding::PrimaryKey>& ownerKeys,
                                             bool joinedValues) -> Statement;
} // namespace orm::db::relations

#include "orm-cxx/database/detail/SqliteRelationDeclarations.hpp"

#pragma once

#include <string_view>
#include <vector>

#include "orm-cxx/database/BackendType.hpp"
#include "orm-cxx/database/binding/PrimaryKey.hpp"
#include "orm-cxx/model/ModelView.hpp"

namespace orm::detail
{
// These validators are shared by DatabaseCore's operations. Keeping them separate
// also lets tests exercise defensive checks with immutable malformed descriptors.
[[nodiscard]] auto hasOwningJunction(model::ModelView owner) -> bool;
[[nodiscard]] auto requireToOneTarget(model::ModelView owner, const model::ColumnView& column) -> model::ModelView;
[[nodiscard]] auto requireRelationTarget(model::ModelView owner,
                                         const model::RelationView& relation) -> model::ModelView;
[[nodiscard]] auto requireEndpointKeyColumns(model::ModelView model, const db::binding::PrimaryKey& key)
    -> std::vector<const model::ColumnView*>;
[[nodiscard]] auto owningJunctionRelations(model::ModelView owner) -> std::vector<const model::RelationView*>;
[[nodiscard]] auto requireOwningJunctionTarget(model::ModelView owner,
                                               const model::RelationView& relation) -> model::ModelView;
[[nodiscard]] auto requireSupportedRelatedTarget(model::ModelView owner, const model::ColumnView& column,
                                                 db::BackendType backendType,
                                                 std::string_view operation) -> model::ModelView;
[[nodiscard]] auto requireIncludedRelationTarget(model::ModelView owner, const model::RelationView& relation,
                                                 db::BackendType backendType) -> model::ModelView;
[[nodiscard]] auto requireCollectionTarget(model::ModelView owner, const model::RelationView& relation,
                                           model::TypeId expectedType) -> model::ModelView;
} // namespace orm::detail

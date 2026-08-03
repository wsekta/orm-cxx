#pragma once

#include "orm-cxx/database/Statement.hpp"
#include "orm-cxx/model/ModelView.hpp"
#include "orm-cxx/query/Predicate.hpp"

namespace orm::db::commands
{
class DeleteCommand
{
public:
    virtual ~DeleteCommand() = default;

    [[nodiscard]] virtual auto remove(model::ModelView model, const query::Predicate& predicate) const -> Statement = 0;
};
} // namespace orm::db::commands

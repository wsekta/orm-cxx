#pragma once

#include "orm-cxx/database/commands/SelectCommand.hpp"
#include "orm-cxx/database/SqlDialect.hpp"
#include "SqlRenderer.hpp"

namespace orm::db::commands
{
class DefaultSelectCommand : public SelectCommand
{
public:
    explicit DefaultSelectCommand(const SqlDialect& dialectInit);

    [[nodiscard]] auto select(model::ModelView model,
                              const query::detail::SelectSpec& spec) const -> SelectStatement override;

private:
    static auto getSelectFields(model::ModelView model, const query::detail::SelectSpec& spec,
                                RenderContext& context) -> std::string;
    static auto getFullModelSelectFields(bool shouldJoin, model::ModelView model,
                                         const SqlDialect& dialect) -> std::string;
    static auto getProjectionSelectFields(const std::vector<query::detail::Projection>& projections,
                                          RenderContext& context) -> std::string;
    static auto renderProjectionSource(const query::detail::ProjectionSource& source,
                                       RenderContext& context) -> std::string;
    static auto renderAggregate(const query::detail::AggregateExpression& aggregate,
                                RenderContext& context) -> std::string;
    static auto getForeignModelSelectFields(bool shouldJoin, const std::string& foreginModelFieldName,
                                            model::ModelView target, model::ModelView model,
                                            const SqlDialect& dialect) -> std::string;
    static auto getJoins(bool shouldJoin, model::ModelView model, const SqlDialect& dialect) -> std::string;
    static auto getGroupBy(const query::detail::SelectSpec& spec, RenderContext& context) -> std::string;
    static auto getHaving(const std::optional<query::detail::AggregatePredicate>& having,
                          RenderContext& context) -> std::string;
    static auto renderAggregatePredicate(const query::detail::AggregatePredicateNode& node,
                                         RenderContext& context) -> std::string;
    static auto renderAggregatePredicate(const query::detail::AggregatePredicateNodePtr& node,
                                         RenderContext& context) -> std::string;
    static auto getOrderBy(const query::detail::SelectSpec& spec, RenderContext& context) -> std::string;

    const SqlDialect& dialect;
};
} // namespace orm::db::commands

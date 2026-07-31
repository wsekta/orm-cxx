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

    [[nodiscard]] auto select(model::ModelView model, const query::SelectSpec& spec) const -> SelectStatement override;

private:
    static auto getSelectFields(model::ModelView model, const query::SelectSpec& spec,
                                RenderContext& context) -> std::string;
    static auto getFullModelSelectFields(bool shouldJoin, model::ModelView model,
                                         const SqlDialect& dialect) -> std::string;
    static auto getProjectionSelectFields(const std::vector<query::Projection>& projections,
                                          RenderContext& context) -> std::string;
    static auto renderProjectionSource(const query::ProjectionSource& source, RenderContext& context) -> std::string;
    static auto renderAggregate(const query::AggregateExpression& aggregate, RenderContext& context) -> std::string;
    static auto getForeignModelSelectFields(bool shouldJoin, const std::string& foreginModelFieldName,
                                            model::ModelView target, model::ModelView model,
                                            const SqlDialect& dialect) -> std::string;
    static auto getJoins(bool shouldJoin, model::ModelView model, const SqlDialect& dialect) -> std::string;
    static auto getGroupBy(const query::SelectSpec& spec, RenderContext& context) -> std::string;
    static auto getHaving(const std::optional<query::AggregatePredicate>& having,
                          RenderContext& context) -> std::string;
    static auto renderAggregatePredicate(const query::AggregatePredicateNode& node,
                                         RenderContext& context) -> std::string;
    static auto renderAggregatePredicate(const query::AggregatePredicateNodePtr& node,
                                         RenderContext& context) -> std::string;
    static auto getOrderBy(const query::SelectSpec& spec, RenderContext& context) -> std::string;

    const SqlDialect& dialect;
};
} // namespace orm::db::commands

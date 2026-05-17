#include "DefaultSelectCommand.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "orm-cxx/utils/StringUtils.hpp"

using orm::utils::removeLastComma;

namespace
{
template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

auto splitPath(const std::string& path) -> std::vector<std::string>
{
    std::vector<std::string> parts;
    std::size_t currentPosition = 0;

    while (currentPosition <= path.size())
    {
        const auto nextPosition = path.find('.', currentPosition);
        const auto part = path.substr(currentPosition, nextPosition - currentPosition);

        if (part.empty())
        {
            throw std::invalid_argument{"Column path contains an empty segment: " + path};
        }

        parts.push_back(part);

        if (nextPosition == std::string::npos)
        {
            break;
        }

        currentPosition = nextPosition + 1;
    }

    return parts;
}

auto join(const std::vector<std::string>& parts, std::string_view separator) -> std::string
{
    std::string joined;

    for (const auto& part : parts)
    {
        joined += part;
        joined += separator;
    }

    if (not joined.empty())
    {
        joined.resize(joined.size() - separator.size());
    }

    return joined;
}

auto startsWith(std::string_view value, std::string_view prefix) -> bool
{
    return value.substr(0, prefix.size()) == prefix;
}

auto findColumnInfo(const orm::model::ModelInfo& modelInfo, const std::string& fieldOrColumnName)
    -> const orm::model::ColumnInfo*
{
    const auto columnInfo =
        std::ranges::find_if(modelInfo.columnsInfo, [&fieldOrColumnName](const orm::model::ColumnInfo& column)
                             { return column.fieldName == fieldOrColumnName or column.name == fieldOrColumnName; });

    if (columnInfo == modelInfo.columnsInfo.end())
    {
        return nullptr;
    }

    return &*columnInfo;
}

auto getColumnInfoOrThrow(const orm::model::ModelInfo& modelInfo, const std::string& fieldOrColumnName)
    -> const orm::model::ColumnInfo&
{
    const auto* columnInfo = findColumnInfo(modelInfo, fieldOrColumnName);

    if (columnInfo == nullptr)
    {
        throw std::invalid_argument{"Unknown column path segment: " + fieldOrColumnName};
    }

    return *columnInfo;
}

auto getForeignModelInfoOrThrow(const orm::model::ModelInfo& modelInfo, const orm::model::ColumnInfo& columnInfo)
    -> const orm::model::ModelInfo&
{
    if (not columnInfo.isForeignModel)
    {
        throw std::invalid_argument{"Column is not a related model: " + columnInfo.fieldName};
    }

    return modelInfo.foreignModelsInfo.at(columnInfo.name);
}
} // namespace

namespace orm::db::commands
{
struct DefaultSelectCommand::RenderContext
{
    const query::QueryData& queryData;
    std::vector<query::QueryParameter> parameters;
    std::unordered_set<std::string> parameterNames;
    std::size_t nextParameterIndex{};
};

namespace
{
auto renderColumn(const query::Column& column, const DefaultSelectCommand::RenderContext& context) -> std::string
{
    const auto parts = splitPath(column.getPath());

    if (parts.size() == 1)
    {
        const auto& columnInfo = getColumnInfoOrThrow(context.queryData.modelInfo, parts[0]);

        if (columnInfo.isForeignModel)
        {
            throw std::invalid_argument{"Use a related field path instead of the related model itself: " +
                                        column.getPath()};
        }

        return std::format("{}.{}", context.queryData.modelInfo.tableName, columnInfo.name);
    }

    if (parts.size() == 2)
    {
        const auto& relatedColumnInfo = getColumnInfoOrThrow(context.queryData.modelInfo, parts[0]);
        const auto& foreignModelInfo = getForeignModelInfoOrThrow(context.queryData.modelInfo, relatedColumnInfo);
        const auto& foreignColumnInfo = getColumnInfoOrThrow(foreignModelInfo, parts[1]);

        if (context.queryData.shouldJoin)
        {
            return std::format("{}.{}", relatedColumnInfo.name, foreignColumnInfo.name);
        }

        if (not foreignColumnInfo.isPrimaryKey)
        {
            throw std::invalid_argument{"Cannot filter by non-id related field when joining is disabled: " +
                                        column.getPath()};
        }

        return std::format("{}.{}_{}", context.queryData.modelInfo.tableName, relatedColumnInfo.name,
                           foreignColumnInfo.name);
    }

    throw std::invalid_argument{"Only one level of related model paths is supported: " + column.getPath()};
}

auto comparisonOperatorToSql(query::ComparisonOperator comparisonOperator) -> std::string_view
{
    switch (comparisonOperator)
    {
    case query::ComparisonOperator::Equal:
        return "=";
    case query::ComparisonOperator::NotEqual:
        return "!=";
    case query::ComparisonOperator::Greater:
        return ">";
    case query::ComparisonOperator::GreaterOrEqual:
        return ">=";
    case query::ComparisonOperator::Less:
        return "<";
    case query::ComparisonOperator::LessOrEqual:
        return "<=";
    case query::ComparisonOperator::Like:
        return "LIKE";
    case query::ComparisonOperator::NotLike:
        return "NOT LIKE";
    }

    throw std::invalid_argument{"Unsupported comparison operator"};
}

auto addAutomaticParameter(DefaultSelectCommand::RenderContext& context, const query::QueryValue& value) -> std::string
{
    std::string parameterName;

    do
    {
        parameterName = std::format("orm_p{}", context.nextParameterIndex++);
    } while (context.parameterNames.contains(parameterName));

    context.parameterNames.insert(parameterName);
    context.parameters.push_back(query::QueryParameter{.name = parameterName, .value = value});

    return ":" + parameterName;
}

auto addRawParameters(DefaultSelectCommand::RenderContext& context,
                      const std::vector<query::QueryParameter>& parameters) -> void
{
    for (const auto& parameter : parameters)
    {
        if (parameter.name.empty() or parameter.name.front() == ':')
        {
            throw std::invalid_argument{"Raw query parameter name must not be empty or include ':'"};
        }

        if (startsWith(parameter.name, "orm_p"))
        {
            throw std::invalid_argument{"Raw query parameter cannot use reserved prefix orm_p: " + parameter.name};
        }

        if (not context.parameterNames.insert(parameter.name).second)
        {
            throw std::invalid_argument{"Duplicate query parameter: " + parameter.name};
        }

        context.parameters.push_back(parameter);
    }
}

auto renderPredicate(const query::PredicateNode& node, DefaultSelectCommand::RenderContext& context) -> std::string;

auto renderPredicate(const query::PredicateNodePtr& node, DefaultSelectCommand::RenderContext& context) -> std::string
{
    return renderPredicate(*node, context);
}

auto renderPredicate(const query::PredicateNode& node, DefaultSelectCommand::RenderContext& context) -> std::string
{
    return std::visit(
        Overloaded{[&context](const query::ComparisonExpression& expression)
                   {
                       const auto column = renderColumn(expression.column, context);
                       const auto sqlOperator = comparisonOperatorToSql(expression.comparisonOperator);
                       const auto parameter = addAutomaticParameter(context, expression.value);

                       return std::format("{} {} {}", column, sqlOperator, parameter);
                   },
                   [&context](const query::NullExpression& expression)
                   {
                       return std::format("{} {}", renderColumn(expression.column, context),
                                          expression.nullOperator == query::NullOperator::IsNull ? "IS NULL" :
                                                                                                   "IS NOT NULL");
                   },
                   [&context](const query::ListExpression& expression)
                   {
                       const auto column = renderColumn(expression.column, context);
                       const auto sqlOperator = expression.listOperator == query::ListOperator::In ? "IN" : "NOT IN";
                       std::vector<std::string> placeholders;
                       placeholders.reserve(expression.values.size());

                       for (const auto& value : expression.values)
                       {
                           placeholders.push_back(addAutomaticParameter(context, value));
                       }

                       return std::format("{} {} ({})", column, sqlOperator, join(placeholders, ", "));
                   },
                   [&context](const query::BetweenExpression& expression)
                   {
                       const auto column = renderColumn(expression.column, context);
                       const auto sqlOperator = expression.betweenOperator == query::BetweenOperator::Between ?
                                                    "BETWEEN" :
                                                    "NOT BETWEEN";
                       const auto lowerParameter = addAutomaticParameter(context, expression.lowerValue);
                       const auto upperParameter = addAutomaticParameter(context, expression.upperValue);

                       return std::format("{} {} {} AND {}", column, sqlOperator, lowerParameter, upperParameter);
                   },
                   [&context](const query::LogicalExpression& expression)
                   {
                       const auto left = renderPredicate(expression.left, context);
                       const auto sqlOperator = expression.logicalOperator == query::LogicalOperator::And ? "AND" : "OR";
                       const auto right = renderPredicate(expression.right, context);

                       return std::format("({} {} {})", left, sqlOperator, right);
                   },
                   [&context](const query::NotExpression& expression)
                   { return std::format("(NOT ({}))", renderPredicate(expression.predicate, context)); },
                   [&context](const query::RawExpression& expression)
                   {
                       addRawParameters(context, expression.parameters);
                       return expression.sql;
                   }},
        node.expression);
}
} // namespace

auto DefaultSelectCommand::select(const query::QueryData& queryData) const -> SelectStatement
{
    RenderContext context{.queryData = queryData};
    const auto selectFields = getSelectFields(queryData.shouldJoin, queryData.modelInfo);
    const auto joins = getJoins(queryData.shouldJoin, queryData.modelInfo);
    const auto where = getWhere(queryData.predicate, context);
    const auto orderBy = getOrderBy(queryData, context);
    const auto limit = getLimit(queryData.limit);
    const auto offset = getOffset(queryData.offset);

    const auto sql = std::format("{} {} FROM {}{}{}{}{}{};", queryData.isDistinct ? "SELECT DISTINCT" : "SELECT",
                                 selectFields, queryData.modelInfo.tableName, joins, where, orderBy, limit, offset);

    return SelectStatement{.sql = sql, .parameters = std::move(context.parameters)};
}

auto DefaultSelectCommand::getSelectFields(bool shouldJoin, const model::ModelInfo& modelInfo) -> std::string
{
    std::string selectFields;

    for (const auto& columnInfo : modelInfo.columnsInfo)
    {
        if (columnInfo.isForeignModel)
        {
            selectFields += getForeignModelSelectFields(shouldJoin, columnInfo.name,
                                                        modelInfo.foreignModelsInfo.at(columnInfo.name), modelInfo);
        }
        else
        {
            selectFields += std::format("{0:}.{1:} AS {0:}_{1:}, ", modelInfo.tableName, columnInfo.name);
        }
    }

    removeLastComma(selectFields);

    return selectFields;
}

auto DefaultSelectCommand::getForeignModelSelectFields(bool shouldJoin, const std::string& foreignModelFieldName,
                                                       const model::ModelInfo& foreignModelInfo,
                                                       const model::ModelInfo& modelInfo) -> std::string
{

    std::string selectFields;
    if (shouldJoin)
    {
        for (const auto& columnInfo : foreignModelInfo.columnsInfo)
        {
            selectFields += std::format("{0:}.{1:} AS {0:}_{1:}, ", foreignModelFieldName, columnInfo.name);
        }
    }
    else
    {
        for (const auto& columnInfo : foreignModelInfo.columnsInfo)
        {
            if (columnInfo.isPrimaryKey)
            {
                selectFields += std::format("{2:}.{0:}_{1:} AS {2:}_{0:}_{1:}, ", foreignModelFieldName,
                                            columnInfo.name, modelInfo.tableName);
            }
        }
    }

    return selectFields;
}

auto DefaultSelectCommand::getJoins(bool shouldJoin, const model::ModelInfo& modelInfo) -> std::string
{
    if (not shouldJoin)
    {
        return {};
    }

    std::string joins;

    for (const auto& columnInfo : modelInfo.columnsInfo)
    {
        if (not columnInfo.isForeignModel)
        {
            continue;
        }

        const auto& foreignModelInfo = modelInfo.foreignModelsInfo.at(columnInfo.name);
        std::vector<std::string> joinPredicates;

        for (const auto& foreignColumnInfo : foreignModelInfo.columnsInfo)
        {
            if (foreignColumnInfo.isPrimaryKey)
            {
                joinPredicates.push_back(std::format("{0:}.{1:} = {2:}.{3:}_{1:}", columnInfo.name,
                                                     foreignColumnInfo.name, modelInfo.tableName, columnInfo.name));
            }
        }

        joins += std::format(" LEFT JOIN {0:} AS {1:} ON {2:} ", foreignModelInfo.tableName, columnInfo.name,
                             join(joinPredicates, " AND "));
    }

    if (not joins.empty())
    {
        joins.pop_back();
    }

    return joins;
}

auto DefaultSelectCommand::getOffset(const std::optional<std::size_t>& offset) -> std::string
{
    if (offset.has_value())
    {
        return std::format(" OFFSET {}", offset.value());
    }

    return {};
}

auto DefaultSelectCommand::getLimit(const std::optional<std::size_t>& limit) -> std::string
{
    if (limit.has_value())
    {
        return std::format(" LIMIT {}", limit.value());
    }

    return {};
}

auto DefaultSelectCommand::getWhere(const std::optional<query::Predicate>& predicate, RenderContext& context)
    -> std::string
{
    if (not predicate.has_value())
    {
        return {};
    }

    return " WHERE " + renderPredicate(predicate->getNode(), context);
}

auto DefaultSelectCommand::getOrderBy(const query::QueryData& queryData, RenderContext& context) -> std::string
{
    if (queryData.orderBy.empty())
    {
        return {};
    }

    std::vector<std::string> orderByClauses;
    orderByClauses.reserve(queryData.orderBy.size());

    for (const auto& orderBy : queryData.orderBy)
    {
        if (orderBy.isRaw)
        {
            orderByClauses.push_back(orderBy.rawSql);
            continue;
        }

        orderByClauses.push_back(std::format("{} {}", renderColumn(orderBy.column, context),
                                             orderBy.direction == query::OrderDirection::Asc ? "ASC" : "DESC"));
    }

    return " ORDER BY " + join(orderByClauses, ", ");
}
} // namespace orm::db::commands

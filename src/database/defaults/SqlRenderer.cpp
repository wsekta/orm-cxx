#include "SqlRenderer.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "SqlAliases.hpp"

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

auto uniqueAlias(std::string_view base, const std::unordered_set<std::string>& reserved) -> std::string
{
    auto candidate = std::string{base};
    std::size_t suffix = 1;

    while (reserved.contains(candidate))
    {
        candidate = std::format("{}_{}", base, suffix++);
    }

    return candidate;
}

auto findColumn(const orm::model::ModelView model, std::string_view fieldOrColumnName) -> const orm::model::ColumnView*
{
    return model.findColumn(fieldOrColumnName);
}

auto getColumnOrThrow(const orm::model::ModelView model,
                      std::string_view fieldOrColumnName) -> const orm::model::ColumnView&
{
    const auto* column = findColumn(model, fieldOrColumnName);

    if (column == nullptr)
    {
        throw std::invalid_argument{"Unknown column path segment: " + std::string{fieldOrColumnName}};
    }

    return *column;
}

auto getTargetOrThrow(const orm::model::ModelView model, const orm::model::ColumnView& column) -> orm::model::ModelView
{
    if (column.kind != orm::model::FieldKind::ToOne)
    {
        throw std::invalid_argument{"Column is not a related model: " + std::string{column.fieldName}};
    }

    const auto target = model.resolveTarget(column);
    if (target == nullptr)
    {
        throw std::logic_error{"To-one relation target is not available in the schema"};
    }
    return target;
}

auto renderSelectColumn(const orm::query::Column& column,
                        const orm::db::commands::RenderContext& context) -> std::string
{
    const auto parts = splitPath(column.getPath());
    const auto rootTable = context.tableAlias.empty() ? std::string{context.model->tableName} : context.tableAlias;

    if (parts.size() == 1)
    {
        const auto& columnView = getColumnOrThrow(context.model, parts[0]);

        if (columnView.kind == orm::model::FieldKind::ToOne)
        {
            throw std::invalid_argument{"Use a related field path instead of the related model itself: " +
                                        column.getPath()};
        }

        return orm::db::aliases::qualifiedIdentifier(context.dialect, rootTable, columnView.name);
    }

    if (parts.size() == 2)
    {
        const auto& relatedColumn = getColumnOrThrow(context.model, parts[0]);
        const auto target = getTargetOrThrow(context.model, relatedColumn);
        const auto& targetColumn = getColumnOrThrow(target, parts[1]);

        if (context.shouldJoin)
        {
            return orm::db::aliases::qualifiedIdentifier(context.dialect, relatedColumn.name, targetColumn.name);
        }

        if (not targetColumn.isPrimaryKey)
        {
            throw std::invalid_argument{"Cannot filter by non-id related field when joining is disabled: " +
                                        column.getPath()};
        }

        return orm::db::aliases::qualifiedIdentifier(
            context.dialect, rootTable, orm::db::aliases::joinedRelationColumn(relatedColumn.name, targetColumn.name));
    }

    throw std::invalid_argument{"Only one level of related model paths is supported: " + column.getPath()};
}

auto comparisonOperatorToSql(orm::query::ComparisonOperator comparisonOperator) -> std::string_view
{
    switch (comparisonOperator)
    {
    case orm::query::ComparisonOperator::Equal:
        return "=";
    case orm::query::ComparisonOperator::NotEqual:
        return "!=";
    case orm::query::ComparisonOperator::Greater:
        return ">";
    case orm::query::ComparisonOperator::GreaterOrEqual:
        return ">=";
    case orm::query::ComparisonOperator::Less:
        return "<";
    case orm::query::ComparisonOperator::LessOrEqual:
        return "<=";
    case orm::query::ComparisonOperator::Like:
        return "LIKE";
    case orm::query::ComparisonOperator::NotLike:
        return "NOT LIKE";
    }

    throw std::invalid_argument{"Unsupported comparison operator"};
}

auto addRawParameters(orm::db::commands::RenderContext& context,
                      const std::vector<orm::query::QueryParameter>& parameters) -> void
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

        (void)context.dialect.bindMarker(parameter.name);

        if (not context.parameterNames.insert(parameter.name).second)
        {
            throw std::invalid_argument{"Duplicate query parameter: " + parameter.name};
        }

        context.parameters.push_back(orm::db::StatementParameter{.name = parameter.name, .value = parameter.value, .nullType = std::nullopt});
    }
}

auto renderPredicate(const orm::query::PredicateNode& node, orm::db::commands::RenderContext& context) -> std::string;
auto renderPredicate(const orm::query::PredicateNodePtr& node,
                     orm::db::commands::RenderContext& context) -> std::string;

auto primaryKeyColumns(orm::model::ModelView model) -> std::vector<const orm::model::ColumnView*>
{
    std::vector<const orm::model::ColumnView*> columns;

    for (const auto& column : model->columns)
    {
        if (column.isPrimaryKey)
        {
            if (column.kind == orm::model::FieldKind::ToOne)
            {
                throw std::invalid_argument{"Relations with model-valued primary-key fields are not supported"};
            }

            columns.push_back(&column);
        }
    }

    if (columns.empty())
    {
        throw std::invalid_argument{"Collection relation endpoint must define a primary key"};
    }

    return columns;
}

auto renderToOneJoins(orm::model::ModelView model, const orm::db::SqlDialect& dialect,
                      const std::string& rootAlias) -> std::string
{
    std::string joins;

    for (const auto& relation : model->columns)
    {
        if (relation.kind != orm::model::FieldKind::ToOne)
        {
            continue;
        }

        const auto target = getTargetOrThrow(model, relation);
        std::vector<std::string> predicates;

        for (const auto* targetColumn : primaryKeyColumns(target))
        {
            predicates.push_back(std::format(
                "{} = {}", orm::db::aliases::qualifiedIdentifier(dialect, relation.name, targetColumn->name),
                orm::db::aliases::qualifiedIdentifier(
                    dialect, rootAlias, orm::db::aliases::joinedRelationColumn(relation.name, targetColumn->name))));
        }

        joins += std::format(" LEFT JOIN {} AS {} ON {}", dialect.quoteIdentifier(target->tableName),
                             dialect.quoteIdentifier(relation.name), join(predicates, " AND "));
    }

    return joins;
}

auto renderNestedPredicate(const orm::query::PredicateNodePtr& predicate, orm::model::ModelView target,
                           orm::db::commands::RenderContext& outerContext,
                           const std::string& targetAlias) -> std::string
{
    orm::db::commands::RenderContext targetContext{
        .model = target,
        .dialect = outerContext.dialect,
        .shouldJoin = true,
        .columnRenderMode = orm::db::commands::ColumnRenderMode::Select,
        .tableAlias = targetAlias,
        .allowCollectionPredicates = false,
        .parameters = std::move(outerContext.parameters),
        .parameterNames = std::move(outerContext.parameterNames),
        .nextParameterIndex = outerContext.nextParameterIndex,
    };
    const auto sql = renderPredicate(predicate, targetContext);
    outerContext.parameters = std::move(targetContext.parameters);
    outerContext.parameterNames = std::move(targetContext.parameterNames);
    outerContext.nextParameterIndex = targetContext.nextParameterIndex;

    return sql;
}

auto renderCollectionPredicate(const orm::query::CollectionExpression& expression,
                               orm::db::commands::RenderContext& context) -> std::string
{
    if (not context.allowCollectionPredicates)
    {
        throw std::invalid_argument{"Nested collection predicates are not supported"};
    }

    const auto* relation = context.model.findRelation(expression.relation);

    if (relation == nullptr or relation->kind == orm::model::RelationKind::ToOne)
    {
        throw std::invalid_argument{"Unknown collection relation: " + expression.relation};
    }

    if (expression.collectionOperator != orm::query::CollectionOperator::Exists and expression.predicate == nullptr)
    {
        throw std::invalid_argument{"Collection any/none requires an element predicate"};
    }

    const auto outerAlias = context.tableAlias.empty() ? std::string{context.model->tableName} : context.tableAlias;
    const auto ownerPrimaryKey = primaryKeyColumns(context.model);
    const auto target = context.model.resolveTarget(*relation);
    if (target == nullptr)
    {
        throw std::logic_error{"Collection relation target is not available in the schema"};
    }
    const auto targetPrimaryKey = primaryKeyColumns(target);
    auto reservedAliases = std::unordered_set<std::string>{outerAlias};

    for (const auto& targetRelation : target->columns)
    {
        if (targetRelation.kind == orm::model::FieldKind::ToOne)
        {
            reservedAliases.emplace(targetRelation.name);
        }
    }

    const auto targetAlias = uniqueAlias("orm_relation_target", reservedAliases);
    reservedAliases.insert(targetAlias);
    const auto junctionAlias = uniqueAlias("orm_relation_junction", reservedAliases);
    std::vector<std::string> predicates;
    std::string from;

    if (relation->kind == orm::model::RelationKind::OneToMany)
    {
        const auto* mappedRelation = target.findRelation(relation->mappedBy);

        if (mappedRelation == nullptr or mappedRelation->kind != orm::model::RelationKind::ToOne)
        {
            throw std::invalid_argument{"Invalid OneToMany mappedBy relation: " + std::string{relation->fieldName}};
        }

        for (const auto* ownerColumn : ownerPrimaryKey)
        {
            predicates.push_back(
                std::format("{} = {}",
                            orm::db::aliases::qualifiedIdentifier(
                                context.dialect, targetAlias,
                                orm::db::aliases::joinedRelationColumn(mappedRelation->columnName, ownerColumn->name)),
                            orm::db::aliases::qualifiedIdentifier(context.dialect, outerAlias, ownerColumn->name)));
        }

        from = std::format("{} AS {}{}", context.dialect.quoteIdentifier(target->tableName),
                           context.dialect.quoteIdentifier(targetAlias),
                           renderToOneJoins(target, context.dialect, targetAlias));
    }
    else
    {
        const auto junction = context.model.resolveJunction(*relation);
        if (not junction.isConfigured())
        {
            throw std::invalid_argument{"ManyToMany relation has no junction mapping: " +
                                        std::string{relation->fieldName}};
        }

        if (junction.ownerColumns.size() != ownerPrimaryKey.size() or
            junction.targetColumns.size() != targetPrimaryKey.size())
        {
            throw std::invalid_argument{"Junction columns do not match relation endpoint keys: " +
                                        std::string{junction.tableName}};
        }

        std::vector<std::string> targetJoin;

        for (std::size_t i = 0; i < ownerPrimaryKey.size(); ++i)
        {
            predicates.push_back(std::format(
                "{} = {}",
                orm::db::aliases::qualifiedIdentifier(context.dialect, junctionAlias, junction.ownerColumns[i]),
                orm::db::aliases::qualifiedIdentifier(context.dialect, outerAlias, ownerPrimaryKey[i]->name)));
        }

        for (std::size_t i = 0; i < targetPrimaryKey.size(); ++i)
        {
            targetJoin.push_back(std::format(
                "{} = {}",
                orm::db::aliases::qualifiedIdentifier(context.dialect, targetAlias, targetPrimaryKey[i]->name),
                orm::db::aliases::qualifiedIdentifier(context.dialect, junctionAlias, junction.targetColumns[i])));
        }

        from = std::format("{} AS {} JOIN {} AS {} ON {}{}", context.dialect.quoteIdentifier(junction.tableName),
                           context.dialect.quoteIdentifier(junctionAlias),
                           context.dialect.quoteIdentifier(target->tableName),
                           context.dialect.quoteIdentifier(targetAlias), join(targetJoin, " AND "),
                           renderToOneJoins(target, context.dialect, targetAlias));
    }

    if (expression.predicate != nullptr)
    {
        predicates.push_back(renderNestedPredicate(expression.predicate, target, context, targetAlias));
    }

    const auto existsSql = std::format("EXISTS (SELECT 1 FROM {} WHERE {})", from, join(predicates, " AND "));

    return expression.collectionOperator == orm::query::CollectionOperator::None ? std::format("NOT ({})", existsSql) :
                                                                                   existsSql;
}

auto renderPredicate(const orm::query::PredicateNodePtr& node, orm::db::commands::RenderContext& context) -> std::string
{
    return renderPredicate(*node, context);
}

auto renderPredicate(const orm::query::PredicateNode& node, orm::db::commands::RenderContext& context) -> std::string
{
    return std::visit(
        Overloaded{
            [&context](const orm::query::ComparisonExpression& expression)
            {
                const auto column = orm::db::commands::renderColumn(expression.column, context);
                const auto sqlOperator = comparisonOperatorToSql(expression.comparisonOperator);
                const auto parameter = orm::db::commands::addAutomaticParameter(context, expression.value);

                return std::format("{} {} {}", column, sqlOperator, parameter);
            },
            [&context](const orm::query::NullExpression& expression)
            {
                return std::format("{} {}", orm::db::commands::renderColumn(expression.column, context),
                                   expression.nullOperator == orm::query::NullOperator::IsNull ? "IS NULL" :
                                                                                                 "IS NOT NULL");
            },
            [&context](const orm::query::ListExpression& expression)
            {
                if (expression.values.empty())
                {
                    return expression.listOperator == orm::query::ListOperator::In ? std::string{"(1 = 0)"} :
                                                                                     std::string{"(1 = 1)"};
                }

                const auto column = orm::db::commands::renderColumn(expression.column, context);
                const auto sqlOperator = expression.listOperator == orm::query::ListOperator::In ? "IN" : "NOT IN";
                std::vector<std::string> placeholders;
                placeholders.reserve(expression.values.size());

                for (const auto& value : expression.values)
                {
                    placeholders.push_back(orm::db::commands::addAutomaticParameter(context, value));
                }

                return std::format("{} {} ({})", column, sqlOperator, join(placeholders, ", "));
            },
            [&context](const orm::query::BetweenExpression& expression)
            {
                const auto column = orm::db::commands::renderColumn(expression.column, context);
                const auto sqlOperator =
                    expression.betweenOperator == orm::query::BetweenOperator::Between ? "BETWEEN" : "NOT BETWEEN";
                const auto lowerParameter = orm::db::commands::addAutomaticParameter(context, expression.lowerValue);
                const auto upperParameter = orm::db::commands::addAutomaticParameter(context, expression.upperValue);

                return std::format("{} {} {} AND {}", column, sqlOperator, lowerParameter, upperParameter);
            },
            [&context](const orm::query::LogicalExpression& expression)
            {
                const auto left = renderPredicate(expression.left, context);
                const auto sqlOperator = expression.logicalOperator == orm::query::LogicalOperator::And ? "AND" : "OR";
                const auto right = renderPredicate(expression.right, context);

                return std::format("({} {} {})", left, sqlOperator, right);
            },
            [&context](const orm::query::NotExpression& expression)
            { return std::format("(NOT ({}))", renderPredicate(expression.predicate, context)); },
            [&context](const orm::query::RawExpression& expression)
            {
                addRawParameters(context, expression.parameters);
                return expression.sql;
            },
            [&context](const orm::query::CollectionExpression& expression)
            { return renderCollectionPredicate(expression, context); }},
        node.expression);
}
} // namespace

namespace orm::db::commands
{
auto renderColumn(const query::Column& column, const RenderContext& context) -> std::string
{
    if (context.columnRenderMode == ColumnRenderMode::WritePredicate)
    {
        return renderWriteColumn(column, context.model, context.dialect, true).sql;
    }

    return renderSelectColumn(column, context);
}

auto renderWriteColumn(const query::Column& column, model::ModelView modelView, const SqlDialect& dialect,
                       bool qualifyWithTable) -> WriteColumn
{
    const auto parts = splitPath(column.getPath());

    if (parts.size() == 1)
    {
        const auto& columnView = getColumnOrThrow(modelView, parts[0]);

        if (columnView.kind == model::FieldKind::ToOne)
        {
            throw std::invalid_argument{"Use a related primary-key field path in write queries: " + column.getPath()};
        }

        return WriteColumn{.sql = qualifyWithTable ?
                                      aliases::qualifiedIdentifier(dialect, modelView->tableName, columnView.name) :
                                      dialect.quoteIdentifier(columnView.name),
                           .type = columnView.type.value(),
                           .isNotNull = columnView.isNotNull};
    }

    if (parts.size() == 2)
    {
        const auto& relatedColumn = getColumnOrThrow(modelView, parts[0]);
        const auto target = getTargetOrThrow(modelView, relatedColumn);
        const auto& targetColumn = getColumnOrThrow(target, parts[1]);

        if (not targetColumn.isPrimaryKey)
        {
            throw std::invalid_argument{"Only related primary-key fields can be used in write queries: " +
                                        column.getPath()};
        }

        const auto columnName = std::format("{}_{}", relatedColumn.name, targetColumn.name);

        return WriteColumn{.sql = qualifyWithTable ?
                                      aliases::qualifiedIdentifier(dialect, modelView->tableName, columnName) :
                                      dialect.quoteIdentifier(columnName),
                           .type = targetColumn.type.value(),
                           .isNotNull = relatedColumn.isNotNull};
    }

    throw std::invalid_argument{"Only one level of related model paths is supported: " + column.getPath()};
}

auto renderWhere(const std::optional<query::Predicate>& predicate, RenderContext& context) -> std::string
{
    if (not predicate.has_value())
    {
        return {};
    }

    return " WHERE " + renderPredicate(predicate->getNode(), context);
}

auto renderWhere(const query::Predicate& predicate, RenderContext& context) -> std::string
{
    return " WHERE " + renderPredicate(predicate.getNode(), context);
}

auto addAutomaticParameter(RenderContext& context, const query::QueryValue& value) -> std::string
{
    std::string parameterName;

    do
    {
        parameterName = std::format("orm_p{}", context.nextParameterIndex++);
    } while (context.parameterNames.contains(parameterName));

    context.parameterNames.insert(parameterName);
    context.parameters.push_back(StatementParameter{.name = parameterName, .value = value, .nullType = std::nullopt});

    return context.dialect.bindMarker(parameterName);
}

auto addNullParameter(RenderContext& context, model::ColumnType type) -> std::string
{
    std::string parameterName;

    do
    {
        parameterName = std::format("orm_p{}", context.nextParameterIndex++);
    } while (context.parameterNames.contains(parameterName));

    context.parameterNames.insert(parameterName);
    context.parameters.push_back(StatementParameter{.name = parameterName, .value = std::nullopt, .nullType = type});

    return context.dialect.bindMarker(parameterName);
}
} // namespace orm::db::commands

#include "SqlRenderer.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>

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

auto renderSelectColumn(const orm::query::Column& column, const orm::db::commands::RenderContext& context)
    -> std::string
{
    const auto parts = splitPath(column.getPath());
    const auto rootTable = context.tableAlias.empty() ? std::string{context.modelInfo.tableName} : context.tableAlias;

    if (parts.size() == 1)
    {
        const auto& columnInfo = getColumnInfoOrThrow(context.modelInfo, parts[0]);

        if (columnInfo.isForeignModel)
        {
            throw std::invalid_argument{"Use a related field path instead of the related model itself: " +
                                        column.getPath()};
        }

        return std::format("{}.{}", rootTable, columnInfo.name);
    }

    if (parts.size() == 2)
    {
        const auto& relatedColumnInfo = getColumnInfoOrThrow(context.modelInfo, parts[0]);
        const auto& foreignModelInfo = getForeignModelInfoOrThrow(context.modelInfo, relatedColumnInfo);
        const auto& foreignColumnInfo = getColumnInfoOrThrow(foreignModelInfo, parts[1]);

        if (context.shouldJoin)
        {
            return std::format("{}.{}", relatedColumnInfo.name, foreignColumnInfo.name);
        }

        if (not foreignColumnInfo.isPrimaryKey)
        {
            throw std::invalid_argument{"Cannot filter by non-id related field when joining is disabled: " +
                                        column.getPath()};
        }

        return std::format("{}.{}_{}", rootTable, relatedColumnInfo.name, foreignColumnInfo.name);
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

        if (not context.parameterNames.insert(parameter.name).second)
        {
            throw std::invalid_argument{"Duplicate query parameter: " + parameter.name};
        }

        context.parameters.push_back(orm::db::StatementParameter{.name = parameter.name, .value = parameter.value});
    }
}

auto renderPredicate(const orm::query::PredicateNode& node, orm::db::commands::RenderContext& context) -> std::string;
auto renderPredicate(const orm::query::PredicateNodePtr& node, orm::db::commands::RenderContext& context)
    -> std::string;

auto primaryKeyColumns(const orm::model::ModelInfo& modelInfo) -> std::vector<const orm::model::ColumnInfo*>
{
    std::vector<const orm::model::ColumnInfo*> columns;

    for (const auto& column : modelInfo.columnsInfo)
    {
        if (column.isPrimaryKey)
        {
            if (column.isForeignModel)
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

auto renderToOneJoins(const orm::model::ModelInfo& modelInfo, const std::string& rootAlias) -> std::string
{
    std::string joins;

    for (const auto& relation : modelInfo.relationsInfo)
    {
        if (relation.kind != orm::model::RelationKind::ToOne)
        {
            continue;
        }

        const auto& targetInfo = relation.targetModel();
        std::vector<std::string> predicates;

        for (const auto* targetColumn : primaryKeyColumns(targetInfo))
        {
            predicates.push_back(std::format("{}.{} = {}.{}_{}", relation.columnName, targetColumn->name,
                                             rootAlias, relation.columnName, targetColumn->name));
        }

        joins += std::format(" LEFT JOIN {} AS {} ON {}", targetInfo.tableName, relation.columnName,
                             join(predicates, " AND "));
    }

    return joins;
}

auto renderNestedPredicate(const orm::query::PredicateNodePtr& predicate,
                           const orm::model::ModelInfo& targetInfo,
                           orm::db::commands::RenderContext& outerContext, const std::string& targetAlias)
    -> std::string
{
    orm::db::commands::RenderContext targetContext{
        .modelInfo = targetInfo,
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

    const auto* relation = context.modelInfo.findRelation(expression.relation);

    if (relation == nullptr or relation->kind == orm::model::RelationKind::ToOne)
    {
        throw std::invalid_argument{"Unknown collection relation: " + expression.relation};
    }

    if (expression.collectionOperator != orm::query::CollectionOperator::Exists and expression.predicate == nullptr)
    {
        throw std::invalid_argument{"Collection any/none requires an element predicate"};
    }

    const auto outerAlias =
        context.tableAlias.empty() ? std::string{context.modelInfo.tableName} : context.tableAlias;
    const auto targetAlias = std::string{"orm_relation_target"};
    const auto junctionAlias = std::string{"orm_relation_junction"};
    const auto ownerPrimaryKey = primaryKeyColumns(context.modelInfo);
    const auto& targetInfo = relation->targetModel();
    const auto targetPrimaryKey = primaryKeyColumns(targetInfo);
    std::vector<std::string> predicates;
    std::string from;

    if (relation->kind == orm::model::RelationKind::OneToMany)
    {
        const auto* mappedRelation = targetInfo.findRelation(relation->mappedBy);

        if (mappedRelation == nullptr or mappedRelation->kind != orm::model::RelationKind::ToOne)
        {
            throw std::invalid_argument{"Invalid OneToMany mappedBy relation: " + relation->fieldName};
        }

        for (const auto* ownerColumn : ownerPrimaryKey)
        {
            predicates.push_back(std::format("{}.{}_{} = {}.{}", targetAlias, mappedRelation->columnName,
                                             ownerColumn->name, outerAlias, ownerColumn->name));
        }

        from = std::format("{} AS {}{}", targetInfo.tableName, targetAlias,
                           renderToOneJoins(targetInfo, targetAlias));
    }
    else
    {
        if (not relation->junction.has_value())
        {
            throw std::invalid_argument{"ManyToMany relation has no junction mapping: " + relation->fieldName};
        }

        const auto& junction = relation->junction.value();

        if (junction.ownerColumns.size() != ownerPrimaryKey.size() or
            junction.targetColumns.size() != targetPrimaryKey.size())
        {
            throw std::invalid_argument{"Junction columns do not match relation endpoint keys: " +
                                        junction.tableName};
        }

        std::vector<std::string> targetJoin;

        for (std::size_t i = 0; i < ownerPrimaryKey.size(); ++i)
        {
            predicates.push_back(std::format("{}.{} = {}.{}", junctionAlias, junction.ownerColumns[i],
                                             outerAlias, ownerPrimaryKey[i]->name));
        }

        for (std::size_t i = 0; i < targetPrimaryKey.size(); ++i)
        {
            targetJoin.push_back(std::format("{}.{} = {}.{}", targetAlias, targetPrimaryKey[i]->name,
                                             junctionAlias, junction.targetColumns[i]));
        }

        from = std::format("{} AS {} JOIN {} AS {} ON {}{}", junction.tableName, junctionAlias,
                           targetInfo.tableName, targetAlias, join(targetJoin, " AND "),
                           renderToOneJoins(targetInfo, targetAlias));
    }

    if (expression.predicate != nullptr)
    {
        predicates.push_back(renderNestedPredicate(expression.predicate, targetInfo, context, targetAlias));
    }

    const auto existsSql = std::format("EXISTS (SELECT 1 FROM {} WHERE {})", from, join(predicates, " AND "));

    return expression.collectionOperator == orm::query::CollectionOperator::None ?
               std::format("NOT ({})", existsSql) :
               existsSql;
}

auto renderPredicate(const orm::query::PredicateNodePtr& node, orm::db::commands::RenderContext& context) -> std::string
{
    return renderPredicate(*node, context);
}

auto renderPredicate(const orm::query::PredicateNode& node, orm::db::commands::RenderContext& context) -> std::string
{
    return std::visit(
        Overloaded{[&context](const orm::query::ComparisonExpression& expression)
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
                       const auto column = orm::db::commands::renderColumn(expression.column, context);
                       const auto sqlOperator = expression.listOperator == orm::query::ListOperator::In ? "IN" :
                                                                                                          "NOT IN";
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
                       const auto sqlOperator = expression.betweenOperator == orm::query::BetweenOperator::Between ?
                                                    "BETWEEN" :
                                                    "NOT BETWEEN";
                       const auto lowerParameter =
                           orm::db::commands::addAutomaticParameter(context, expression.lowerValue);
                       const auto upperParameter =
                           orm::db::commands::addAutomaticParameter(context, expression.upperValue);

                       return std::format("{} {} {} AND {}", column, sqlOperator, lowerParameter, upperParameter);
                   },
                   [&context](const orm::query::LogicalExpression& expression)
                   {
                       const auto left = renderPredicate(expression.left, context);
                       const auto sqlOperator =
                           expression.logicalOperator == orm::query::LogicalOperator::And ? "AND" : "OR";
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
        return renderWriteColumn(column, context.modelInfo, true).sql;
    }

    return renderSelectColumn(column, context);
}

auto renderWriteColumn(const query::Column& column, const model::ModelInfo& modelInfo, bool qualifyWithTable)
    -> WriteColumn
{
    const auto parts = splitPath(column.getPath());

    if (parts.size() == 1)
    {
        const auto& columnInfo = getColumnInfoOrThrow(modelInfo, parts[0]);

        if (columnInfo.isForeignModel)
        {
            throw std::invalid_argument{"Use a related primary-key field path in write queries: " + column.getPath()};
        }

        return WriteColumn{.sql = qualifyWithTable ? std::format("{}.{}", modelInfo.tableName, columnInfo.name) :
                                                     columnInfo.name,
                           .type = columnInfo.type,
                           .isNotNull = columnInfo.isNotNull};
    }

    if (parts.size() == 2)
    {
        const auto& relatedColumnInfo = getColumnInfoOrThrow(modelInfo, parts[0]);
        const auto& foreignModelInfo = getForeignModelInfoOrThrow(modelInfo, relatedColumnInfo);
        const auto& foreignColumnInfo = getColumnInfoOrThrow(foreignModelInfo, parts[1]);

        if (not foreignColumnInfo.isPrimaryKey)
        {
            throw std::invalid_argument{"Only related primary-key fields can be used in write queries: " +
                                        column.getPath()};
        }

        const auto columnName = std::format("{}_{}", relatedColumnInfo.name, foreignColumnInfo.name);

        return WriteColumn{.sql = qualifyWithTable ? std::format("{}.{}", modelInfo.tableName, columnName) :
                                                     columnName,
                           .type = foreignColumnInfo.type,
                           .isNotNull = relatedColumnInfo.isNotNull};
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
    context.parameters.push_back(StatementParameter{.name = parameterName, .value = value});

    return ":" + parameterName;
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

    return ":" + parameterName;
}
} // namespace orm::db::commands

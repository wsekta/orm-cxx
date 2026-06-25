#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "orm-cxx/database/Statement.hpp"
#include "orm-cxx/model/ModelInfo.hpp"
#include "orm-cxx/query/Predicate.hpp"

namespace orm::db::commands
{
enum class ColumnRenderMode
{
    Select,
    WritePredicate,
};

struct RenderContext
{
    const model::ModelInfo& modelInfo;
    bool shouldJoin = true;
    ColumnRenderMode columnRenderMode = ColumnRenderMode::Select;
    std::vector<StatementParameter> parameters;
    std::unordered_set<std::string> parameterNames;
    std::size_t nextParameterIndex{};
};

struct WriteColumn
{
    std::string sql;
    model::ColumnType type;
    bool isNotNull;
};

[[nodiscard]] auto renderColumn(const query::Column& column, const RenderContext& context) -> std::string;
[[nodiscard]] auto renderWriteColumn(const query::Column& column, const model::ModelInfo& modelInfo,
                                     bool qualifyWithTable) -> WriteColumn;
[[nodiscard]] auto renderWhere(const std::optional<query::Predicate>& predicate, RenderContext& context) -> std::string;
[[nodiscard]] auto renderWhere(const query::Predicate& predicate, RenderContext& context) -> std::string;
[[nodiscard]] auto addAutomaticParameter(RenderContext& context, const query::QueryValue& value) -> std::string;
[[nodiscard]] auto addNullParameter(RenderContext& context, model::ColumnType type) -> std::string;
} // namespace orm::db::commands

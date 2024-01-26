#include "DefaultSelectCommand.hpp"

#include <format>

namespace orm::db::commands
{
auto DefaultSelectCommand::select(const query::QueryData& queryData) const -> std::string
{
    return std::format("SELECT {} FROM {}{}{};", 
                        getSelectFields(queryData.modelInfo), 
                        queryData.modelInfo.tableName, 
                        getOffset(queryData.offset),
                        getLimit(queryData.limit));
}

auto DefaultSelectCommand::getSelectFields(const model::ModelInfo& /*modelInfo*/) -> std::string
{
    return "*";
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
} // namespace orm::db::commands

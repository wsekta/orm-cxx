#pragma once

#include "orm-cxx/database/commands/SelectCommand.hpp"
#include "orm-cxx/database/TypeTranslator.hpp"

namespace orm::db::commands
{
class DefaultSelectCommand : public SelectCommand
{
public:
    [[nodiscard]] auto select(const query::QueryData& queryData) const -> std::string override;

private:
    static auto getSelectFields(const model::ModelInfo& modelInfo) -> std::string;
    static auto getOffset(const std::optional<std::size_t>& offset) -> std::string;
    static auto getLimit(const std::optional<std::size_t>& limit) -> std::string;
};
} // namespace orm::db::commands

#pragma once

#include "orm-cxx/database/commands/SelectCommand.hpp"
#include "orm-cxx/database/TypeTranslator.hpp"

namespace orm::db::commands
{
class DefaultSelectCommand : public SelectCommand
{
public:
    [[nodiscard]] auto select(const query::QueryData& queryData) const -> std::string override;
};
} // namespace orm::db::commands

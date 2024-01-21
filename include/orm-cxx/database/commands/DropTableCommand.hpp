#pragma once

#include "orm-cxx/model/ModelInfo.hpp"

namespace orm::db::commands
{
class DropTableCommand
{
public:
    virtual ~DropTableCommand() = default;

    [[nodiscard]] virtual auto dropTable(const model::ModelInfo& modelInfo) const -> std::string = 0;
};
}

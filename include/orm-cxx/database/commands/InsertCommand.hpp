#pragma once

#include "orm-cxx/model/ModelInfo.hpp"

namespace orm::db::commands
{
class InsertCommand
{
public:
    virtual ~InsertCommand() = default;

    [[nodiscard]] virtual auto insert(const model::ModelInfo& modelInfo) const -> std::string = 0;
};
}

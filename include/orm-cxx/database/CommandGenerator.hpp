#pragma once

#include <string>

#include "orm-cxx/model/ModelInfo.hpp"

namespace orm::db
{
class CommandGenerator
{
public:
    virtual ~CommandGenerator() = default;

    virtual auto createTable(const model::ModelInfo& modelInfo) const -> std::string = 0;
    virtual auto dropTable(const model::ModelInfo& modelInfo) const -> std::string = 0;
    virtual auto insert(const model::ModelInfo& modelInfo) const -> std::string = 0;

protected:
    inline auto removeLastComma(std::string& command) const -> void
    {
        command.pop_back();
        command.pop_back();
    }
};
}
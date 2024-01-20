#pragma once

#include <string>

#include "orm-cxx/model/ModelInfo.hpp"
#include "orm-cxx/query/QueryData.hpp"

namespace orm::db
{
class CommandGenerator
{
public:
    virtual ~CommandGenerator() = default;

    [[nodiscard]] virtual auto createTable(const model::ModelInfo& modelInfo) const -> std::string = 0;
    [[nodiscard]] virtual auto dropTable(const model::ModelInfo& modelInfo) const -> std::string = 0;
    [[nodiscard]] virtual auto insert(const model::ModelInfo& modelInfo) const -> std::string = 0;
    [[nodiscard]] virtual auto select(const query::QueryData& queryData) const -> std::string = 0;

protected:
    static auto removeLastComma(std::string& command) -> void;
};
} // namespace orm::db

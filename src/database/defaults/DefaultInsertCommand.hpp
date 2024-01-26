#pragma once

#include "orm-cxx/database/commands/InsertCommand.hpp"

namespace orm::db::commands
{
class DefaultInsertCommand : public InsertCommand
{
public:
    [[nodiscard]] auto insert(const model::ModelInfo& modelInfo) const -> std::string override;
private:
    static auto getInsertFields(const std::vector<std::string>& fieldNames) -> std::string;
    static auto getInsertValues(const std::vector<std::string>& fieldNames) -> std::string;
    static auto getFieldsNames(const model::ModelInfo& modelInfo) -> std::vector<std::string>;
    static auto getForeginModelIdsNames(const std::string& foreginModelFieldName, const model::ModelInfo& modelInfo) -> std::vector<std::string>;
};
} // namespace orm::db::commands

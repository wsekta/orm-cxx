#pragma once

#include "orm-cxx/database/commands/InsertCommand.hpp"
#include "orm-cxx/database/SqlDialect.hpp"

namespace orm::db::commands
{
class DefaultInsertCommand : public InsertCommand
{
public:
    explicit DefaultInsertCommand(const SqlDialect& dialect);

    [[nodiscard]] auto insert(const model::ModelInfo& modelInfo) const -> std::string override;

private:
    const SqlDialect& dialect;

    auto getInsertFields(const std::vector<std::string>& fieldNames) const -> std::string;
    auto getInsertValues(const std::vector<std::string>& fieldNames) const -> std::string;
    static auto getFieldsNames(const model::ModelInfo& modelInfo) -> std::vector<std::string>;
    static auto getForeginModelIdsNames(const std::string& foreginModelFieldName,
                                        const model::ModelInfo& modelInfo) -> std::vector<std::string>;
};
} // namespace orm::db::commands

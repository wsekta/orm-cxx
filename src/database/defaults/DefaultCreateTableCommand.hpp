#pragma once

#include <memory>

#include "orm-cxx/database/commands/CreateTableCommand.hpp"
#include "orm-cxx/database/TypeTranslator.hpp"

namespace orm::db::commands
{
class DefaultCreateTableCommand : public CreateTableCommand
{
public:
    DefaultCreateTableCommand(std::shared_ptr<TypeTranslator> typeTranslator);

    [[nodiscard]] auto createTable(const model::ModelInfo& modelInfo) const -> std::string override;

private:
    std::shared_ptr<TypeTranslator> typeTranslator;

    [[nodiscard]] auto addColumnsForForeignIds(const model::ModelInfo& modelInfo,
                                               const model::ColumnInfo& columnInfo) const -> std::string;
    [[nodiscard]] static auto addForeignIds(const model::ModelInfo& modelInfo) -> std::string;
};
} // namespace orm::db::commands

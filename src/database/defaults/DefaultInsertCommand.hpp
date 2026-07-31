#pragma once

#include <string_view>

#include "orm-cxx/database/commands/InsertCommand.hpp"
#include "orm-cxx/database/SqlDialect.hpp"

namespace orm::db::commands
{
class DefaultInsertCommand : public InsertCommand
{
public:
    explicit DefaultInsertCommand(const SqlDialect& dialect);

    [[nodiscard]] auto insert(model::ModelView model) const -> std::string override;

private:
    const SqlDialect& dialect;

    auto getInsertFields(const std::vector<std::string>& fieldNames) const -> std::string;
    auto getInsertValues(const std::vector<std::string>& fieldNames) const -> std::string;
    static auto getFieldsNames(model::ModelView model) -> std::vector<std::string>;
    static auto getForeignModelIdsNames(std::string_view foreignModelFieldName,
                                        model::ModelView target) -> std::vector<std::string>;
};
} // namespace orm::db::commands

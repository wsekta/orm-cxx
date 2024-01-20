#pragma once

#include "orm-cxx/database/CommandGenerator.hpp"
#include "SqliteTypeTranslator.hpp"

namespace orm::db::sqlite
{
class SqliteCommandGenerator : public CommandGenerator
{
public:
    [[nodiscard]] auto createTable(const model::ModelInfo& modelInfo) const -> std::string override;
    [[nodiscard]] auto dropTable(const model::ModelInfo& modelInfo) const -> std::string override;
    [[nodiscard]] auto insert(const model::ModelInfo& modelInfo) const -> std::string override;
    [[nodiscard]] auto select(const query::QueryData& queryData) const -> std::string override;

private:
    SqliteTypeTranslator typeTranslator;

    [[nodiscard]] auto addColumnsForForeignIds(const model::ModelInfo& modelInfo,
                                               const model::ColumnInfo& columnInfo) const -> std::string;
    [[nodiscard]] static auto addForeignIds(const model::ModelInfo& modelInfo) -> std::string;
};
} // namespace orm::db::sqlite

#pragma once

#include "orm-cxx/database/CommandGenerator.hpp"
#include "SqliteTypeTranslator.hpp"

namespace orm::db::sqlite
{
class SqliteCommandGenerator : public CommandGenerator
{
public:
    auto createTable(const model::ModelInfo& modelInfo) const -> std::string override;
    auto dropTable(const model::ModelInfo& modelInfo) const -> std::string override;
    auto insert(const model::ModelInfo& modelInfo) const -> std::string override;

private:
    SqliteTypeTranslator typeTranslator;

    auto addColumnsForForeignIds(const model::ForeignIdsInfo& foreignIdsInfo, const model::ColumnInfo& columnInfo) const
        -> std::string;

    auto addForeignIds(const model::ForeignIdsInfo& foreignIdsInfo) const -> std::string;
};
}
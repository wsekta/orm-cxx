module;

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

module orm;

import :internal;

namespace orm::db::mysql
{
auto MysqlTypeTranslator::toSqlType(model::ColumnType type,
                                    std::optional<std::size_t> maxStringLength) const -> std::string
{
    return mysql_family::sqlType(type, maxStringLength);
}
}

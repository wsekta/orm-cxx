#include "DefaultSelectCommand.hpp"

namespace orm::db::commands
{
auto DefaultSelectCommand::select(const query::QueryData& queryData) const -> std::string
{
    using namespace std::string_literals;

    std::string command = "SELECT * FROM "s.append(queryData.modelInfo.tableName);

    if (queryData.offset.has_value())
    {
        command.append(" OFFSET "s.append(std::to_string(queryData.offset.value())));
    }

    if (queryData.limit.has_value())
    {
        command.append(" LIMIT "s.append(std::to_string(queryData.limit.value())));
    }

    command.append(";");

    return command;
}
}

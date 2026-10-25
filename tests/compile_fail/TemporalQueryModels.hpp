#pragma once

#include <optional>

namespace temporal_query_models
{
struct Record
{
    int id;
    orm::Date date;
    orm::TimeOfDay time;
    orm::LocalDateTime local;
    orm::UtcTimestamp instant;
    orm::Uuid uuid;
    std::optional<orm::Date> optionalDate;
    std::optional<orm::TimeOfDay> optionalTime;
    std::optional<orm::LocalDateTime> optionalLocal;
    std::optional<orm::UtcTimestamp> optionalInstant;
    std::optional<orm::Uuid> optionalUuid;
};

using Schema = orm::Schema<Record>;
using Context = orm::OrmContext<Schema>;
} // namespace temporal_query_models

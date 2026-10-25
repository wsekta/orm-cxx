#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

import orm;

#include "Connection.hpp"

struct TemporalEvent
{
    orm::Uuid id;
    orm::Date date;
    orm::TimeOfDay startsAt;
    orm::LocalDateTime startsLocal;
    std::optional<orm::UtcTimestamp> recordedAt;

    inline static constexpr orm::reflection::FixedString table_name{"orm_temporal_values_example"};
};

struct RecordedEvent
{
    orm::Uuid id;
    std::optional<orm::UtcTimestamp> recordedAt;
};

using TemporalSchema = orm::Schema<TemporalEvent>;
using namespace orm::query;

inline constexpr auto byId = select<TemporalEvent>().where(col<&TemporalEvent::id>() == param<orm::Uuid, 0>());
inline constexpr auto recordedEvents =
    selectAs<TemporalEvent, RecordedEvent>(as<"id">(col<&TemporalEvent::id>()),
                                           as<"recordedAt">(col<&TemporalEvent::recordedAt>()))
        .where(col<&TemporalEvent::recordedAt>().isNotNull());

int main(int argc, char** argv)
{
    try
    {
        auto database = makeExampleDatabase(argc, argv);
        const auto& capabilities = database.getBackendCapabilities();
        if (!capabilities.supportsColumnType(orm::model::ColumnType::Uuid) ||
            !capabilities.supportsColumnType(orm::model::ColumnType::UtcTimestamp))
            throw std::invalid_argument{
                "This example requires native temporal and UUID columns: PostgreSQL or MariaDB"};
        auto context = database.orm<TemporalSchema>();
        context.createTable<TemporalEvent>();

        const orm::Date day{std::chrono::year{2026}, std::chrono::month{10}, std::chrono::day{24}};
        const orm::TimeOfDay time{std::chrono::hours{9} + std::chrono::microseconds{123456}};
        const orm::LocalDateTime local{std::chrono::local_days{day} + time.sinceMidnight()};
        const auto id = orm::Uuid::parse("019a1122-3344-7788-99aa-bbccddeeff00");
        context.insert(TemporalEvent{id, day, time, local, std::nullopt});

        for (const auto& event : context.select(byId, id))
            std::cout << event.id.toString()
                      << ": microseconds since midnight=" << event.startsAt.sinceMidnight().count()
                      << ", recorded=" << std::boolalpha << event.recordedAt.has_value() << '\n';

        // The application's explicit UTC instant is separate from its civil local datetime.
        const orm::UtcTimestamp recorded{std::chrono::sys_days{day} + std::chrono::hours{7} +
                                         std::chrono::microseconds{123456}};
        orm::Update<TemporalEvent> update;
        update.set(col<&TemporalEvent::recordedAt>(), recorded).where(col<&TemporalEvent::id>() == id);
        context.update(update);
        for (const auto& event : context.select(recordedEvents))
            std::cout << event.id.toString()
                      << ": UTC epoch microseconds=" << event.recordedAt.value().time_since_epoch().count() << '\n';

        context.deleteTable<TemporalEvent>();
        database.disconnect();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

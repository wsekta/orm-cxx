import orm;

// Public value comparisons must remain usable without including <compare> in the consumer.
static_assert((orm::TimeOfDay{} <=> orm::TimeOfDay{}) == 0);
static_assert(orm::TimeOfDay{} == orm::TimeOfDay{});
static_assert(!(orm::TimeOfDay{} != orm::TimeOfDay{}));
static_assert(!(orm::TimeOfDay{} < orm::TimeOfDay{}));

static_assert((orm::Uuid{} <=> orm::Uuid{}) == 0);
static_assert(orm::Uuid{} == orm::Uuid{});
static_assert(!(orm::Uuid{} != orm::Uuid{}));
static_assert(!(orm::Uuid{} < orm::Uuid{}));

auto compareImportedTemporalQueryValues() -> bool
{
    const orm::query::QueryValue local{orm::LocalDateTime{}};
    const orm::query::QueryValue localCopy{orm::LocalDateTime{}};
    const orm::query::QueryValue instant{orm::UtcTimestamp{}};
    const orm::query::QueryValue instantCopy{orm::UtcTimestamp{}};

    return local == localCopy && !(local != localCopy) && (local <=> localCopy) == 0 && instant == instantCopy &&
           !(instant != instantCopy) && (instant <=> instantCopy) == 0;
}

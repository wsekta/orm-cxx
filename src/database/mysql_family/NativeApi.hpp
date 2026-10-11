#pragma once

#include <string_view>

#include "soci/mysql/soci-mysql.h"

namespace orm_mysql_native
{
/** Private client boundary. The defaults always call the installed Oracle client. */
struct Calls
{
    decltype(&::mysql_real_query) realQuery = &::mysql_real_query;
    decltype(&::mysql_store_result) storeResult = &::mysql_store_result;
    decltype(&::mysql_fetch_row) fetchRow = &::mysql_fetch_row;
    decltype(&::mysql_free_result) freeResult = &::mysql_free_result;
    decltype(&::mysql_errno) errorNumber = &::mysql_errno;
    decltype(&::mysql_field_count) fieldCount = &::mysql_field_count;
    decltype(&::mysql_next_result) nextResult = &::mysql_next_result;
    decltype(&::mysql_set_server_option) setServerOption = &::mysql_set_server_option;
    decltype(&::mysql_set_character_set) setCharacterSet = &::mysql_set_character_set;
    decltype(&::mysql_close) close = &::mysql_close;
    void (*beforeStatement)(MYSQL*, std::string_view) = +[](MYSQL*, std::string_view) {};
};

inline constexpr Calls defaultCalls{};
inline thread_local const Calls* activeCalls = &defaultCalls;

[[nodiscard]] inline auto calls() noexcept -> const Calls&
{
    return *activeCalls;
}

/** Keep overrides local to one execution thread and restore nested callers on every exit. */
class ScopedCalls final
{
public:
    explicit ScopedCalls(const Calls& replacement) noexcept : previous_{activeCalls}
    {
        activeCalls = &replacement;
    }
    ~ScopedCalls()
    {
        activeCalls = previous_;
    }
    ScopedCalls(const ScopedCalls&) = delete;
    ScopedCalls(Calls&&) = delete;
    auto operator=(const ScopedCalls&) -> ScopedCalls& = delete;

private:
    const Calls* previous_;
};
}

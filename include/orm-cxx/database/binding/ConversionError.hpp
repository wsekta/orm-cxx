#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace orm::db::binding
{
/**
 * @brief Internal signal for a result value that cannot be represented by the
 * requested model or projection field without loss.
 */
class ConversionError final : public std::runtime_error
{
public:
    explicit ConversionError(std::string message) : std::runtime_error{std::move(message)} {}
};
} // namespace orm::db::binding

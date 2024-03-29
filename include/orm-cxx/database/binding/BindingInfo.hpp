#pragma once

namespace orm::db::binding
{
/**
 * @brief Binding info.
 *
 * This struct is used to store information about a binding how to treat data of a specific type.
 */
struct BindingInfo
{
    bool joinedValues = false;
};
} // namespace orm::db::binding

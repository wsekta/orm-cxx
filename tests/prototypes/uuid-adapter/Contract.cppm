module;

#include <concepts>

// Feasibility fixture only: the production contract will be introduced after
// this module ownership and reachability experiment succeeds on CI.
export module orm_cxx_uuid_prototype.contract;

import orm;

export namespace orm
{
template <typename T>
struct UuidAdapter;

template <typename T>
concept UuidAdaptable = requires(const T& value, const Uuid& uuid) {
    { UuidAdapter<T>::toUuid(value) } -> std::same_as<Uuid>;
    { UuidAdapter<T>::fromUuid(uuid) } -> std::same_as<T>;
};
} // namespace orm

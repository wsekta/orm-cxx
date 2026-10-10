export module orm.reflection:storage;

namespace orm::reflection::detail
{
template <typename T>
struct PointerWrapper
{
    T value;
};

template <typename T>
PointerWrapper(T) -> PointerWrapper<T>;

template <typename T>
[[nodiscard]] constexpr auto wrapPointer(const T& pointer) noexcept -> PointerWrapper<T>
{
    return {pointer};
}

template <typename T>
union InactiveStorage
{
    char inactive;
    T object;

    constexpr InactiveStorage() noexcept : inactive{} {}
    constexpr ~InactiveStorage() {}
};

} // namespace orm::reflection::detail

export module orm.reflection:binding_traits;

namespace orm::reflection::detail
{
template <typename>
inline constexpr bool alwaysFalse = false;

template <typename TupleType, bool FieldsAreAddressable>
struct BindingTraits
{
    using Tuple = TupleType;
    inline static constexpr bool fieldsAreAddressable = FieldsAreAddressable;
};
} // namespace orm::reflection::detail

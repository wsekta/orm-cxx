#include "orm-cxx/reflection/MemberName.hpp"

namespace
{
struct LightweightNameModel
{
    int identifier;
};

enum class LightweightValue
{
    selected
};

static_assert(orm::reflection::memberName<&LightweightNameModel::identifier>() == "identifier");
inline constexpr auto lightweightValueName = orm::reflection::valueName<LightweightValue::selected>();
static_assert(lightweightValueName.view().ends_with("selected"));
} // namespace

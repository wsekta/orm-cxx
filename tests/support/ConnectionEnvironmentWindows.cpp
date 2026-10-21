module;
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
module orm;
namespace orm::test
{
auto setConnectionEnvironment(const char* key, const std::optional<std::string>& value) -> void
{
    if (_putenv_s(key, value ? value->c_str() : "") != 0)
        throw std::runtime_error{"Cannot configure isolated connection test environment"};
}
}

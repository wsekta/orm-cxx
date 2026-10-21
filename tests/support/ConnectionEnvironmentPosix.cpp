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
    const auto result = value ? setenv(key, value->c_str(), 1) : unsetenv(key);
    if (result != 0)
        throw std::runtime_error{"Cannot configure isolated connection test environment"};
}
}

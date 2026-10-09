#include <cstddef>
#include <optional>
#include <string>

import orm.reflection;

extern "C" auto orm_cxx_global_string_type_name_data() -> const char*
{
    return orm::reflection::getTypeName<std::string>().data();
}

extern "C" auto orm_cxx_global_string_type_name_size() -> std::size_t
{
    return orm::reflection::getTypeName<std::string>().size();
}

extern "C" auto orm_cxx_global_optional_string_type_name_data() -> const char*
{
    return orm::reflection::getTypeName<std::optional<std::string>>().data();
}

extern "C" auto orm_cxx_global_optional_string_type_name_size() -> std::size_t
{
    return orm::reflection::getTypeName<std::optional<std::string>>().size();
}

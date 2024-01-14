#pragma once

#include <string>
#include <vector>

namespace orm::utils
{
auto replaceAll(std::string& s, std::string const& toReplace, std::string const& replaceWith) -> void;
}
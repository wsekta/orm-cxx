#pragma once

#include <string>
#include <vector>

namespace orm::utils
{
auto replaceAll(std::string& text, const std::string& toReplace, const std::string& replaceWith) -> void;
}

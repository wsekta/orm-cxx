#pragma once

// Keep the platform-specific signature grammar behind one private selector.
// Public reflection APIs never depend on compiler signature spellings.
#if defined(_MSC_VER)
#include "MsvcSignatureParser.hpp"
#elif defined(__clang__)
#include "ClangSignatureParser.hpp"
#elif defined(__GNUC__)
#include "GccSignatureParser.hpp"
#else
#error "orm::reflection supports Clang, GCC, and MSVC."
#endif

#pragma once

// clang-format off

#if defined(_MSC_VER)
    #define DISABLE_WARNING_PUSH __pragma(warning(push))
    #define DISABLE_WARNING_POP __pragma(warning(pop))
    #define DISABLE_WARNING(warningNumber) __pragma(warning(disable : warningNumber))

    #define DISABLE_EXTERNAL_WARNINGS DISABLE_WARNING(4100) DISABLE_WARNING(4505)

#elif defined(__GNUC__) || defined(__clang__)
    #define DO_PRAGMA(X) _Pragma(#X)
    #define DISABLE_WARNING_PUSH DO_PRAGMA(GCC diagnostic push)
    #define DISABLE_WARNING_POP DO_PRAGMA(GCC diagnostic pop)
    #define DISABLE_WARNING(warningName) DO_PRAGMA(GCC diagnostic ignored #warningName)

    #if defined(__clang__)
        #define DISABLE_EXTERNAL_WARNINGS DISABLE_WARNING(-Wunused-parameter) DISABLE_WARNING(-Wunused-function) DISABLE_WARNING(-Wignored-qualifiers) DISABLE_WARNING(-Wshadow) DISABLE_WARNING(-Wundefined-inline)
    #else
        #define DISABLE_EXTERNAL_WARNINGS DISABLE_WARNING(-Wunused-parameter) DISABLE_WARNING(-Wunused-function) DISABLE_WARNING(-Wignored-qualifiers) DISABLE_WARNING(-Wshadow)
    #endif
#else
    #define DISABLE_WARNING_PUSH
    #define DISABLE_WARNING_POP
    #define DISABLE_EXTERNAL_WARNINGS

#endif

// clang-format on

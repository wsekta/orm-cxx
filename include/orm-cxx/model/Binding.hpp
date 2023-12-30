#pragma once

#include "orm-cxx/model.hpp"
#include "soci/type-conversion.h"
#include "soci/values.h"

namespace soci
{
template <typename T>
struct type_conversion<orm::Model<T>>
{
    typedef values base_type;

    static void from_base(values const& /*v*/, indicator /* ind */, orm::Model<T>& /*model*/)
    {
        // TODO: Implement this
    }

    static void to_base(const orm::Model<T>& /*p*/, values& /*v*/, indicator& /*ind*/)
    {
        // TODO: Implement this
    }
};
}
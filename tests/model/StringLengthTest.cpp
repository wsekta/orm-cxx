module;
#include "tests/UnitTestPrelude.hpp"
module orm;
import :internal;
import :test_support;
import :model;
import :database;

namespace string_length_models
{
struct Bounded
{
    int id;
    std::string name;
    std::optional<std::string> note;
    inline static constexpr auto string_lengths =
        orm::stringLengths(orm::stringLength<&Bounded::name, 3>(), orm::stringLength<&Bounded::note, 8>());
};
using Schema = orm::Schema<Bounded>;
}

TEST(StringLengthTest, MetadataPreservesExplicitOptionalAndUnboundedColumns)
{
    constexpr auto view = orm::model::modelView<string_length_models::Schema, string_length_models::Bounded>();
    EXPECT_FALSE(view->columns[0].maxStringLength);
    EXPECT_EQ(view->columns[1].maxStringLength, 3);
    EXPECT_EQ(view->columns[2].maxStringLength, 8);
}

TEST(StringLengthTest, CountsUnicodeCharactersAndRejectsMalformedOrOversizedInput)
{
    using orm::db::binding::ConversionError;
    using orm::db::binding::validateStringLength;
    EXPECT_NO_THROW(validateStringLength("", 1));
    EXPECT_NO_THROW(validateStringLength("a\xc4\x85\xf0\x9f\x98\x80", 3));
    EXPECT_THROW(validateStringLength("a\xc4\x85\xf0\x9f\x98\x80", 2), ConversionError);
    for (const auto malformed :
         {"\x80", "\xc0\x80", "\xe0\x80\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xc2", "\xc2x"})
        EXPECT_THROW(validateStringLength(malformed, 20), ConversionError);
    EXPECT_NO_THROW(validateStringLength("unbounded", std::nullopt));
}

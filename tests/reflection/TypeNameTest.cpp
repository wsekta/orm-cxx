#include <array>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "orm-cxx/reflection/Reflection.hpp"
#include "tests/reflection/GeneratedFieldLimitModels.hpp"

namespace
{
using namespace orm::reflection;
using namespace std::string_view_literals;

template <FixedString Name>
struct CompileTimeName
{
    static constexpr auto value = Name;
};

template <typename T>
struct TypeWrapper
{
    T value;
};

enum class Colour
{
    red,
    blue
};

inline constexpr int namedSymbol = 42;

struct Empty
{
};

struct Single
{
    int value;
};

struct Record
{
    int id;
    bool enabled;
    double score;

    void reset()
    {
        score = 0.0;
    }
};

struct Person
{
    int id;
    std::string name;
    std::optional<int> age;
};

struct Nested
{
    Record record;
    std::array<int, 3> values;
    int initialized = 17;
};

union UnsupportedUnion
{
    int integer;
    double real;
};

struct NonAggregate
{
    explicit NonAggregate(int value) : value(value) {}

    int value;
};

template <typename T>
concept CanTieRvalue = requires(T&& value) { tieFields(std::move(value)); };

template <typename T>
concept CanVisitRvalue = requires(T&& value) { forEachField(std::move(value), [](const auto&) {}); };

template <typename T>
concept CanPointRvalue = requires(T&& value) { fieldPointers(std::move(value)); };

template <typename T>
concept CanViewTemporary = requires { T{}.view(); };

static_assert(CompileTimeName<"entity">::value == "entity"sv);
static_assert(FixedString{"same"} == FixedString{"same"});
static_assert(FixedString{"short"} != FixedString{"longer"});

static_assert(typeName<int>() == "int"sv);
static_assert(getTypeName<int>() == "int"sv);
static_assert(getTypeName<TypeWrapper<Record>>().find("class ") == std::string_view::npos);
static_assert(getTypeName<TypeWrapper<Record>>().find("struct ") == std::string_view::npos);
static_assert(memberName<&Record::id>() == "id"sv);
static_assert(memberName<&Record::reset>() == "reset"sv);
inline constexpr auto redName = valueName<Colour::red>();
inline constexpr auto symbolName = valueName<&namedSymbol>();
static_assert(redName.size() >= 3);
static_assert(redName[redName.size() - 3] == 'r' && redName[redName.size() - 2] == 'e' &&
              redName[redName.size() - 1] == 'd');
static_assert(symbolName.view().find("namedSymbol") != std::string_view::npos);

static_assert(fieldCount<Empty> == 0);
static_assert(fieldCount<Single> == 1);
static_assert(fieldCount<Record> == 3);
static_assert(fieldCount<Person> == 3);
static_assert(fieldCount<const Person&> == 3);
static_assert(fieldCount<Nested> == 3);
constexpr Nested nestedDefaults{{1, true, 3.0}, {4, 5, 6}};
static_assert(std::get<2>(tieFields(nestedDefaults)) == 17);
static_assert(std::get<1>(tieFields(nestedDefaults))[2] == 6);
static_assert(fieldCount<reflection_limit_models::MaxFields> == 128);
static_assert(fieldName<reflection_limit_models::MaxFields, 127>() == "field127"sv);
static_assert(fieldName<Record, 0>() == "id"sv);
static_assert(fieldName<Record, 2>() == "score"sv);
static_assert(fieldNames<Record>() == std::array{"id"sv, "enabled"sv, "score"sv});
static_assert(fields<Empty>().empty());
static_assert(fields<Record>()[1].index == 1);
static_assert(fields<Record>()[1].name == "enabled"sv);
static_assert(std::is_same_v<field_type_t<Record, 0>, int>);
static_assert(std::is_same_v<field_type_t<const Record&, 0>, int>);
static_assert(std::is_same_v<field_type_t<Person, 2>, std::optional<int>>);
static_assert(std::is_same_v<decltype(tieFields(std::declval<Record&>())), std::tuple<int&, bool&, double&>>);
static_assert(std::is_same_v<decltype(tieFields(std::declval<const Record&>())),
                             std::tuple<const int&, const bool&, const double&>>);
static_assert(std::is_same_v<decltype(fieldPointers(std::declval<Record&>())), std::tuple<int*, bool*, double*>>);
static_assert(std::is_same_v<decltype(fieldPointers(std::declval<const Record&>())),
                             std::tuple<const int*, const bool*, const double*>>);
static_assert(!CanTieRvalue<Record>);
static_assert(!CanVisitRvalue<Record>);
static_assert(!CanPointRvalue<Record>);
static_assert(!CanViewTemporary<FixedString<0>>);
static_assert(!detail::supportedAggregateCategory<UnsupportedUnion>);
static_assert(!detail::supportedAggregateCategory<int[2]>);
static_assert(!detail::supportedAggregateCategory<NonAggregate>);

consteval auto constexprFieldAccessWorks() -> bool
{
    Record record{1, false, 2.5};
    auto tied = tieFields(record);
    std::get<0>(tied) = 9;
    std::get<1>(tied) = true;

    const auto pointers = fieldPointers(record);
    *std::get<2>(pointers) = 7.5;
    return record.id == 9 && record.enabled && record.score == 7.5;
}

static_assert(constexprFieldAccessWorks());

TEST(ReflectionNamesTest, exposesCompilerNamesThroughStableStorage)
{
    EXPECT_THAT(getTypeName<Person>(), ::testing::HasSubstr("Person"));
    EXPECT_THAT(fields<Person>()[1].typeName, ::testing::HasSubstr("string"));

    const auto firstNames = fieldNames<Person>();
    const auto secondNames = fieldNames<Person>();
    EXPECT_EQ(firstNames, secondNames);
    EXPECT_EQ(firstNames[0].data(), secondNames[0].data());
}

TEST(ReflectionFieldsTest, tiesAndAddressesMutableFields)
{
    Record record{7, true, 9.5};
    auto tied = tieFields(record);
    std::get<0>(tied) = 42;
    std::get<2>(tied) = 4.5;

    EXPECT_EQ(record.id, 42);
    EXPECT_DOUBLE_EQ(record.score, 4.5);

    const auto pointers = fieldPointers(record);
    EXPECT_EQ(std::get<0>(pointers), &record.id);
    EXPECT_EQ(std::get<1>(pointers), &record.enabled);
    EXPECT_EQ(std::get<2>(pointers), &record.score);
}

TEST(ReflectionFieldsTest, supportsEmptyAggregates)
{
    Empty empty;
    EXPECT_EQ(std::tuple_size_v<decltype(tieFields(empty))>, std::size_t{0});
    EXPECT_EQ(std::tuple_size_v<decltype(fieldPointers(empty))>, std::size_t{0});

    std::size_t visits = 0;
    forEachField(empty, [&](auto&&...) { ++visits; });
    EXPECT_EQ(visits, std::size_t{0});
}

TEST(ReflectionFieldsTest, visitsFieldsWithCompileTimeIndexAndDescriptor)
{
    Record record{3, false, 1.25};
    std::vector<std::string_view> names;
    std::size_t expectedIndex = 0;

    forEachField(
        record,
        [&]<std::size_t Index>(std::integral_constant<std::size_t, Index>, FieldDescriptor descriptor, auto& field)
        {
            static_assert(Index < fieldCount<Record>);
            EXPECT_EQ(descriptor.index, Index);
            EXPECT_EQ(Index, expectedIndex++);
            names.push_back(descriptor.name);

            if constexpr (Index == 0)
            {
                static_assert(std::is_same_v<decltype(field), int&>);
                field = 99;
            }
        });

    EXPECT_EQ(record.id, 99);
    EXPECT_EQ(names, (std::vector<std::string_view>{"id", "enabled", "score"}));
}

TEST(ReflectionFieldsTest, supportsErgonomicVisitorFallbacksAndConstValues)
{
    const Record record{3, true, 8.0};
    std::size_t descriptorVisits = 0;
    forEachField(record,
                 [&](FieldDescriptor descriptor, const auto& field)
                 {
                     EXPECT_EQ(descriptor.index, descriptorVisits++);
                     static_assert(std::is_const_v<std::remove_reference_t<decltype(field)>>);
                 });

    std::size_t fieldOnlyVisits = 0;
    forEachField(record, [&](const auto&) { ++fieldOnlyVisits; });

    EXPECT_EQ(descriptorVisits, fieldCount<Record>);
    EXPECT_EQ(fieldOnlyVisits, fieldCount<Record>);
}
} // namespace

module;

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module orm:expressions;

import orm.reflection;
import :foundation;
import :model;

// query/QueryValue.hpp
namespace orm::query
{
export
{

    /**
     * @brief A value that can be bound as a query parameter.
     *
     * QueryValue stores the subset of C++ values supported by the SELECT query DSL.
     * Values are rendered as SOCI bind parameters, not interpolated into SQL.
     */
    class QueryValue
    {
    public:
        using Value = std::variant<int, long long, unsigned long long, double, std::string>;

        QueryValue(bool inputValue) : logicalType{model::ColumnType::Bool}, value{static_cast<int>(inputValue)} {}
        QueryValue(char inputValue) : logicalType{model::ColumnType::Char}, value{static_cast<int>(inputValue)} {}
        QueryValue(signed char inputValue) : logicalType{model::ColumnType::Char}, value{static_cast<int>(inputValue)}
        {
        }
        QueryValue(unsigned char inputValue)
            : logicalType{model::ColumnType::UnsignedChar}, value{static_cast<int>(inputValue)}
        {
        }
        QueryValue(short inputValue) : logicalType{model::ColumnType::Short}, value{static_cast<int>(inputValue)} {}
        QueryValue(unsigned short inputValue)
            : logicalType{model::ColumnType::UnsignedShort}, value{static_cast<int>(inputValue)}
        {
        }
        QueryValue(int inputValue) : logicalType{model::ColumnType::Int}, value{inputValue} {}
        QueryValue(unsigned int inputValue)
            : logicalType{model::ColumnType::UnsignedInt}, value{static_cast<unsigned long long>(inputValue)}
        {
        }
        QueryValue(long inputValue)
            : logicalType{sizeof(long) > sizeof(int) ? model::ColumnType::LongLong : model::ColumnType::Int},
              value{storeLong(inputValue)}
        {
        }
        QueryValue(unsigned long inputValue)
            : logicalType{sizeof(unsigned long) > sizeof(unsigned int) ? model::ColumnType::UnsignedLongLong :
                                                                         model::ColumnType::UnsignedInt},
              value{static_cast<unsigned long long>(inputValue)}
        {
        }
        QueryValue(long long inputValue) : logicalType{model::ColumnType::LongLong}, value{inputValue} {}
        QueryValue(unsigned long long inputValue) : logicalType{model::ColumnType::UnsignedLongLong}, value{inputValue}
        {
        }
        QueryValue(float inputValue) : logicalType{model::ColumnType::Float}, value{static_cast<double>(inputValue)} {}
        QueryValue(double inputValue) : logicalType{model::ColumnType::Double}, value{inputValue} {}
        QueryValue(const char* inputValue) : logicalType{model::ColumnType::String}, value{std::string{inputValue}} {}
        QueryValue(std::string inputValue) : logicalType{model::ColumnType::String}, value{std::move(inputValue)} {}
        QueryValue(std::string_view inputValue) : logicalType{model::ColumnType::String}, value{std::string{inputValue}}
        {
        }

        template <std::size_t Size>
        QueryValue(const char (&inputValue)[Size])
            : logicalType{model::ColumnType::String}, value{std::string{inputValue}}
        {
        }

        template <typename T>
        QueryValue(const std::optional<T>& optionalValue) : QueryValue(optionalValue.value())
        {
        }

        /**
         * @brief Restores a canonical transport value with its model-level type.
         *
         * Hydration and backend adapters use this factory when several logical
         * column types share the same SOCI exchange representation.
         */
        [[nodiscard]] static auto fromStorage(model::ColumnType logicalType, Value value) -> QueryValue
        {
            if (not isCompatibleStorage(logicalType, value))
            {
                throw std::invalid_argument{"Query value storage does not match its logical column type"};
            }

            return QueryValue{logicalType, std::move(value), StorageValueTag{}};
        }

        /**
         * @brief Checks both the canonical variant alternative and its logical range.
         */
        [[nodiscard]] static auto isCompatibleStorage(model::ColumnType logicalType,
                                                      const Value& storedValue) noexcept -> bool
        {
            const auto* intValue = std::get_if<int>(&storedValue);
            const auto* unsignedValue = std::get_if<unsigned long long>(&storedValue);
            const auto* doubleValue = std::get_if<double>(&storedValue);

            switch (logicalType)
            {
            case model::ColumnType::Bool:
                return intValue != nullptr and (*intValue == 0 or *intValue == 1);
            case model::ColumnType::Char:
                return intValue != nullptr and *intValue >= std::numeric_limits<signed char>::lowest() and
                       *intValue <= std::numeric_limits<unsigned char>::max();
            case model::ColumnType::UnsignedChar:
                return intValue != nullptr and *intValue >= 0 and
                       *intValue <= std::numeric_limits<unsigned char>::max();
            case model::ColumnType::Short:
                return intValue != nullptr and *intValue >= std::numeric_limits<short>::lowest() and
                       *intValue <= std::numeric_limits<short>::max();
            case model::ColumnType::UnsignedShort:
                return intValue != nullptr and *intValue >= 0 and
                       *intValue <= std::numeric_limits<unsigned short>::max();
            case model::ColumnType::Int:
                return intValue != nullptr;
            case model::ColumnType::UnsignedInt:
                return unsignedValue != nullptr and *unsignedValue <= std::numeric_limits<unsigned int>::max();
            case model::ColumnType::LongLong:
                return std::holds_alternative<long long>(storedValue);
            case model::ColumnType::UnsignedLongLong:
                return unsignedValue != nullptr;
            case model::ColumnType::Float:
                return doubleValue != nullptr and std::isfinite(*doubleValue) and
                       *doubleValue >= static_cast<double>(std::numeric_limits<float>::lowest()) and
                       *doubleValue <= static_cast<double>(std::numeric_limits<float>::max()) and
                       static_cast<double>(static_cast<float>(*doubleValue)) == *doubleValue;
            case model::ColumnType::Double:
                return doubleValue != nullptr and std::isfinite(*doubleValue);
            case model::ColumnType::String:
                return std::holds_alternative<std::string>(storedValue);
            case model::ColumnType::Uuid:
                return false;
            }

            return false;
        }

        [[nodiscard]] auto getLogicalType() const noexcept -> model::ColumnType
        {
            return logicalType;
        }

        [[nodiscard]] auto get() const -> const Value&
        {
            return value;
        }

        auto operator<=>(const QueryValue&) const -> std::partial_ordering = default;

    private:
        struct StorageValueTag
        {
        };

        [[nodiscard]] static auto storeLong(long inputValue) -> Value
        {
            if constexpr (sizeof(long) > sizeof(int))
            {
                return Value{static_cast<long long>(inputValue)};
            }
            else
            {
                return Value{static_cast<int>(inputValue)};
            }
        }

        QueryValue(model::ColumnType logicalTypeInit, Value valueInit, StorageValueTag)
            : logicalType{logicalTypeInit}, value{std::move(valueInit)}
        {
        }

        model::ColumnType logicalType;
        Value value;
    };

    /**
     * @brief A named parameter for raw query fragments.
     */
    struct QueryParameter
    {
        std::string name;
        QueryValue value;
    };

    /**
     * @brief Creates a named parameter for a raw SQL fragment.
     *
     * The name must not include the leading ':'.
     */
    template <typename T>
    auto param(std::string name, T value) -> QueryParameter
    {
        return QueryParameter{.name = std::move(name), .value = QueryValue{std::move(value)}};
    }
}
} // namespace orm::query

// query/RuntimePredicate.hpp
namespace orm::query::detail
{
class Predicate;
struct PredicateNode;

using PredicateNodePtr = std::shared_ptr<const PredicateNode>;

enum class ComparisonOperator
{
    Equal,
    NotEqual,
    Greater,
    GreaterOrEqual,
    Less,
    LessOrEqual,
    Like,
    NotLike,
};

enum class NullOperator
{
    IsNull,
    IsNotNull,
};

enum class ListOperator
{
    In,
    NotIn,
};

enum class BetweenOperator
{
    Between,
    NotBetween,
};

enum class LogicalOperator
{
    And,
    Or,
};

enum class CollectionOperator
{
    Any,
    Exists,
    None,
};

class Column
{
public:
    /**
     * @brief Creates a column reference from a model field path.
     *
     * A path can reference a direct field such as "name" or one one-to-one relation level,
     * such as "profile.city".
     */
    explicit Column(std::string columnPath) : path{std::move(columnPath)} {}

    [[nodiscard]] auto getPath() const -> const std::string&
    {
        return path;
    }

    template <typename T>
    auto operator==(T value) const -> Predicate;

    auto operator==(std::nullptr_t) const -> Predicate;

    template <typename T>
    auto operator!=(T value) const -> Predicate;

    auto operator!=(std::nullptr_t) const -> Predicate;

    template <typename T>
    auto operator>(T value) const -> Predicate;

    template <typename T>
    auto operator>=(T value) const -> Predicate;

    template <typename T>
    auto operator<(T value) const -> Predicate;

    template <typename T>
    auto operator<=(T value) const -> Predicate;

    template <typename T>
    auto like(T value) const -> Predicate;

    template <typename T>
    auto notLike(T value) const -> Predicate;

    auto isNull() const -> Predicate;

    auto isNotNull() const -> Predicate;

    template <typename T>
    auto in(std::initializer_list<T> values) const -> Predicate;

    template <typename T>
    auto in(const std::vector<T>& values) const -> Predicate;

    auto in(std::vector<QueryValue> values) const -> Predicate;

    template <typename T>
    auto notIn(std::initializer_list<T> values) const -> Predicate;

    template <typename T>
    auto notIn(const std::vector<T>& values) const -> Predicate;

    auto notIn(std::vector<QueryValue> values) const -> Predicate;

    template <typename Lower, typename Upper>
    auto between(Lower lowerValue, Upper upperValue) const -> Predicate;

    template <typename Lower, typename Upper>
    auto notBetween(Lower lowerValue, Upper upperValue) const -> Predicate;

private:
    std::string path;

    auto compare(ComparisonOperator comparisonOperator, QueryValue value) const -> Predicate;
    auto list(ListOperator listOperator, std::vector<QueryValue> values) const -> Predicate;

    template <typename Container>
    static auto makeValues(const Container& values) -> std::vector<QueryValue>;
};

struct ComparisonExpression
{
    Column column;
    ComparisonOperator comparisonOperator;
    QueryValue value;
};

struct NullExpression
{
    Column column;
    NullOperator nullOperator;
};

struct ListExpression
{
    Column column;
    ListOperator listOperator;
    std::vector<QueryValue> values;
};

struct BetweenExpression
{
    Column column;
    BetweenOperator betweenOperator;
    QueryValue lowerValue;
    QueryValue upperValue;
};

struct LogicalExpression
{
    PredicateNodePtr left;
    LogicalOperator logicalOperator;
    PredicateNodePtr right;
};

struct NotExpression
{
    PredicateNodePtr predicate;
};

struct RawExpression
{
    std::string sql;
    std::vector<QueryParameter> parameters;
};

struct CollectionExpression
{
    std::string relation;
    CollectionOperator collectionOperator;
    PredicateNodePtr predicate;
};

struct PredicateNode
{
    using Expression = std::variant<ComparisonExpression, NullExpression, ListExpression, BetweenExpression,
                                    LogicalExpression, NotExpression, RawExpression, CollectionExpression>;

    Expression expression;
};

class Predicate
{
public:
    /**
     * @brief Creates a predicate from an expression node.
     */
    explicit Predicate(PredicateNode predicateNode) : node{std::make_shared<PredicateNode>(std::move(predicateNode))} {}

    [[nodiscard]] auto getNode() const -> const PredicateNode&
    {
        return *node;
    }

private:
    PredicateNodePtr node;

    friend auto operator&&(const Predicate& left, const Predicate& right) -> Predicate;
    friend auto operator||(const Predicate& left, const Predicate& right) -> Predicate;
    friend auto operator!(const Predicate& predicate) -> Predicate;
};

template <typename T>
auto Column::operator==(T value) const -> Predicate
{
    return compare(ComparisonOperator::Equal, QueryValue{std::move(value)});
}

inline auto Column::operator==(std::nullptr_t) const -> Predicate
{
    return isNull();
}

template <typename T>
auto Column::operator!=(T value) const -> Predicate
{
    return compare(ComparisonOperator::NotEqual, QueryValue{std::move(value)});
}

inline auto Column::operator!=(std::nullptr_t) const -> Predicate
{
    return isNotNull();
}

template <typename T>
auto Column::operator>(T value) const -> Predicate
{
    return compare(ComparisonOperator::Greater, QueryValue{std::move(value)});
}

template <typename T>
auto Column::operator>=(T value) const -> Predicate
{
    return compare(ComparisonOperator::GreaterOrEqual, QueryValue{std::move(value)});
}

template <typename T>
auto Column::operator<(T value) const -> Predicate
{
    return compare(ComparisonOperator::Less, QueryValue{std::move(value)});
}

template <typename T>
auto Column::operator<=(T value) const -> Predicate
{
    return compare(ComparisonOperator::LessOrEqual, QueryValue{std::move(value)});
}

template <typename T>
auto Column::like(T value) const -> Predicate
{
    return compare(ComparisonOperator::Like, QueryValue{std::move(value)});
}

template <typename T>
auto Column::notLike(T value) const -> Predicate
{
    return compare(ComparisonOperator::NotLike, QueryValue{std::move(value)});
}

inline auto Column::isNull() const -> Predicate
{
    return Predicate{PredicateNode{NullExpression{.column = *this, .nullOperator = NullOperator::IsNull}}};
}

inline auto Column::isNotNull() const -> Predicate
{
    return Predicate{PredicateNode{NullExpression{.column = *this, .nullOperator = NullOperator::IsNotNull}}};
}

template <typename T>
auto Column::in(std::initializer_list<T> values) const -> Predicate
{
    return in(makeValues(values));
}

template <typename T>
auto Column::in(const std::vector<T>& values) const -> Predicate
{
    return in(makeValues(values));
}

inline auto Column::in(std::vector<QueryValue> values) const -> Predicate
{
    return list(ListOperator::In, std::move(values));
}

template <typename T>
auto Column::notIn(std::initializer_list<T> values) const -> Predicate
{
    return notIn(makeValues(values));
}

template <typename T>
auto Column::notIn(const std::vector<T>& values) const -> Predicate
{
    return notIn(makeValues(values));
}

inline auto Column::notIn(std::vector<QueryValue> values) const -> Predicate
{
    return list(ListOperator::NotIn, std::move(values));
}

template <typename Lower, typename Upper>
auto Column::between(Lower lowerValue, Upper upperValue) const -> Predicate
{
    return Predicate{PredicateNode{BetweenExpression{.column = *this,
                                                     .betweenOperator = BetweenOperator::Between,
                                                     .lowerValue = QueryValue{std::move(lowerValue)},
                                                     .upperValue = QueryValue{std::move(upperValue)}}}};
}

template <typename Lower, typename Upper>
auto Column::notBetween(Lower lowerValue, Upper upperValue) const -> Predicate
{
    return Predicate{PredicateNode{BetweenExpression{.column = *this,
                                                     .betweenOperator = BetweenOperator::NotBetween,
                                                     .lowerValue = QueryValue{std::move(lowerValue)},
                                                     .upperValue = QueryValue{std::move(upperValue)}}}};
}

inline auto Column::compare(ComparisonOperator comparisonOperator, QueryValue value) const -> Predicate
{
    return Predicate{
        PredicateNode{ComparisonExpression{.column = *this, .comparisonOperator = comparisonOperator, .value = value}}};
}

inline auto Column::list(ListOperator listOperator, std::vector<QueryValue> values) const -> Predicate
{
    if (values.empty())
    {
        throw std::invalid_argument{"IN predicate requires at least one value"};
    }

    return Predicate{
        PredicateNode{ListExpression{.column = *this, .listOperator = listOperator, .values = std::move(values)}}};
}

template <typename Container>
auto Column::makeValues(const Container& values) -> std::vector<QueryValue>
{
    std::vector<QueryValue> queryValues;
    queryValues.reserve(values.size());

    for (const auto& value : values)
    {
        queryValues.emplace_back(value);
    }

    return queryValues;
}

inline auto col(std::string path) -> Column
{
    return Column{std::move(path)};
}

/**
 * @brief Creates a typed field reference.
 *
 * The current implementation still receives the field name as a string. The Model and
 * FieldType template parameters document the intended model and field type at the call site.
 */
template <typename Model, typename FieldType>
auto field(std::string path) -> Column
{
    return Column{std::move(path)};
}

inline auto operator&&(const Predicate& left, const Predicate& right) -> Predicate
{
    return Predicate{PredicateNode{
        LogicalExpression{.left = left.node, .logicalOperator = LogicalOperator::And, .right = right.node}}};
}

inline auto operator||(const Predicate& left, const Predicate& right) -> Predicate
{
    return Predicate{PredicateNode{
        LogicalExpression{.left = left.node, .logicalOperator = LogicalOperator::Or, .right = right.node}}};
}

inline auto operator!(const Predicate& predicate) -> Predicate
{
    return Predicate{PredicateNode{NotExpression{.predicate = predicate.node}}};
}

/**
 * @brief Matches models for which at least one collection element satisfies a predicate.
 *
 * Column paths inside
 * the predicate are relative to the collection element model.
 */
inline auto any(std::string relation, const Predicate& predicate) -> Predicate
{
    return Predicate{
        PredicateNode{CollectionExpression{.relation = std::move(relation),
                                           .collectionOperator = CollectionOperator::Any,
                                           .predicate = std::make_shared<PredicateNode>(predicate.getNode())}}};
}

/**
 * @brief Matches models whose mapped collection contains at least one element.
 */
inline auto exists(std::string relation) -> Predicate
{
    return Predicate{PredicateNode{CollectionExpression{
        .relation = std::move(relation), .collectionOperator = CollectionOperator::Exists, .predicate = nullptr}}};
}

/**
 * @brief Matches models for which no collection element satisfies a predicate.
 */
inline auto none(std::string relation, const Predicate& predicate) -> Predicate
{
    return Predicate{
        PredicateNode{CollectionExpression{.relation = std::move(relation),
                                           .collectionOperator = CollectionOperator::None,
                                           .predicate = std::make_shared<PredicateNode>(predicate.getNode())}}};
}

/**
 * @brief Creates a raw SQL predicate.
 *
 * The SQL text is inserted as-is. Values should be supplied with query::param
 * so they can still be bound as database parameters.
 */
template <typename... Parameters>
auto raw(std::string sql, Parameters... parameters) -> Predicate
{
    return Predicate{PredicateNode{
        RawExpression{.sql = std::move(sql), .parameters = std::vector<QueryParameter>{std::move(parameters)...}}}};
}
} // namespace orm::query::detail

// query/Parameters.hpp
namespace orm::query
{
export
{

    template <typename T, std::size_t Index>
    class Parameter;
}
}

namespace orm::query::detail
{
struct TypedAccess
{
    template <typename T, typename... Args>
    static constexpr auto make(Args&&... args) -> T
    {
        return T(std::forward<Args>(args)...);
    }
    template <typename T>
    static auto erase(const T& value)
    {
        return value.runtime();
    }
    template <typename T, typename Args>
    static auto erase(const T& value, const Args& args)
    {
        if constexpr (requires { value.runtime(args); })
            return value.runtime(args);
        else
            return value.runtime();
    }
    template <typename T>
    static constexpr auto children(const T& value) -> const auto&
    {
        return value.children;
    }
};

template <typename T>
auto erase(const T& value)
{
    return TypedAccess::erase(value);
}
template <typename T>
using scalar_t = std::remove_cv_t<model::detail::static_optional_value_t<std::remove_cvref_t<T>>>;
template <typename T>
inline constexpr bool numericScalar =
    std::is_arithmetic_v<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool> &&
    requires { model::LogicalTypeTraits<std::remove_cvref_t<T>>::value; };
template <typename T>
inline constexpr bool supportedScalar =
    numericScalar<T> || std::same_as<std::remove_cvref_t<T>, bool> || std::same_as<std::remove_cvref_t<T>, std::string>;
template <typename Target, typename Source>
inline constexpr bool fitsNumericLimits = []
{
    using T = std::remove_cvref_t<Target>;
    using S = std::decay_t<Source>;
    if constexpr (!std::is_arithmetic_v<T> || std::same_as<T, bool> || !std::is_arithmetic_v<S> ||
                  std::same_as<S, bool>)
        return false;
    else if constexpr (std::is_integral_v<T> && std::is_integral_v<S>)
        return (!std::is_signed_v<S> || std::is_signed_v<T>) &&
               std::numeric_limits<T>::digits >= std::numeric_limits<S>::digits;
    else if constexpr (std::is_floating_point_v<T> && std::is_integral_v<S>)
        return std::numeric_limits<T>::radix == 2 && std::numeric_limits<T>::digits >= std::numeric_limits<S>::digits &&
               std::numeric_limits<T>::max_exponent > std::numeric_limits<S>::digits;
    else if constexpr (std::is_floating_point_v<T> && std::is_floating_point_v<S>)
        return std::numeric_limits<T>::radix == std::numeric_limits<S>::radix &&
               std::numeric_limits<T>::digits >= std::numeric_limits<S>::digits &&
               std::numeric_limits<T>::max_exponent >= std::numeric_limits<S>::max_exponent &&
               std::numeric_limits<T>::min_exponent <= std::numeric_limits<S>::min_exponent;
    else
        return false;
}();
template <typename Target, typename Source>
inline constexpr bool isSafeNumericWidening = numericScalar<Target> && fitsNumericLimits<Target, Source>;
template <typename T>
inline constexpr bool stringValue =
    std::same_as<std::remove_cvref_t<T>, std::string> || std::same_as<std::remove_cvref_t<T>, std::string_view> ||
    std::same_as<std::decay_t<T>, const char*> || std::same_as<std::decay_t<T>, char*>;
template <typename T>
struct ParameterValue
{
    using type = std::remove_cvref_t<T>;
};
template <typename T, std::size_t I>
struct ParameterValue<Parameter<T, I>>
{
    using type = T;
};
template <typename T>
using parameter_value_t = typename ParameterValue<std::remove_cvref_t<T>>::type;
template <typename Target, typename Source>
inline constexpr bool compatibleValue = isSafeNumericWidening<Target, parameter_value_t<Source>> ||
                                        (std::same_as<Target, bool> && std::same_as<parameter_value_t<Source>, bool>) ||
                                        (std::same_as<Target, std::string> && stringValue<parameter_value_t<Source>>);
template <typename Target, typename Source>
concept ORM_QUERY_UNBOUND_PARAMETER_VALUE = std::same_as<parameter_value_t<Source>, std::remove_cvref_t<Source>>;
template <typename Target, typename Source>
concept ORM_QUERY_VALUE = compatibleValue<Target, Source> && ORM_QUERY_UNBOUND_PARAMETER_VALUE<Target, Source>;
template <typename Target, typename Source>
concept ORM_QUERY_VALUE_OR_PARAMETER = compatibleValue<Target, Source>;
template <typename Model, typename Expected>
concept ORM_QUERY_MODEL_TYPE = std::same_as<Model, Expected>;
template <typename E, typename M>
concept ORM_QUERY_MODEL = requires { typename E::Model; } && ORM_QUERY_MODEL_TYPE<typename E::Model, M>;
template <auto... Members>
concept ORM_QUERY_MEMBER = sizeof...(Members) > 0 && (std::is_member_object_pointer_v<decltype(Members)> && ...) &&
                           ((Members != nullptr) && ...);
template <typename T>
concept ORM_QUERY_NUMERIC = numericScalar<T>;
template <typename T>
concept ORM_QUERY_STRING = std::same_as<T, std::string>;
template <typename T>
concept ORM_QUERY_ORDERABLE = numericScalar<T> || std::same_as<T, std::string>;
template <bool Nullable>
concept ORM_QUERY_NULLABLE = Nullable;

template <typename Target, typename Source>
    requires ORM_QUERY_VALUE<Target, Source>
auto typedValue(Source&& value) -> QueryValue
{
    if constexpr (std::same_as<Target, std::string>)
    {
        if constexpr (std::is_pointer_v<std::decay_t<Source>>)
            if (value == nullptr)
                throw std::invalid_argument{"A string query value must not be a null pointer"};
        return QueryValue{std::string{std::forward<Source>(value)}};
    }
    else
        return QueryValue{static_cast<Target>(value)};
}

template <typename... Tuples>
using ConcatTuples = decltype(std::tuple_cat(std::declval<Tuples>()...));
template <typename T, typename = void>
struct ParameterTypesTrait
{
    using type = std::tuple<>;
};
template <typename T>
struct ParameterTypesTrait<T, std::void_t<typename T::ParameterTypes>>
{
    using type = typename T::ParameterTypes;
};
template <typename... T>
struct ParameterTypesTrait<std::tuple<T...>, void>
{
    using type = ConcatTuples<typename ParameterTypesTrait<std::remove_cvref_t<T>>::type...>;
};
template <typename T>
using ParameterTypes = typename ParameterTypesTrait<std::remove_cvref_t<T>>::type;
template <typename T>
inline constexpr bool hasParameters = std::tuple_size_v<ParameterTypes<T>> != 0;
template <typename T>
concept ORM_QUERY_UNBOUND_PARAMETER = !hasParameters<T>;
template <typename T>
concept ORM_QUERY_BOUND = ORM_QUERY_UNBOUND_PARAMETER<T>;
template <typename Tuple>
struct SlotIndices;
template <typename... P>
struct SlotIndices<std::tuple<P...>>
{
    inline static constexpr std::array<std::size_t, sizeof...(P)> value{P::index...};
};
template <typename T>
inline constexpr auto parameterSlots = SlotIndices<ParameterTypes<T>>::value;

template <std::size_t N>
struct CapturedText
{
    std::array<char, N> data{};
    std::size_t length{};
    constexpr auto view() const -> std::string_view
    {
        return {data.data(), length};
    }
};
template <typename T>
constexpr auto captureValue(T&& value)
{
    using V = std::remove_cvref_t<T>;
    if constexpr (std::is_array_v<V> && std::same_as<std::remove_cv_t<std::remove_extent_t<V>>, char>)
    {
        CapturedText<std::extent_v<V>> result;
        for (; result.length < result.data.size() && value[result.length] != '\0'; ++result.length)
            result.data[result.length] = value[result.length];
        return result;
    }
    else if constexpr (stringValue<T>)
    {
        if constexpr (std::is_pointer_v<std::decay_t<T>>)
            if (value == nullptr)
                throw std::invalid_argument{"A string query value must not be a null pointer"};
        return std::string{std::forward<T>(value)};
    }
    else if constexpr (model::isNullable<V>)
    {
        using Owned = decltype(captureValue(value.value()));
        if (value.has_value())
            return std::optional<Owned>{captureValue(value.value())};
        return std::optional<Owned>{};
    }
    else
        return V{std::forward<T>(value)};
}
template <typename T>
struct IsParameter : std::false_type
{
};
template <typename T, std::size_t I>
struct IsParameter<Parameter<T, I>> : std::true_type
{
};
template <typename T>
inline constexpr bool isParameter = IsParameter<std::remove_cvref_t<T>>::value;
template <typename T, typename Args>
constexpr decltype(auto) resolveValue(const T& value, const Args& args)
{
    if constexpr (isParameter<T>)
        return std::get<T::index>(args);
    else if constexpr (requires { value.view(); })
        return value.view();
    else
        return (value);
}

template <typename T>
struct ContainerTraits
{
    inline static constexpr bool isContainer = false;
};
template <typename T, std::size_t N>
struct ContainerTraits<std::array<T, N>>
{
    using Value = T;
    inline static constexpr bool isContainer = true;
    inline static constexpr bool fixed = true;
    inline static constexpr int family = 0;
    inline static constexpr std::size_t size = N;
};
template <typename T, typename A>
struct ContainerTraits<std::vector<T, A>>
{
    using Value = T;
    inline static constexpr bool isContainer = true;
    inline static constexpr bool fixed = false;
    inline static constexpr int family = 1;
};
template <typename T>
struct ContainerTraits<std::initializer_list<T>>
{
    using Value = T;
    inline static constexpr bool isContainer = true;
    inline static constexpr bool fixed = false;
    inline static constexpr int family = 2;
};
template <typename Target, typename Source>
consteval auto parameterCompatible() -> bool
{
    using T = std::remove_cvref_t<Target>;
    using S = std::remove_cvref_t<Source>;
    if constexpr (ContainerTraits<T>::isContainer && ContainerTraits<S>::isContainer)
    {
        if constexpr (ContainerTraits<T>::family != ContainerTraits<S>::family)
            return false;
        else if constexpr (ContainerTraits<T>::fixed)
            return ContainerTraits<T>::size == ContainerTraits<S>::size &&
                   compatibleValue<typename ContainerTraits<T>::Value, typename ContainerTraits<S>::Value>;
        else
            return compatibleValue<typename ContainerTraits<T>::Value, typename ContainerTraits<S>::Value>;
    }
    else if constexpr (model::isNullable<T>)
    {
        if constexpr (model::isNullable<S>)
            return parameterCompatible<scalar_t<T>, scalar_t<S>>();
        else
            return parameterCompatible<scalar_t<T>, S>();
    }
    else if constexpr (std::is_arithmetic_v<T> && !std::same_as<T, bool>)
        return fitsNumericLimits<T, S>;
    else if constexpr (stringValue<T>)
        return stringValue<S>;
    else
        return compatibleValue<T, S>;
}
template <typename P, typename Slots>
struct SlotTypesCompatible;
template <typename P, typename... Slots>
struct SlotTypesCompatible<P, std::tuple<Slots...>>
{
    inline static constexpr bool value =
        ((P::index != Slots::index || std::same_as<typename P::Value, typename Slots::Value>) && ...);
};
template <typename Root, typename... Args>
consteval auto validateParameters() -> void
{
    using Slots = ParameterTypes<Root>;
    constexpr auto slots = parameterSlots<Root>;
    constexpr bool indicesInRange = [slots]() consteval
    {
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i] >= slots.size())
                return false;
        return true;
    }();
    constexpr auto count = [slots]() consteval
    {
        std::size_t result = 0;
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const auto slot = slots[i];
            if (slot >= slots.size())
                return std::size_t{0};
            result = std::max(result, slot + 1);
        }
        return result;
    }();
    constexpr bool continuous = [slots]() consteval
    {
        if (!indicesInRange)
            return false;
        for (std::size_t i = 0; i < count; ++i)
        {
            bool found = false;
            for (std::size_t j = 0; j < slots.size(); ++j)
                found = found || slots[j] == i;
            if (!found)
                return false;
        }
        return true;
    }();
    static_assert(continuous, "ORM_QUERY_PARAMETER_INDEX: parameter indices must be continuous from zero");
    static_assert(sizeof...(Args) == count, "ORM_QUERY_PARAMETER_COUNT: wrong number of bound arguments");
    constexpr bool typesMatch = []<typename... P>(std::tuple<P...>*) consteval
    { return (SlotTypesCompatible<P, Slots>::value && ...); }(static_cast<Slots*>(nullptr));
    static_assert(typesMatch, "ORM_QUERY_PARAMETER_TYPE: one index must declare exactly one type");
    if constexpr (continuous && typesMatch && sizeof...(Args) == count)
    {
        using Values = std::tuple<Args...>;
        constexpr bool compatible = []<std::size_t... I>(std::index_sequence<I...>) consteval
        {
            return (parameterCompatible<typename std::tuple_element_t<I, Slots>::Value,
                                        std::tuple_element_t<std::tuple_element_t<I, Slots>::index, Values>>() &&
                    ...);
        }(std::make_index_sequence<std::tuple_size_v<Slots>>{});
        static_assert(compatible, "ORM_QUERY_PARAMETER_VALUE: bound argument cannot safely fit its declared type");
    }
}
} // namespace orm::query::detail

namespace orm::query
{
export
{

    template <typename T, std::size_t Index>
    class Parameter
    {
    public:
        using Value = T;
        using ParameterTypes = std::tuple<Parameter>;
        inline static constexpr std::size_t index = Index;
        inline static constexpr std::array<std::size_t, 1> parameterSlots{Index};

    private:
        friend struct detail::TypedAccess;
        constexpr Parameter() = default;
    };
    template <typename T, std::size_t Index>
    constexpr auto param() -> Parameter<T, Index>
    {
        return detail::TypedAccess::make<Parameter<T, Index>>();
    }
}
} // namespace orm::query

// query/Expression.hpp
namespace orm::query::detail
{
template <ComparisonOperator Op, typename S, typename T>
constexpr auto comparison(const S& source, T&& value);
}

namespace orm::query
{
export
{

    template <typename Owner, bool WriteSafe, bool ContainsCollection>
    class TypedPredicate;
    template <typename Owner>
    class TypedAggregatePredicate;

    enum class ExprKind
    {
        Column,
        Comparison,
        Null,
        List,
        Between,
        Logical,
        Not,
        Collection,
        Aggregate,
        Order,
        Projection,
        DynamicPredicate,
        DynamicAggregatePredicate,
        DynamicAggregate,
        DynamicOrder,
        DynamicProjection,
    };

    template <ExprKind Kind, typename Meta, typename... Children>
    class Expression : public Meta
    {
    public:
        inline static constexpr ExprKind kind = Kind;
        using ParameterTypes = detail::ConcatTuples<detail::ParameterTypes<Children>...>;
        inline static constexpr auto parameterSlots = detail::SlotIndices<ParameterTypes>::value;

        auto dynamic() const
            requires detail::ORM_QUERY_UNBOUND_PARAMETER<Expression>
        {
            return detail::TypedAccess::make<typename Meta::Dynamic>(runtime());
        }

        template <typename T>
            requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T>
        constexpr auto operator==(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::Equal>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T>
        constexpr auto operator!=(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::NotEqual>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T> &&
                    detail::ORM_QUERY_ORDERABLE<typename Meta::Value>
        constexpr auto operator>(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::Greater>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T> &&
                    detail::ORM_QUERY_ORDERABLE<typename Meta::Value>
        constexpr auto operator>=(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::GreaterOrEqual>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T> &&
                    detail::ORM_QUERY_ORDERABLE<typename Meta::Value>
        constexpr auto operator<(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::Less>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T> &&
                    detail::ORM_QUERY_ORDERABLE<typename Meta::Value>
        constexpr auto operator<=(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::LessOrEqual>(*this, std::forward<T>(value));
        }

    private:
        friend struct detail::TypedAccess;
        constexpr explicit Expression(Children... childrenInit) : children{std::move(childrenInit)...} {}
        auto runtime() const
            requires detail::ORM_QUERY_UNBOUND_PARAMETER<Expression>
        {
            return runtime(std::tuple<>{});
        }
        template <typename Args>
        auto runtime(const Args& args) const
        {
            return Meta::render(children, args);
        }
        std::tuple<Children...> children;
    };
}
} // namespace orm::query

namespace orm::query::detail
{
template <typename E>
struct IsExpression : std::false_type
{
};
template <ExprKind K, typename M, typename... C>
struct IsExpression<Expression<K, M, C...>> : std::true_type
{
};
template <typename E>
inline constexpr bool isExpression = IsExpression<std::remove_cvref_t<E>>::value;
template <typename E>
inline constexpr bool staticSqlEligible = []
{
    if constexpr (requires { E::staticSqlEligible; })
        return E::staticSqlEligible;
    else
        return false;
}();
template <ExprKind K, typename Meta, typename... Children>
constexpr auto makeExpression(Children&&... children)
{
    using E = Expression<K, Meta, std::remove_cvref_t<Children>...>;
    return TypedAccess::make<E>(std::forward<Children>(children)...);
}
template <typename E, typename Args>
auto bindExpression(const E& expression, const Args& args)
{
    if constexpr (isExpression<E>)
        return TypedAccess::make<typename E::Dynamic>(TypedAccess::erase(expression, args));
    else
        return expression;
}
template <typename E, typename Args, typename Callback>
auto visitValues(const E& expression, const Args& args, Callback&& callback) -> void
{
    if constexpr (isExpression<E>)
        E::visit(TypedAccess::children(expression), args, callback);
}
template <typename M, bool W = true, bool C = false>
struct ExpressionMeta
{
    using Model = M;
    using Value = void;
    inline static constexpr bool writeSafe = W;
    inline static constexpr bool containsCollection = C;
    inline static constexpr bool nullable = false;
    inline static constexpr bool isPredicate = false;
    inline static constexpr bool isAggregatePredicate = false;
    inline static constexpr bool isAggregate = false;
    inline static constexpr bool isColumn = false;
    inline static constexpr bool isOrder = false;
    inline static constexpr bool isProjection = false;
};
template <typename S, ComparisonOperator Op>
struct ComparisonMeta : ExpressionMeta<typename S::Model, S::writeSafe, S::containsCollection>
{
    using Source = S;
    using Dynamic = std::conditional_t<S::isAggregate, TypedAggregatePredicate<typename S::Model>,
                                       TypedPredicate<typename S::Model, S::writeSafe, S::containsCollection>>;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isPredicate = !S::isAggregate;
    inline static constexpr bool isAggregatePredicate = S::isAggregate;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<S>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        const auto source = TypedAccess::erase(std::get<0>(children), args);
        const auto value = typedValue<typename S::Value>(resolveValue(std::get<1>(children), args));
        if constexpr (S::isAggregate)
        {
            if constexpr (Op == ComparisonOperator::Equal)
                return source == value;
            else if constexpr (Op == ComparisonOperator::NotEqual)
                return source != value;
            else if constexpr (Op == ComparisonOperator::Greater)
                return source > value;
            else if constexpr (Op == ComparisonOperator::GreaterOrEqual)
                return source >= value;
            else if constexpr (Op == ComparisonOperator::Less)
                return source < value;
            else
                return source <= value;
        }
        else
            return Predicate{PredicateNode{ComparisonExpression{source, Op, value}}};
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        callback(typedValue<typename S::Value>(resolveValue(std::get<1>(children), args)));
    }
};
template <ComparisonOperator Op, typename S, typename T>
constexpr auto comparison(const S& source, T&& value)
{
    return makeExpression<ExprKind::Comparison, ComparisonMeta<S, Op>>(source, captureValue(std::forward<T>(value)));
}
template <typename S, NullOperator Op>
struct NullMeta : ExpressionMeta<typename S::Model, S::writeSafe, S::containsCollection>
{
    using Source = S;
    using Dynamic = TypedPredicate<typename S::Model, S::writeSafe, S::containsCollection>;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isPredicate = true;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<S>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return Predicate{PredicateNode{NullExpression{TypedAccess::erase(std::get<0>(children), args), Op}}};
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children&, const Args&, Callback&) -> void
    {
    }
};
template <typename S, BetweenOperator Op>
struct BetweenMeta : ExpressionMeta<typename S::Model, S::writeSafe, S::containsCollection>
{
    using Source = S;
    using Dynamic = TypedPredicate<typename S::Model, S::writeSafe, S::containsCollection>;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isPredicate = true;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<S>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return Predicate{
            PredicateNode{BetweenExpression{TypedAccess::erase(std::get<0>(children), args), Op,
                                            typedValue<typename S::Value>(resolveValue(std::get<1>(children), args)),
                                            typedValue<typename S::Value>(resolveValue(std::get<2>(children), args))}}};
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        callback(typedValue<typename S::Value>(resolveValue(std::get<1>(children), args)));
        callback(typedValue<typename S::Value>(resolveValue(std::get<2>(children), args)));
    }
};
template <typename... V>
struct FixedValues
{
    std::tuple<V...> values;
    using ParameterTypes = ConcatTuples<detail::ParameterTypes<V>...>;
};
template <typename T>
struct ListTraits
{
    inline static constexpr bool fixed = false;
    inline static constexpr auto size = std::dynamic_extent;
};
template <typename... V>
struct ListTraits<FixedValues<V...>>
{
    inline static constexpr bool fixed = true;
    inline static constexpr std::size_t size = sizeof...(V);
};
template <typename V, std::size_t N, std::size_t I>
struct ListTraits<Parameter<std::array<V, N>, I>>
{
    inline static constexpr bool fixed = true;
    inline static constexpr std::size_t size = N;
};
template <typename S, ListOperator Op, typename Stored>
struct ListMeta : ExpressionMeta<typename S::Model, S::writeSafe, S::containsCollection>
{
    using Source = S;
    using Values = Stored;
    using Dynamic = TypedPredicate<typename S::Model, S::writeSafe, S::containsCollection>;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isFixedList = ListTraits<Stored>::fixed;
    inline static constexpr auto arity = ListTraits<Stored>::size;
    inline static constexpr bool isPredicate = true;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<S> && isFixedList;
    static_assert(!isFixedList || arity != 0, "ORM_QUERY_EMPTY_IN: IN requires at least one value");
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        const auto& stored = std::get<1>(children);
        if constexpr (requires { stored.values; })
            std::apply([&](const auto&... value)
                       { (callback(typedValue<typename S::Value>(resolveValue(value, args))), ...); }, stored.values);
        else
        {
            const auto& values = resolveValue(stored, args);
            if (values.empty())
                throw std::invalid_argument{"IN predicate requires at least one value"};
            for (const auto& value : values)
                callback(typedValue<typename S::Value>(
                    static_cast<typename std::remove_cvref_t<decltype(values)>::value_type>(value)));
        }
    }
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        std::vector<QueryValue> values;
        auto append = [&](QueryValue value) { values.push_back(std::move(value)); };
        visit(children, args, append);
        return Predicate{
            PredicateNode{ListExpression{TypedAccess::erase(std::get<0>(children), args), Op, std::move(values)}}};
    }
};
template <ListOperator Op, typename S, typename... V>
constexpr auto fixedList(const S& source, V&&... value)
{
    auto values = FixedValues<decltype(captureValue(std::forward<V>(value)))...>{
        std::tuple{captureValue(std::forward<V>(value))...}};
    return makeExpression<ExprKind::List, ListMeta<S, Op, decltype(values)>>(source, std::move(values));
}
template <ListOperator Op, typename S, typename Container>
constexpr auto containerList(const S& source, const Container& values)
{
    using C = std::remove_cvref_t<Container>;
    if constexpr (isParameter<C>)
        return makeExpression<ExprKind::List, ListMeta<S, Op, C>>(source, values);
    else if constexpr (ContainerTraits<C>::fixed)
        return [&]<std::size_t... I>(std::index_sequence<I...>)
        { return fixedList<Op>(source, values[I]...); }(std::make_index_sequence<ContainerTraits<C>::size>{});
    else
    {
        if (values.size() == 0)
            throw std::invalid_argument{"IN predicate requires at least one value"};
        using V = decltype(captureValue(std::declval<typename C::value_type>()));
        std::vector<V> owned;
        owned.reserve(values.size());
        for (const auto& value : values)
            owned.push_back(captureValue(static_cast<typename C::value_type>(value)));
        return makeExpression<ExprKind::List, ListMeta<S, Op, decltype(owned)>>(source, std::move(owned));
    }
}
template <typename L, typename R, LogicalOperator Op>
struct LogicalMeta
    : ExpressionMeta<typename L::Model, L::writeSafe && R::writeSafe, L::containsCollection || R::containsCollection>
{
    using Left = L;
    using Right = R;
    using Dynamic = std::conditional_t<L::isAggregatePredicate, TypedAggregatePredicate<typename L::Model>,
                                       TypedPredicate<typename L::Model, L::writeSafe && R::writeSafe,
                                                      L::containsCollection || R::containsCollection>>;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isPredicate = L::isPredicate;
    inline static constexpr bool isAggregatePredicate = L::isAggregatePredicate;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<L> && detail::staticSqlEligible<R>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        const auto left = TypedAccess::erase(std::get<0>(children), args);
        const auto right = TypedAccess::erase(std::get<1>(children), args);
        if constexpr (Op == LogicalOperator::And)
            return left && right;
        else
            return left || right;
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        visitValues(std::get<0>(children), args, callback);
        visitValues(std::get<1>(children), args, callback);
    }
};
template <typename E>
struct NotMeta : ExpressionMeta<typename E::Model, E::writeSafe, E::containsCollection>
{
    using Child = E;
    using Dynamic = std::conditional_t<E::isAggregatePredicate, TypedAggregatePredicate<typename E::Model>,
                                       TypedPredicate<typename E::Model, E::writeSafe, E::containsCollection>>;
    inline static constexpr bool isPredicate = E::isPredicate;
    inline static constexpr bool isAggregatePredicate = E::isAggregatePredicate;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<E>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return !TypedAccess::erase(std::get<0>(children), args);
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        visitValues(std::get<0>(children), args, callback);
    }
};
} // namespace orm::query::detail

// query/TypedColumn.hpp
namespace orm::query::detail
{
template <auto... Members>
struct ColumnTraits;
template <auto Member>
struct ColumnTraits<Member>
{
    static_assert(std::is_member_object_pointer_v<decltype(Member)>,
                  "ORM_QUERY_MEMBER: expected a data member pointer");
    using Model = model::detail::member_owner_t<Member>;
    using Field = model::detail::member_value_t<Member>;
    using Value = scalar_t<Field>;
    static_assert(Member != nullptr && model::detail::memberExists<Model, Member>(),
                  "ORM_QUERY_MEMBER: expected a reflected aggregate member");
    static_assert(supportedScalar<Value>, "ORM_QUERY_COLUMN: terminal field must be a supported scalar");
    inline static constexpr bool nullable = model::isNullable<Field>;
    inline static constexpr bool writeSafe = true;
    inline static constexpr std::array pathParts{model::detail::reflectedMemberNameStorage<Member>.view()};
    static auto path() -> std::string
    {
        return std::string{pathParts[0]};
    }
};
template <auto Relation, auto Member>
struct ColumnTraits<Relation, Member>
{
    static_assert(std::is_member_object_pointer_v<decltype(Relation)> &&
                      std::is_member_object_pointer_v<decltype(Member)>,
                  "ORM_QUERY_MEMBER: expected data member pointers");
    using Model = model::detail::member_owner_t<Relation>;
    using RelationField = model::detail::member_value_t<Relation>;
    using Target = scalar_t<RelationField>;
    using Terminal = ColumnTraits<Member>;
    using Value = typename Terminal::Value;
    static_assert(Relation != nullptr && model::detail::memberExists<Model, Relation>(),
                  "ORM_QUERY_MEMBER: expected a reflected relation member");
    static_assert(std::same_as<Target, typename Terminal::Model> && std::is_aggregate_v<Target> &&
                      !is_relation_collection_v<RelationField>,
                  "ORM_QUERY_PATH: adjacent owners must follow a to-one relation");
    inline static constexpr bool nullable = model::isNullable<RelationField> || Terminal::nullable;
    inline static constexpr bool writeSafe =
        model::detail::isPrimaryKey<Target>(model::detail::reflectedMemberNameStorage<Member>.view());
    inline static constexpr std::array pathParts{model::detail::reflectedMemberNameStorage<Relation>.view(),
                                                 model::detail::reflectedMemberNameStorage<Member>.view()};
    static auto path() -> std::string
    {
        return std::string{pathParts[0]} + "." + std::string{pathParts[1]};
    }
};
template <auto Member>
consteval auto collectionMember() -> bool
{
    if constexpr (!std::is_member_object_pointer_v<decltype(Member)>)
        return false;
    else
        return Member != nullptr && is_relation_collection_v<model::detail::member_value_t<Member>>;
}
template <auto Member>
concept ORM_QUERY_COLLECTION = collectionMember<Member>();
template <bool C>
concept ORM_QUERY_NO_NESTED_COLLECTION = !C;
template <auto Member>
struct CollectionTraits
{
    using Model = model::detail::member_owner_t<Member>;
    using Target = relation_target_t<model::detail::member_value_t<Member>>;
    static_assert(model::detail::memberExists<Model, Member>(), "ORM_QUERY_MEMBER: expected a reflected collection");
    static auto name() -> std::string
    {
        return std::string{model::detail::reflectedMemberNameStorage<Member>.view()};
    }
};
} // namespace orm::query::detail

namespace orm::query
{
export
{

    template <typename Owner, bool WriteSafe = true, bool ContainsCollection = false>
    class TypedPredicate
    {
    public:
        using Model = Owner;
        using ParameterTypes = std::tuple<>;
        inline static constexpr auto kind = ExprKind::DynamicPredicate;
        inline static constexpr bool isPredicate = true;
        inline static constexpr bool isAggregatePredicate = false;
        inline static constexpr bool isAggregate = false;
        inline static constexpr bool writeSafe = WriteSafe;
        inline static constexpr bool containsCollection = ContainsCollection;
        inline static constexpr bool staticSqlEligible = false;
        template <typename E>
            requires detail::isExpression<E> && E::isPredicate && detail::ORM_QUERY_MODEL<E, Owner> &&
                     (E::writeSafe == WriteSafe) &&
                     (E::containsCollection == ContainsCollection) && detail::ORM_QUERY_UNBOUND_PARAMETER<E>
        TypedPredicate(const E& expression) : data{detail::erase(expression)}
        {
        }
        template <typename E>
            requires detail::isExpression<E> && E::isPredicate && detail::ORM_QUERY_MODEL<E, Owner> &&
                         (E::writeSafe == WriteSafe) &&
                         (E::containsCollection == ContainsCollection) && detail::ORM_QUERY_UNBOUND_PARAMETER<E>
        auto operator=(const E& expression) -> TypedPredicate&
        {
            data = detail::erase(expression);
            return *this;
        }
        auto dynamic() const -> TypedPredicate
        {
            return *this;
        }

    private:
        friend struct detail::TypedAccess;
        explicit TypedPredicate(detail::Predicate value) : data{std::move(value)} {}
        auto runtime() const -> detail::Predicate
        {
            return data;
        }
        detail::Predicate data;
    };

    template <auto... Members>
    class TypedColumn
    {
        static_assert(sizeof...(Members) >= 1 && sizeof...(Members) <= 2,
                      "ORM_QUERY_PATH: columns support a direct member or one to-one relation level");
        using Traits = detail::ColumnTraits<Members...>;

    public:
        using Model = typename Traits::Model;
        using Value = typename Traits::Value;
        using ParameterTypes = std::tuple<>;
        inline static constexpr auto kind = ExprKind::Column;
        inline static constexpr auto pathParts = Traits::pathParts;
        inline static constexpr bool isColumn = true;
        inline static constexpr bool isAggregate = false;
        inline static constexpr bool nullable = Traits::nullable;
        inline static constexpr bool writeSafe = Traits::writeSafe;
        inline static constexpr bool containsCollection = false;
        inline static constexpr bool staticSqlEligible = true;
        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
        constexpr auto operator==(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::Equal>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
        constexpr auto operator!=(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::NotEqual>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
        constexpr auto operator>(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::Greater>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
        constexpr auto operator>=(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::GreaterOrEqual>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
        constexpr auto operator<(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::Less>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
        constexpr auto operator<=(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::LessOrEqual>(*this, std::forward<T>(value));
        }
        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_STRING<Value>
        constexpr auto like(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::Like>(*this, std::forward<T>(value));
        }
        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_STRING<Value>
        constexpr auto notLike(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::NotLike>(*this, std::forward<T>(value));
        }
        constexpr auto isNull() const
            requires detail::ORM_QUERY_NULLABLE<nullable>
        {
            return detail::makeExpression<ExprKind::Null, detail::NullMeta<TypedColumn, detail::NullOperator::IsNull>>(
                *this);
        }
        constexpr auto isNotNull() const
            requires detail::ORM_QUERY_NULLABLE<nullable>
        {
            return detail::makeExpression<ExprKind::Null,
                                          detail::NullMeta<TypedColumn, detail::NullOperator::IsNotNull>>(*this);
        }
        constexpr auto operator==(std::nullptr_t) const
            requires detail::ORM_QUERY_NULLABLE<nullable>
        {
            return isNull();
        }
        constexpr auto operator!=(std::nullptr_t) const
            requires detail::ORM_QUERY_NULLABLE<nullable>
        {
            return isNotNull();
        }
        constexpr auto operator==(std::nullopt_t) const
            requires detail::ORM_QUERY_NULLABLE<nullable>
        {
            return isNull();
        }
        constexpr auto operator!=(std::nullopt_t) const
            requires detail::ORM_QUERY_NULLABLE<nullable>
        {
            return isNotNull();
        }
        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
        auto in(std::initializer_list<T> values) const
        {
            return detail::containerList<detail::ListOperator::In>(*this, values);
        }
        template <typename T>
            requires detail::ContainerTraits<detail::parameter_value_t<T>>::isContainer &&
                     detail::ORM_QUERY_VALUE_OR_PARAMETER<
                         Value, typename detail::ContainerTraits<detail::parameter_value_t<T>>::Value>
        constexpr auto in(const T& values) const
        {
            return detail::containerList<detail::ListOperator::In>(*this, values);
        }
        template <typename T, std::size_t N>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
        constexpr auto in(const T (&values)[N]) const
        {
            return [&]<std::size_t... I>(std::index_sequence<I...>)
            { return detail::fixedList<detail::ListOperator::In>(*this, values[I]...); }(std::make_index_sequence<N>{});
        }
        template <typename... T>
            requires(sizeof...(T) > 0) && (detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && ...)
        constexpr auto in(T&&... values) const
        {
            return detail::fixedList<detail::ListOperator::In>(*this, std::forward<T>(values)...);
        }

        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
        auto notIn(std::initializer_list<T> values) const
        {
            return detail::containerList<detail::ListOperator::NotIn>(*this, values);
        }
        template <typename T>
            requires detail::ContainerTraits<detail::parameter_value_t<T>>::isContainer &&
                     detail::ORM_QUERY_VALUE_OR_PARAMETER<
                         Value, typename detail::ContainerTraits<detail::parameter_value_t<T>>::Value>
        constexpr auto notIn(const T& values) const
        {
            return detail::containerList<detail::ListOperator::NotIn>(*this, values);
        }
        template <typename T, std::size_t N>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
        constexpr auto notIn(const T (&values)[N]) const
        {
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return detail::fixedList<detail::ListOperator::NotIn>(*this, values[I]...);
            }(std::make_index_sequence<N>{});
        }
        template <typename... T>
            requires(sizeof...(T) > 0) && (detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && ...)
        constexpr auto notIn(T&&... values) const
        {
            return detail::fixedList<detail::ListOperator::NotIn>(*this, std::forward<T>(values)...);
        }
        template <typename L, typename U>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, L> && detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, U> &&
                     detail::ORM_QUERY_ORDERABLE<Value>
        constexpr auto between(L&& lower, U&& upper) const
        {
            return detail::makeExpression<ExprKind::Between,
                                          detail::BetweenMeta<TypedColumn, detail::BetweenOperator::Between>>(
                *this, detail::captureValue(std::forward<L>(lower)), detail::captureValue(std::forward<U>(upper)));
        }
        template <typename L, typename U>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, L> && detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, U> &&
                     detail::ORM_QUERY_ORDERABLE<Value>
        constexpr auto notBetween(L&& lower, U&& upper) const
        {
            return detail::makeExpression<ExprKind::Between,
                                          detail::BetweenMeta<TypedColumn, detail::BetweenOperator::NotBetween>>(
                *this, detail::captureValue(std::forward<L>(lower)), detail::captureValue(std::forward<U>(upper)));
        }

    private:
        friend struct detail::TypedAccess;
        constexpr TypedColumn() = default;
        auto runtime() const -> detail::Column
        {
            return detail::col(Traits::path());
        }
    };
    template <auto... Members>
        requires detail::ORM_QUERY_MEMBER<Members...>
    constexpr auto col() -> TypedColumn<Members...>
    {
        return detail::TypedAccess::make<TypedColumn<Members...>>();
    }
}
} // namespace orm::query

namespace orm::query::detail
{
template <typename E>
concept ORM_QUERY_WRITE_SAFE = E::writeSafe;
template <typename E>
struct IsTypedPredicate : std::false_type
{
};
template <typename M, bool W, bool C>
struct IsTypedPredicate<TypedPredicate<M, W, C>> : std::true_type
{
};
template <typename E>
struct IsTypedColumn : std::false_type
{
};
template <auto... Members>
struct IsTypedColumn<TypedColumn<Members...>> : std::true_type
{
};
template <typename E>
concept PredicateExpression =
    (IsTypedPredicate<std::remove_cvref_t<E>>::value || isExpression<E>) && std::remove_cvref_t<E>::isPredicate;
template <typename E, typename M>
concept AnyPredicateFor = PredicateExpression<E> && ORM_QUERY_MODEL<std::remove_cvref_t<E>, M>;
template <typename E, typename M>
concept PredicateFor = AnyPredicateFor<E, M> && ORM_QUERY_UNBOUND_PARAMETER<E>;
template <typename E, typename M>
concept PlanPredicateFor = AnyPredicateFor<E, M>;
template <typename E, typename M>
concept ColumnFor = IsTypedColumn<std::remove_cvref_t<E>>::value && ORM_QUERY_MODEL<std::remove_cvref_t<E>, M>;
template <typename M, typename... Columns>
concept ORM_QUERY_MODEL_COLUMNS = (ColumnFor<Columns, M> && ...);
template <typename E>
struct IsTypedAggregatePredicate : std::false_type
{
};
template <typename E>
concept LogicalPredicate =
    PredicateExpression<E> || ((isExpression<E> || IsTypedAggregatePredicate<std::remove_cvref_t<E>>::value) &&
                               std::remove_cvref_t<E>::isAggregatePredicate);
template <auto Member, CollectionOperator Op, typename C>
struct CollectionMeta : ExpressionMeta<typename CollectionTraits<Member>::Model, true, true>
{
    using Child = C;
    using Target = typename CollectionTraits<Member>::Target;
    using Dynamic = TypedPredicate<typename CollectionTraits<Member>::Model, true, true>;
    inline static constexpr auto member = Member;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isPredicate = true;
    inline static constexpr bool staticSqlEligible = std::same_as<C, void> || detail::staticSqlEligible<C>;
    inline static constexpr auto relationName = model::detail::reflectedMemberNameStorage<Member>.view();
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        if constexpr (Op == CollectionOperator::Exists)
            return detail::exists(std::string{relationName});
        else if constexpr (Op == CollectionOperator::Any)
            return detail::any(std::string{relationName}, TypedAccess::erase(std::get<0>(children), args));
        else
            return detail::none(std::string{relationName}, TypedAccess::erase(std::get<0>(children), args));
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        if constexpr (!std::same_as<C, void>)
            visitValues(std::get<0>(children), args, callback);
    }
};
} // namespace orm::query::detail

namespace orm::query
{
export
{

    template <typename L, typename R>
        requires detail::LogicalPredicate<L> && detail::LogicalPredicate<R> &&
                 detail::ORM_QUERY_MODEL<std::remove_cvref_t<R>, typename std::remove_cvref_t<L>::Model> &&
                 (std::remove_cvref_t<L>::isAggregatePredicate == std::remove_cvref_t<R>::isAggregatePredicate)
    constexpr auto operator&&(const L& left, const R& right)
    {
        return detail::makeExpression<ExprKind::Logical, detail::LogicalMeta<L, R, detail::LogicalOperator::And>>(
            left, right);
    }
    template <typename L, typename R>
        requires detail::LogicalPredicate<L> && detail::LogicalPredicate<R> &&
                 detail::ORM_QUERY_MODEL<std::remove_cvref_t<R>, typename std::remove_cvref_t<L>::Model> &&
                 (std::remove_cvref_t<L>::isAggregatePredicate == std::remove_cvref_t<R>::isAggregatePredicate)
    constexpr auto operator||(const L& left, const R& right)
    {
        return detail::makeExpression<ExprKind::Logical, detail::LogicalMeta<L, R, detail::LogicalOperator::Or>>(left,
                                                                                                                 right);
    }
    template <typename E>
        requires detail::LogicalPredicate<E>
    constexpr auto operator!(const E& value)
    {
        return detail::makeExpression<ExprKind::Not, detail::NotMeta<E>>(value);
    }
    template <auto Member, typename E>
        requires detail::ORM_QUERY_COLLECTION<Member> &&
                 detail::AnyPredicateFor<E, typename detail::CollectionTraits<Member>::Target> &&
                 detail::ORM_QUERY_NO_NESTED_COLLECTION<E::containsCollection>
    constexpr auto any(const E& predicate)
    {
        return detail::makeExpression<ExprKind::Collection,
                                      detail::CollectionMeta<Member, detail::CollectionOperator::Any, E>>(predicate);
    }
    template <auto Member, typename E>
        requires detail::ORM_QUERY_COLLECTION<Member> &&
                 detail::AnyPredicateFor<E, typename detail::CollectionTraits<Member>::Target> &&
                 detail::ORM_QUERY_NO_NESTED_COLLECTION<E::containsCollection>
    constexpr auto none(const E& predicate)
    {
        return detail::makeExpression<ExprKind::Collection,
                                      detail::CollectionMeta<Member, detail::CollectionOperator::None, E>>(predicate);
    }
    template <auto Member>
        requires detail::ORM_QUERY_COLLECTION<Member>
    constexpr auto exists()
    {
        return detail::makeExpression<ExprKind::Collection,
                                      detail::CollectionMeta<Member, detail::CollectionOperator::Exists, void>>();
    }
    template <typename M, typename... P>
        requires(std::same_as<std::remove_cvref_t<P>, QueryParameter> && ...)
    auto raw(std::string sql, P... params) -> TypedPredicate<M>
    {
        return detail::TypedAccess::make<TypedPredicate<M>>(detail::raw(std::move(sql), std::move(params)...));
    }
}
} // namespace orm::query

// query/Predicate.hpp

// query/Aggregate.hpp
namespace orm::query::detail
{
class AggregatePredicate;
struct AggregatePredicateNode;

using AggregatePredicateNodePtr = std::shared_ptr<const AggregatePredicateNode>;

enum class AggregateFunction
{
    Count,
    CountAll,
    Sum,
    Avg,
    Min,
    Max,
};

struct AggregateExpression
{
    AggregateFunction function;
    std::optional<Column> column = std::nullopt;

    template <typename T>
    auto operator==(T value) const -> AggregatePredicate;

    template <typename T>
    auto operator!=(T value) const -> AggregatePredicate;

    template <typename T>
    auto operator>(T value) const -> AggregatePredicate;

    template <typename T>
    auto operator>=(T value) const -> AggregatePredicate;

    template <typename T>
    auto operator<(T value) const -> AggregatePredicate;

    template <typename T>
    auto operator<=(T value) const -> AggregatePredicate;

private:
    auto compare(ComparisonOperator comparisonOperator, QueryValue value) const -> AggregatePredicate;
};

struct AggregateComparisonExpression
{
    AggregateExpression aggregate;
    ComparisonOperator comparisonOperator;
    QueryValue value;
};

struct AggregateLogicalExpression
{
    AggregatePredicateNodePtr left;
    LogicalOperator logicalOperator;
    AggregatePredicateNodePtr right;
};

struct AggregateNotExpression
{
    AggregatePredicateNodePtr predicate;
};

struct AggregatePredicateNode
{
    using Expression = std::variant<AggregateComparisonExpression, AggregateLogicalExpression, AggregateNotExpression>;

    Expression expression;
};

class AggregatePredicate
{
public:
    explicit AggregatePredicate(AggregatePredicateNode predicateNode)
        : node{std::make_shared<AggregatePredicateNode>(std::move(predicateNode))}
    {
    }

    [[nodiscard]] auto getNode() const -> const AggregatePredicateNode&
    {
        return *node;
    }

private:
    AggregatePredicateNodePtr node;

    friend auto operator&&(const AggregatePredicate& left, const AggregatePredicate& right) -> AggregatePredicate;
    friend auto operator||(const AggregatePredicate& left, const AggregatePredicate& right) -> AggregatePredicate;
    friend auto operator!(const AggregatePredicate& predicate) -> AggregatePredicate;
};

inline auto count(Column sourceColumn) -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::Count, .column = std::move(sourceColumn)};
}

inline auto countAll() -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::CountAll};
}

inline auto sum(Column sourceColumn) -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::Sum, .column = std::move(sourceColumn)};
}

inline auto avg(Column sourceColumn) -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::Avg, .column = std::move(sourceColumn)};
}

inline auto min(Column sourceColumn) -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::Min, .column = std::move(sourceColumn)};
}

inline auto max(Column sourceColumn) -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::Max, .column = std::move(sourceColumn)};
}

template <typename T>
auto AggregateExpression::operator==(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::Equal, QueryValue{std::move(value)});
}

template <typename T>
auto AggregateExpression::operator!=(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::NotEqual, QueryValue{std::move(value)});
}

template <typename T>
auto AggregateExpression::operator>(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::Greater, QueryValue{std::move(value)});
}

template <typename T>
auto AggregateExpression::operator>=(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::GreaterOrEqual, QueryValue{std::move(value)});
}

template <typename T>
auto AggregateExpression::operator<(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::Less, QueryValue{std::move(value)});
}

template <typename T>
auto AggregateExpression::operator<=(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::LessOrEqual, QueryValue{std::move(value)});
}

inline auto AggregateExpression::compare(ComparisonOperator comparisonOperator,
                                         QueryValue value) const -> AggregatePredicate
{
    return AggregatePredicate{AggregatePredicateNode{
        AggregateComparisonExpression{.aggregate = *this, .comparisonOperator = comparisonOperator, .value = value}}};
}

inline auto operator&&(const AggregatePredicate& left, const AggregatePredicate& right) -> AggregatePredicate
{
    return AggregatePredicate{AggregatePredicateNode{
        AggregateLogicalExpression{.left = left.node, .logicalOperator = LogicalOperator::And, .right = right.node}}};
}

inline auto operator||(const AggregatePredicate& left, const AggregatePredicate& right) -> AggregatePredicate
{
    return AggregatePredicate{AggregatePredicateNode{
        AggregateLogicalExpression{.left = left.node, .logicalOperator = LogicalOperator::Or, .right = right.node}}};
}

inline auto operator!(const AggregatePredicate& predicate) -> AggregatePredicate
{
    return AggregatePredicate{AggregatePredicateNode{AggregateNotExpression{.predicate = predicate.node}}};
}
} // namespace orm::query::detail

namespace orm::query
{
export
{

    template <typename Owner>
    class TypedAggregatePredicate
    {
    public:
        using Model = Owner;
        using ParameterTypes = std::tuple<>;
        using DynamicAggregateMarker = void;
        inline static constexpr auto kind = ExprKind::DynamicAggregatePredicate;
        inline static constexpr bool isPredicate = false;
        inline static constexpr bool isAggregatePredicate = true;
        inline static constexpr bool writeSafe = false;
        inline static constexpr bool containsCollection = false;
        inline static constexpr bool staticSqlEligible = false;
        template <typename E>
            requires detail::isExpression<E> && E::isAggregatePredicate && detail::ORM_QUERY_MODEL<E, Owner> &&
                     detail::ORM_QUERY_UNBOUND_PARAMETER<E>
        TypedAggregatePredicate(const E& expression) : data{detail::erase(expression)}
        {
        }
        template <typename E>
            requires detail::isExpression<E> && E::isAggregatePredicate && detail::ORM_QUERY_MODEL<E, Owner> &&
                         detail::ORM_QUERY_UNBOUND_PARAMETER<E>
        auto operator=(const E& expression) -> TypedAggregatePredicate&
        {
            data = detail::erase(expression);
            return *this;
        }
        auto dynamic() const -> TypedAggregatePredicate
        {
            return *this;
        }

    private:
        friend struct detail::TypedAccess;
        explicit TypedAggregatePredicate(detail::AggregatePredicate value) : data{std::move(value)} {}
        auto runtime() const -> detail::AggregatePredicate
        {
            return data;
        }
        detail::AggregatePredicate data;
    };
    template <typename Owner, typename ResultValue, bool Nullable>
    class TypedAggregate
    {
    public:
        using Model = Owner;
        using Value = ResultValue;
        using ParameterTypes = std::tuple<>;
        inline static constexpr auto kind = ExprKind::DynamicAggregate;
        inline static constexpr bool nullable = Nullable;
        inline static constexpr bool isAggregate = true;
        inline static constexpr bool writeSafe = false;
        inline static constexpr bool containsCollection = false;
        inline static constexpr bool staticSqlEligible = false;
        template <typename E>
            requires detail::isExpression<E> && E::isAggregate && detail::ORM_QUERY_MODEL<E, Owner> &&
                     std::same_as<typename E::Value, Value> &&
                     (E::nullable == Nullable) && detail::ORM_QUERY_UNBOUND_PARAMETER<E>
        TypedAggregate(const E& expression) : data{detail::erase(expression)}
        {
        }
        template <typename E>
            requires detail::isExpression<E> && E::isAggregate && detail::ORM_QUERY_MODEL<E, Owner> &&
                         std::same_as<typename E::Value, Value> &&
                         (E::nullable == Nullable) && detail::ORM_QUERY_UNBOUND_PARAMETER<E>
        auto operator=(const E& expression) -> TypedAggregate&
        {
            data = detail::erase(expression);
            return *this;
        }
        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
        constexpr auto operator==(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::Equal>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
        constexpr auto operator!=(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::NotEqual>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
        constexpr auto operator>(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::Greater>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
        constexpr auto operator>=(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::GreaterOrEqual>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
        constexpr auto operator<(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::Less>(*this, std::forward<T>(value));
        }

        template <typename T>
            requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
        constexpr auto operator<=(T&& value) const
        {
            return detail::comparison<detail::ComparisonOperator::LessOrEqual>(*this, std::forward<T>(value));
        }
        auto dynamic() const -> TypedAggregate
        {
            return *this;
        }

    private:
        friend struct detail::TypedAccess;
        explicit TypedAggregate(detail::AggregateExpression value) : data{std::move(value)} {}
        auto runtime() const -> detail::AggregateExpression
        {
            return data;
        }
        detail::AggregateExpression data;
    };
}
} // namespace orm::query

namespace orm::query::detail
{
template <AggregateFunction Function, typename C, typename M, typename V, bool N>
struct AggregateMeta : ExpressionMeta<M, false>
{
    using Source = C;
    using Value = V;
    using Dynamic = TypedAggregate<M, V, N>;
    inline static constexpr auto function = Function;
    inline static constexpr bool nullable = N;
    inline static constexpr bool isAggregate = true;
    inline static constexpr bool staticSqlEligible = true;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        if constexpr (Function == AggregateFunction::CountAll)
            return AggregateExpression{.function = Function};
        else
            return AggregateExpression{.function = Function, .column = TypedAccess::erase(std::get<0>(children), args)};
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children&, const Args&, Callback&) -> void
    {
    }
};
template <typename M>
struct IsTypedAggregatePredicate<TypedAggregatePredicate<M>> : std::true_type
{
};
template <typename E>
struct IsTypedAggregate : std::false_type
{
};
template <typename M, typename V, bool N>
struct IsTypedAggregate<TypedAggregate<M, V, N>> : std::true_type
{
};
template <typename E>
concept AggregateExpressionType =
    (IsTypedAggregate<std::remove_cvref_t<E>>::value || isExpression<E>) && std::remove_cvref_t<E>::isAggregate;
template <typename E, typename M>
concept AnyAggregatePredicateFor =
    (IsTypedAggregatePredicate<std::remove_cvref_t<E>>::value || isExpression<E>) &&
    std::remove_cvref_t<E>::isAggregatePredicate && ORM_QUERY_MODEL<std::remove_cvref_t<E>, M>;
template <typename E, typename M>
concept AggregatePredicateFor = AnyAggregatePredicateFor<E, M> && ORM_QUERY_UNBOUND_PARAMETER<E>;
template <typename E, typename M>
concept PlanAggregatePredicateFor = AnyAggregatePredicateFor<E, M>;
} // namespace orm::query::detail

namespace orm::query
{
export
{

    template <auto... Members>
    constexpr auto count(TypedColumn<Members...> column)
    {
        using C = TypedColumn<Members...>;
        return detail::makeExpression<ExprKind::Aggregate, detail::AggregateMeta<detail::AggregateFunction::Count, C,
                                                                                 typename C::Model, long long, false>>(
            column);
    }
    template <typename Model>
    constexpr auto countAll()
    {
        return detail::makeExpression<ExprKind::Aggregate, detail::AggregateMeta<detail::AggregateFunction::CountAll,
                                                                                 void, Model, long long, false>>();
    }
    template <auto... Members>
        requires detail::ORM_QUERY_NUMERIC<typename TypedColumn<Members...>::Value>
    constexpr auto sum(TypedColumn<Members...> column)
    {
        using C = TypedColumn<Members...>;
        using V = std::conditional_t<std::is_integral_v<typename C::Value>, long long, double>;
        return detail::makeExpression<
            ExprKind::Aggregate, detail::AggregateMeta<detail::AggregateFunction::Sum, C, typename C::Model, V, true>>(
            column);
    }
    template <auto... Members>
        requires detail::ORM_QUERY_NUMERIC<typename TypedColumn<Members...>::Value>
    constexpr auto avg(TypedColumn<Members...> column)
    {
        using C = TypedColumn<Members...>;
        return detail::makeExpression<ExprKind::Aggregate, detail::AggregateMeta<detail::AggregateFunction::Avg, C,
                                                                                 typename C::Model, double, true>>(
            column);
    }
    template <auto... Members>
        requires detail::ORM_QUERY_ORDERABLE<typename TypedColumn<Members...>::Value>
    constexpr auto min(TypedColumn<Members...> column)
    {
        using C = TypedColumn<Members...>;
        return detail::makeExpression<
            ExprKind::Aggregate,
            detail::AggregateMeta<detail::AggregateFunction::Min, C, typename C::Model, typename C::Value, true>>(
            column);
    }
    template <auto... Members>
        requires detail::ORM_QUERY_ORDERABLE<typename TypedColumn<Members...>::Value>
    constexpr auto max(TypedColumn<Members...> column)
    {
        using C = TypedColumn<Members...>;
        return detail::makeExpression<
            ExprKind::Aggregate,
            detail::AggregateMeta<detail::AggregateFunction::Max, C, typename C::Model, typename C::Value, true>>(
            column);
    }
}
} // namespace orm::query

// query/OrderBy.hpp
namespace orm::query::detail
{
enum class OrderDirection
{
    Asc,
    Desc,
};

/**
 * @brief A single ORDER BY clause.
 */
struct OrderBy
{
    Column column{""};
    OrderDirection direction{OrderDirection::Asc};
    std::string rawSql{};
    bool isRaw{false};
};

/**
 * @brief Creates an ascending ORDER BY clause.
 */
inline auto asc(Column column) -> OrderBy
{
    return OrderBy{.column = std::move(column), .direction = OrderDirection::Asc};
}

/**
 * @brief Creates a descending ORDER BY clause.
 */
inline auto desc(Column column) -> OrderBy
{
    return OrderBy{.column = std::move(column), .direction = OrderDirection::Desc};
}

/**
 * @brief Creates a raw ORDER BY clause.
 */
inline auto rawOrder(std::string sql) -> OrderBy
{
    return OrderBy{.rawSql = std::move(sql), .isRaw = true};
}
} // namespace orm::query::detail

namespace orm::query
{
export
{

    template <typename Owner>
    class TypedOrderBy
    {
    public:
        using Model = Owner;
        using ParameterTypes = std::tuple<>;
        inline static constexpr auto kind = ExprKind::DynamicOrder;
        inline static constexpr bool isOrder = true;
        inline static constexpr bool staticSqlEligible = false;
        template <typename E>
            requires detail::isExpression<E> && E::isOrder && detail::ORM_QUERY_MODEL<E, Owner> &&
                     detail::ORM_QUERY_UNBOUND_PARAMETER<E>
        TypedOrderBy(const E& expression) : data{detail::erase(expression)}
        {
        }
        template <typename E>
            requires detail::isExpression<E> && E::isOrder && detail::ORM_QUERY_MODEL<E, Owner> &&
                         detail::ORM_QUERY_UNBOUND_PARAMETER<E>
        auto operator=(const E& expression) -> TypedOrderBy&
        {
            data = detail::erase(expression);
            return *this;
        }
        auto dynamic() const -> TypedOrderBy
        {
            return *this;
        }

    private:
        friend struct detail::TypedAccess;
        explicit TypedOrderBy(detail::OrderBy value) : data{std::move(value)} {}
        auto runtime() const -> detail::OrderBy
        {
            return data;
        }
        detail::OrderBy data;
    };
}
} // namespace orm::query

namespace orm::query::detail
{
template <typename C, OrderDirection Direction>
struct OrderMeta : ExpressionMeta<typename C::Model>
{
    using Source = C;
    using Dynamic = TypedOrderBy<typename C::Model>;
    inline static constexpr auto direction = Direction;
    inline static constexpr bool isOrder = true;
    inline static constexpr bool staticSqlEligible = true;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return OrderBy{.column = TypedAccess::erase(std::get<0>(children), args), .direction = Direction};
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children&, const Args&, Callback&) -> void
    {
    }
};
template <typename E>
struct IsTypedOrderBy : std::false_type
{
};
template <typename M>
struct IsTypedOrderBy<TypedOrderBy<M>> : std::true_type
{
};
template <typename E, typename M>
concept OrderFor =
    (IsTypedOrderBy<std::remove_cvref_t<E>>::value || isExpression<E>) && std::remove_cvref_t<E>::isOrder &&
    ORM_QUERY_MODEL<std::remove_cvref_t<E>, M> && ORM_QUERY_UNBOUND_PARAMETER<E>;
template <typename M, typename... Orders>
concept ORM_QUERY_MODEL_ORDERS = (OrderFor<Orders, M> && ...);
} // namespace orm::query::detail

namespace orm::query
{
export
{

    template <auto... Members>
    constexpr auto asc(TypedColumn<Members...> column)
    {
        return detail::makeExpression<ExprKind::Order,
                                      detail::OrderMeta<TypedColumn<Members...>, detail::OrderDirection::Asc>>(column);
    }
    template <auto... Members>
    constexpr auto desc(TypedColumn<Members...> column)
    {
        return detail::makeExpression<ExprKind::Order,
                                      detail::OrderMeta<TypedColumn<Members...>, detail::OrderDirection::Desc>>(column);
    }
    template <typename M>
    auto rawOrder(std::string sql) -> TypedOrderBy<M>
    {
        return detail::TypedAccess::make<TypedOrderBy<M>>(detail::rawOrder(std::move(sql)));
    }
}
} // namespace orm::query

// query/Projection.hpp
namespace orm::query::detail
{
using ProjectionSource = std::variant<Column, AggregateExpression>;

/**
 * @brief A projected source expression and the DTO field alias it hydrates.
 */
struct Projection
{
    std::string resultField;
    ProjectionSource source;
};

/**
 * @brief Projects a source model column path into a result DTO field.
 */
inline auto as(std::string resultField, Column sourceColumn) -> Projection
{
    return Projection{.resultField = std::move(resultField), .source = std::move(sourceColumn)};
}

/**
 * @brief Projects an aggregate expression into a result DTO field.
 */
inline auto as(std::string resultField, AggregateExpression aggregate) -> Projection
{
    return Projection{.resultField = std::move(resultField), .source = std::move(aggregate)};
}

} // namespace orm::query::detail

namespace orm::query
{
export
{

    template <typename Owner>
    class TypedProjection
    {
    public:
        using Model = Owner;
        using ParameterTypes = std::tuple<>;
        inline static constexpr auto kind = ExprKind::DynamicProjection;
        inline static constexpr bool isProjection = true;
        inline static constexpr bool staticSqlEligible = false;
        template <typename E>
            requires detail::isExpression<E> && E::isProjection && detail::ORM_QUERY_MODEL<E, Owner> &&
                     detail::ORM_QUERY_UNBOUND_PARAMETER<E>
        TypedProjection(const E& expression) : data{detail::erase(expression)}
        {
        }
        template <typename E>
            requires detail::isExpression<E> && E::isProjection && detail::ORM_QUERY_MODEL<E, Owner> &&
                         detail::ORM_QUERY_UNBOUND_PARAMETER<E>
        auto operator=(const E& expression) -> TypedProjection&
        {
            data = detail::erase(expression);
            return *this;
        }
        auto dynamic() const -> TypedProjection
        {
            return *this;
        }

    private:
        friend struct detail::TypedAccess;
        explicit TypedProjection(detail::Projection value) : data{std::move(value)} {}
        auto runtime() const -> detail::Projection
        {
            return data;
        }
        detail::Projection data;
    };
}
} // namespace orm::query

namespace orm::query::detail
{
template <typename S, reflection::FixedString Alias>
struct ProjectionMeta : ExpressionMeta<typename S::Model>
{
    using Source = S;
    using Dynamic = TypedProjection<typename S::Model>;
    inline static constexpr auto alias = Alias;
    inline static constexpr bool isProjection = true;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<S>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return detail::as(std::string{Alias.view()}, TypedAccess::erase(std::get<0>(children), args));
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children&, const Args&, Callback&) -> void
    {
    }
};
template <typename S>
struct RuntimeProjectionMeta : ExpressionMeta<typename S::Model>
{
    using Source = S;
    using Dynamic = TypedProjection<typename S::Model>;
    inline static constexpr bool isProjection = true;
    inline static constexpr bool staticSqlEligible = false;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return detail::as(std::get<1>(children), TypedAccess::erase(std::get<0>(children), args));
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children&, const Args&, Callback&) -> void
    {
    }
};
template <typename E>
struct IsTypedProjection : std::false_type
{
};
template <typename M>
struct IsTypedProjection<TypedProjection<M>> : std::true_type
{
};
template <typename E, typename M>
concept ProjectionFor =
    (IsTypedProjection<std::remove_cvref_t<E>>::value || isExpression<E>) && std::remove_cvref_t<E>::isProjection &&
    ORM_QUERY_MODEL<std::remove_cvref_t<E>, M> && ORM_QUERY_UNBOUND_PARAMETER<E>;
template <typename M, typename... Projections>
concept ORM_QUERY_MODEL_PROJECTIONS = (ProjectionFor<Projections, M> && ...);
template <typename S>
concept ProjectionSourceExpression = IsTypedColumn<std::remove_cvref_t<S>>::value || AggregateExpressionType<S>;
} // namespace orm::query::detail

namespace orm::query
{
export
{

    template <typename S>
        requires detail::ProjectionSourceExpression<S>
    constexpr auto as(std::string alias, S source)
    {
        return detail::makeExpression<ExprKind::Projection, detail::RuntimeProjectionMeta<S>>(source, std::move(alias));
    }
    template <reflection::FixedString Alias, typename S>
        requires detail::ProjectionSourceExpression<S>
    constexpr auto as(S source)
    {
        return detail::makeExpression<ExprKind::Projection, detail::ProjectionMeta<S, Alias>>(source);
    }
}
} // namespace orm::query

// query/SelectSpec.hpp
namespace orm::query::detail
{
/**
 * @brief Runtime SELECT options independent from static model metadata.
 */
struct SelectSpec
{
    std::optional<std::size_t> offset = std::nullopt;
    std::optional<std::size_t> limit = std::nullopt;
    std::optional<Predicate> predicate = std::nullopt;
    std::vector<OrderBy> orderBy;
    std::vector<Projection> projections;
    std::vector<Column> groupBy;
    std::optional<AggregatePredicate> having = std::nullopt;
    std::vector<std::string> includes;
    bool isDistinct = false;
    bool shouldJoin = true;
};
} // namespace orm::query::detail

// query/UpdateSpec.hpp
namespace orm::query::detail
{
struct UpdateValue
{
    std::optional<QueryValue> value;
};

struct UpdateAssignment
{
    Column column;
    UpdateValue value;
};

/**
 * @brief Runtime UPDATE options independent from static model metadata.
 */
struct UpdateSpec
{
    std::vector<UpdateAssignment> assignments;
    std::optional<Predicate> predicate = std::nullopt;
};
} // namespace orm::query::detail

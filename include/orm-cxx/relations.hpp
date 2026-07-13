#pragma once

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace orm
{
class Database;

namespace detail
{
template <typename T>
class RelationCollection
{
public:
    using value_type = T;
    using container_type = std::vector<T>;
    using size_type = typename container_type::size_type;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;

    RelationCollection() = default;

    RelationCollection(const RelationCollection& other)
        : values_(other.values_ == nullptr ? nullptr : std::make_shared<container_type>(*other.values_)),
          loaded_(other.loaded_)
    {
    }

    auto operator=(const RelationCollection& other) -> RelationCollection&
    {
        if (this == &other)
        {
            return *this;
        }

        auto copiedValues = other.values_ == nullptr ? std::shared_ptr<container_type>{} :
                                                       std::make_shared<container_type>(*other.values_);
        values_ = std::move(copiedValues);
        loaded_ = other.loaded_;
        return *this;
    }

    RelationCollection(RelationCollection&&) noexcept = default;
    auto operator=(RelationCollection&&) noexcept -> RelationCollection& = default;

    explicit RelationCollection(container_type values)
        : values_(std::make_shared<container_type>(std::move(values))), loaded_(true)
    {
    }

    [[nodiscard]] auto isLoaded() const noexcept -> bool
    {
        return loaded_;
    }

    auto values() -> container_type&
    {
        return mutableValues();
    }

    auto values() const -> const container_type&
    {
        return readableValues();
    }

    auto begin() -> iterator
    {
        return values().begin();
    }

    auto begin() const -> const_iterator
    {
        return values().begin();
    }

    auto cbegin() const -> const_iterator
    {
        return values().cbegin();
    }

    auto end() -> iterator
    {
        return values().end();
    }

    auto end() const -> const_iterator
    {
        return values().end();
    }

    auto cend() const -> const_iterator
    {
        return values().cend();
    }

    [[nodiscard]] auto size() const noexcept -> size_type
    {
        return values_ == nullptr ? 0 : values_->size();
    }

    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return values_ == nullptr or values_->empty();
    }

    auto operator[](size_type index) -> T&
    {
        return values()[index];
    }

    auto operator[](size_type index) const -> const T&
    {
        return values()[index];
    }

private:
    friend class orm::Database;

    auto setLoaded(container_type values) -> void
    {
        values_ = std::make_shared<container_type>(std::move(values));
        loaded_ = true;
    }

    auto mutableValues() -> container_type&
    {
        if (values_ == nullptr)
        {
            values_ = std::make_shared<container_type>();
        }

        return *values_;
    }

    auto readableValues() const -> const container_type&
    {
        if (values_ == nullptr)
        {
            values_ = std::make_shared<container_type>();
        }

        return *values_;
    }

    mutable std::shared_ptr<container_type> values_;
    bool loaded_{};
};
} // namespace detail

template <typename T>
class OneToMany : public detail::RelationCollection<T>
{
public:
    using detail::RelationCollection<T>::RelationCollection;
};

template <typename T>
class ManyToMany : public detail::RelationCollection<T>
{
public:
    using detail::RelationCollection<T>::RelationCollection;
};

namespace detail
{
template <typename T>
struct RelationCollectionTraits
{
    inline static constexpr bool isCollection = false;
    inline static constexpr bool isOneToMany = false;
    using Target = void;
};

template <typename T>
struct RelationCollectionTraits<OneToMany<T>>
{
    inline static constexpr bool isCollection = true;
    inline static constexpr bool isOneToMany = true;
    using Target = T;
};

template <typename T>
struct RelationCollectionTraits<ManyToMany<T>>
{
    inline static constexpr bool isCollection = true;
    inline static constexpr bool isOneToMany = false;
    using Target = T;
};

template <typename T>
inline constexpr bool isRelationCollection = RelationCollectionTraits<std::remove_cv_t<T>>::isCollection;

template <typename T>
struct OptionalRelationCollectionTraits
{
    inline static constexpr bool value = false;
};

template <typename T>
struct OptionalRelationCollectionTraits<std::optional<T>>
{
    inline static constexpr bool value = isRelationCollection<T>;
};

template <typename T>
inline constexpr bool isOptionalRelationCollection = OptionalRelationCollectionTraits<std::remove_cv_t<T>>::value;
} // namespace detail

template <typename T>
inline constexpr bool is_relation_collection_v = detail::isRelationCollection<std::remove_cvref_t<T>>;

template <typename T>
inline constexpr bool is_optional_relation_collection_v = detail::isOptionalRelationCollection<std::remove_cvref_t<T>>;

template <typename T>
using relation_target_t = typename detail::RelationCollectionTraits<std::remove_cvref_t<T>>::Target;

template <typename T>
inline constexpr bool is_one_to_many_v =
    is_relation_collection_v<T> && detail::RelationCollectionTraits<std::remove_cvref_t<T>>::isOneToMany;

class OneToManyDescriptor
{
public:
    explicit OneToManyDescriptor(std::string_view fieldName) : fieldName_(fieldName) {}

    [[nodiscard]] auto mappedBy(std::string_view fieldName) const -> OneToManyDescriptor
    {
        auto result = *this;
        result.mappedBy_ = fieldName;
        return result;
    }

    [[nodiscard]] auto fieldName() const noexcept -> const std::string&
    {
        return fieldName_;
    }

    [[nodiscard]] auto mappedByField() const noexcept -> const std::string&
    {
        return mappedBy_;
    }

private:
    std::string fieldName_;
    std::string mappedBy_;
};

class ManyToManyDescriptor
{
public:
    explicit ManyToManyDescriptor(std::string_view fieldName) : fieldName_(fieldName) {}

    [[nodiscard]] auto through(std::string_view tableName) const -> ManyToManyDescriptor
    {
        auto result = *this;
        result.through_ = tableName;
        return result;
    }

    [[nodiscard]] auto mappedBy(std::string_view fieldName) const -> ManyToManyDescriptor
    {
        auto result = *this;
        result.mappedBy_ = fieldName;
        return result;
    }

    [[nodiscard]] auto ownerColumns(std::initializer_list<std::string_view> columns) const -> ManyToManyDescriptor
    {
        auto result = *this;
        result.ownerColumns_.clear();
        result.ownerColumns_.reserve(columns.size());
        for (const auto column : columns)
        {
            result.ownerColumns_.emplace_back(column);
        }
        return result;
    }

    [[nodiscard]] auto ownerColumns(std::vector<std::string> columns) const -> ManyToManyDescriptor
    {
        auto result = *this;
        result.ownerColumns_ = std::move(columns);
        return result;
    }

    [[nodiscard]] auto targetColumns(std::initializer_list<std::string_view> columns) const -> ManyToManyDescriptor
    {
        auto result = *this;
        result.targetColumns_.clear();
        result.targetColumns_.reserve(columns.size());
        for (const auto column : columns)
        {
            result.targetColumns_.emplace_back(column);
        }
        return result;
    }

    [[nodiscard]] auto targetColumns(std::vector<std::string> columns) const -> ManyToManyDescriptor
    {
        auto result = *this;
        result.targetColumns_ = std::move(columns);
        return result;
    }

    [[nodiscard]] auto fieldName() const noexcept -> const std::string&
    {
        return fieldName_;
    }

    [[nodiscard]] auto throughTable() const noexcept -> const std::string&
    {
        return through_;
    }

    [[nodiscard]] auto mappedByField() const noexcept -> const std::string&
    {
        return mappedBy_;
    }

    [[nodiscard]] auto ownerColumnNames() const noexcept -> const std::vector<std::string>&
    {
        return ownerColumns_;
    }

    [[nodiscard]] auto targetColumnNames() const noexcept -> const std::vector<std::string>&
    {
        return targetColumns_;
    }

private:
    std::string fieldName_;
    std::string through_;
    std::string mappedBy_;
    std::vector<std::string> ownerColumns_;
    std::vector<std::string> targetColumns_;
};

[[nodiscard]] inline auto oneToMany(std::string_view fieldName) -> OneToManyDescriptor
{
    return OneToManyDescriptor{fieldName};
}

[[nodiscard]] inline auto manyToMany(std::string_view fieldName) -> ManyToManyDescriptor
{
    return ManyToManyDescriptor{fieldName};
}

template <typename... Descriptors>
[[nodiscard]] auto relations(Descriptors&&... descriptors)
{
    static_assert(((std::is_same_v<std::decay_t<Descriptors>, OneToManyDescriptor> ||
                    std::is_same_v<std::decay_t<Descriptors>, ManyToManyDescriptor>) &&
                   ...),
                  "orm::relations accepts only relation descriptors");
    return std::make_tuple(std::forward<Descriptors>(descriptors)...);
}
} // namespace orm

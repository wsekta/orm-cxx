module;

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

export module orm:relation_collections;

// relations.hpp
namespace orm
{
export
{

    class Database;
    template <typename SchemaType>
    class OrmContext;
}
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
        : values_(not other.values_ ? nullptr : std::make_shared<container_type>(*other.values_)),
          loaded_(other.loaded_)
    {
    }

    auto operator=(const RelationCollection& other) -> RelationCollection&
    {
        if (this == &other)
        {
            return *this;
        }

        auto copiedValues =
            not other.values_ ? std::shared_ptr<container_type>{} : std::make_shared<container_type>(*other.values_);
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
        return not values_ ? 0 : values_->size();
    }

    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return not values_ or values_->empty();
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
    template <typename>
    friend class orm::OrmContext;

    auto setLoaded(container_type values) -> void
    {
        values_ = std::make_shared<container_type>(std::move(values));
        loaded_ = true;
    }

    auto mutableValues() -> container_type&
    {
        if (not values_)
        {
            values_ = std::make_shared<container_type>();
        }

        return *values_;
    }

    auto readableValues() const -> const container_type&
    {
        if (not values_)
        {
            values_ = std::make_shared<container_type>();
        }

        return *values_;
    }

    mutable std::shared_ptr<container_type> values_;
    bool loaded_{};
};
}
export
{
    // namespace detail

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
}
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
}
export
{
    // namespace detail

    template <typename T>
    inline constexpr bool is_relation_collection_v = detail::isRelationCollection<std::remove_cvref_t<T>>;

    template <typename T>
    inline constexpr bool is_optional_relation_collection_v =
        detail::isOptionalRelationCollection<std::remove_cvref_t<T>>;

    template <typename T>
    using relation_target_t = typename detail::RelationCollectionTraits<std::remove_cvref_t<T>>::Target;

    template <typename T>
    inline constexpr bool is_one_to_many_v =
        is_relation_collection_v<T> && detail::RelationCollectionTraits<std::remove_cvref_t<T>>::isOneToMany;
}

} // namespace orm

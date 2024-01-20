# Query

1. [Build select](#build-select)
2. [Limit select](#limit-select)
3. [Offset select](#offset-select)
4. [Build select](#build-select)

## Build select

To build select use `orm::Query` class. For example:

```cpp
struct ObjectModel
{
    int id;
    std::string name;
    std::string email;
    std::string password;
    std::string created_at;
    std::string updated_at;
};

orm::Query<ObjectModel> select;
```

it is builder class so you can chain methods:

```cpp
orm::Query<ObjectModel> select;
select.limit(10).offset(10);
```

## Limit select

To limit select use `limit` method:

```cpp
select.limit(10);
```

## Offset select

To offset select use `offset` method:

```cpp
select.offset(10);
```

## Build select

To build select use `buildQuery` method, it will return string with select:

```cpp
auto queryString = select.buildQuery();
```
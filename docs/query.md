# Query

1. [Build query](#build-query)
2. [Limit query](#limit-query)
3. [Offset query](#offset-query)
4. [Build query](#build-query)

## Build query

To build query use `orm::Query` class. For example:

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

orm::Query<ObjectModel> query;
```

it is builder class so you can chain methods:

```cpp
orm::Query<ObjectModel> query;
query.limit(10).offset(10);
```

## Limit query

To limit query use `limit` method:

```cpp
query.limit(10);
```

## Offset query

To offset query use `offset` method:

```cpp
query.offset(10);
```

## Build query

To build query use `buildQuery` method, it will return string with query:

```cpp
auto queryString = query.buildQuery();
```
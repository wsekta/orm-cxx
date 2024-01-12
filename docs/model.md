# Model

1. [Create simple model](#create-simple-model)
2. [Fields types](#fields-types)
3. [Table name](#table-name)
4. [Mapping fields' names](#mapping-fields-names)
5. [Primary key](#primary-key)

## Create simple model

Simple model is a struct with supoorted types of fields. For example:

```cpp
struct User {
    int id;
    std::string name;
    std::string email;
    std::string password;
    std::string created_at;
    std::string updated_at;
};
```

## Fields types

Supported types of fields:

* int
* float
* double
* std::string

## Table name

To override table name use `table_name` static field:

```cpp
struct User {
    static constexpr const char* table_name = "users";
    int id;
    std::string name;
    std::string email;
    std::string password;
    std::string created_at;
    std::string updated_at;
};
```

## Mapping fields' names

If you want to map fields' names to database columns' names use 'columns_names' static field with dictionary type (
e.g. `std::map` or `std::unordered_map`).
Not all fields have to be mapped, those that are not mapped will be named the same as fields' names.
For example:

```cpp
struct User {
    static constexpr std::map<const char*, const char*> columns_names = {
        {"id", "user_id"},
        {"name", "user_name"},
        {"email", "user_email"},
        {"password", "user_password"},
        {"created_at", "user_created_at"},
        {"updated_at", "user_updated_at"}
    };
    int id;
    std::string name;
    std::string email;
    std::string password;
    std::string created_at;
    std::string updated_at;
};
```

## Primary key

The default primary key is `id` field:

```cpp
struct User {
    int id;
    std::string name;
    std::string email;
    std::string password;
    std::string created_at;
    std::string updated_at;
};
```

If you want to change primary key use `id_columns` static field with iterable type containing fields' names as std::
string (e.g. `std::vector<std::string>` or `std::array<std::string>`):

```cpp
struct User {
    static constexpr std::array<std::string, 2> id_columns = {"id", "name"};
    int id;
    std::string name;
    std::string email;
    std::string password;
    std::string created_at;
    std::string updated_at;
};
```

You can also create model without primary key, just do not add `id` field:

```cpp
struct User {
    std::string name;
    std::string email;
    std::string password;
    std::string created_at;
    std::string updated_at;
};
```

or use `id_columns` static field with empty iterable:

```cpp
struct User {
    static constexpr std::array<std::string, 0> id_columns = {};
    std::string name;
    std::string email;
    std::string password;
    std::string created_at;
    std::string updated_at;
};
```
<div align="center">
  <h1>ORM C++</h1>
  <p>ORM Library for Modern C++(C++20) using native reflection.</p>

[![clang++](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml?query=branch%3Amain)
[![g++](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml?query=branch%3Amain)
[![msvc](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml?query=branch%3Amain)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat-square)](https://github.com/wsekta/orm-cxx/issues/new)
</div>

## 🎯 Goal

The goal of the ORM C++ is to provide a decent Object-Relational Mapping library for C++ community.

## Usage

```c++
#include <optional>

#include "orm-cxx/orm.hpp"

struct ObjectModel
{
    // INTEGER - if field is optional it will not be marked as NOT NULL
    std::optional<int> field1;

    // TEXT NOT NULL
    std::string field2;

    // defining id_columns is optional
    inline static const std::vector<std::string> id_columns = {"field1", "field2"};

    // other way to define id column - will be over writen by using id_columns
    // int id;

    // defining table_name is optional
    inline static const std::string table_name = "object_model";
};

int main()
{
    // connect with standard connection string
    orm::Database database;
    database.connect("sqlite3://test.db");

    // drop table if exists
    database.deleteTable<ObjectModel>();

    // create table in database
    database.createTable<ObjectModel>();

    // create objects and insert them into table
    std::vector<ObjectModel> objects{{1, "test"}, {std::nullopt, "text"}};
    database.insertObjects(objects);

    // query objects from database
    orm::Query<ObjectModel> query;
    auto queriedObjects = database.executeQuery(query);

    return 0;
}
```

## 📖 Documentation

https://wsekta.github.io/orm-cxx/

## Consuming library with CMake (CMake 3.22 or newer)

1. Add config to git submodules (execute in project root):

 ```bash
 mkdir externals
 cd externals
 git submodule add https://github.com/wsekta/orm-cxx.git
 ```

2. Link with library:

 ```cmake
 set(BUILD_CONFIG_CXX_TESTS OFF)
set(BUILD_ORM_CXX_EXAMPLE OFF)

add_subdirectory(externals/orm-cxx)

add_executable(main Main.cpp)

target_link_libraries(main orm-cxx)
 ```

## Compiler support

- [MSVC➚](https://en.wikipedia.org/wiki/Microsoft_Visual_Studio) version 143 or newer.
- [GCC➚](https://gcc.gnu.org/) version 13 or newer.
- [Clang➚](https://clang.llvm.org/) version 16 or newer.

## Dependencies

- [GTest](https://github.com/google/googletest) (```BUILD_ORM_CXX_TESTS=OFF``` CMake flag to disable)
- [faker-cxx](https://github.com/cieslarmichal/faker-cxx) (```BUILD_ORM_CXX_TESTS=OFF``` CMake flag to disable)
- [reflect-cpp](https://github.com/wsekta/reflect-cpp)
- [SOCI](https://github.com/SOCI/soci)

## ✨ Contributing

Feel free to join ORM C++ development! 🚀

Please check [CONTRIBUTING](https://github.com/wsekta/orm-cxx/blob/main/CONTRIBUTING.md) guide.

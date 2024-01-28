<div align="center">
  <h1>ORM C++</h1>
  <p>ORM Library for Modern C++(C++20) using native reflection.</p>

[![C++](https://img.shields.io/badge/C++20-grey.svg?style=flat&logo=c%2B%2B&logoColor=blue)](https://en.cppreference.com/w/cpp/20)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Maintenance](https://img.shields.io/badge/Maintained%3F-yes-green.svg)](https://github.com/wsekta/orm-cxx/graphs/commit-activity)
[![Generic badge](https://img.shields.io/badge/gcc-13+-blue.svg)](https://gcc.gnu.org/)
[![Generic badge](https://img.shields.io/badge/clang-16+-blue.svg)](https://clang.llvm.org/)
[![Generic badge](https://img.shields.io/badge/MSVC-17+-blue.svg)](https://en.wikipedia.org/wiki/Microsoft_Visual_Studio)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat)](https://github.com/wsekta/orm-cxx/issues/new)

[![clang++](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml?select=branch%3Amain)
[![g++](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml?select=branch%3Amain)
[![msvc](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml?select=branch%3Amain)
[![codecov](https://codecov.io/github/wsekta/orm-cxx/graph/badge.svg?token=MREUNGY5C9)](https://codecov.io/github/wsekta/orm-cxx)
</div>

## 🎯 Goal

The goal of the ORM C++ is to provide a decent Object-Relational Mapping library for C++ community.

🆕 Base on native reflection from modern C++(C++20)<br>
✅ 100% test coverage with unit tests and integration tests<br>
🗂️ Support for multiple databases<br>
⚙️ Support for multiple compilers<br>
☠️ No macros (currently few 😕)<br>
🚀 As low as possible runtime overhead<br>
👶 Easy to use<br>
📉 As few dependencies as possible<br>

## ⚙️ Usage

```cpp
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

    // defining table_name is optional, adding it will overwrite default table name
    inline static const std::string table_name = "object_model";

    // defining columns_names is optional, adding it will overwrite default columns names
    // not all columns have to be defined, others will get default names
    inline static const std::map<std::string, std::string> columns_names = {{"field1", "some_field1_name"},
                                                                            {"field2", "some_field2_name"}};
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
    database.insert(objects);

    // define select query with builder pattern
    orm::Query<ObjectModel> query;
    query.limit(10).offset(5);

    // execute query
    auto queriedObjects = database.select(query);

    return 0;
}
```

## 📖 Documentation

## [Markdown](docs/main.md)

## [Doxygen](https://wsekta.github.io/orm-cxx/)

## 📝 Consuming library with CMake (CMake 3.22 or newer)

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

## ⚒️ Compiler support

- [MSVC➚](https://en.wikipedia.org/wiki/Microsoft_Visual_Studio) version 143 or newer.
- [GCC➚](https://gcc.gnu.org/) version 13 or newer.
- [Clang➚](https://clang.llvm.org/) version 16 or newer.

## 📦 Dependencies

- [GTest](https://github.com/google/googletest) (```BUILD_ORM_CXX_TESTS=OFF``` CMake flag to disable)
- [faker-cxx](https://github.com/cieslarmichal/faker-cxx) (```BUILD_ORM_CXX_TESTS=OFF``` CMake flag to disable)
- [reflect-cpp](https://github.com/wsekta/reflect-cpp)
- [SOCI](https://github.com/SOCI/soci)

## ✨ Contributing

Feel free to join ORM C++ development! 🚀

Please check [CONTRIBUTING](https://github.com/wsekta/orm-cxx/blob/main/CONTRIBUTING.md) guide.

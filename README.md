<div align="center">
  <h1>ORM C++</h1>
  <p>ORM Library for Modern C++(C++20) using native reflection.</p>

[![clang++](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml?query=branch%3Amain)
[![g++](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml?query=branch%3Amain)
[![msvc](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml?query=branch%3Amain)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat-square)]([http://makeapullrequest.com](https://github.com/wsekta/orm-cxx/issues/new)
</div>

## 🎯 Goal

The goal of the ORM C++ is to provide a decent Object-Relational Mapping library for C++ community.

## Usage

```cpp

//🚧🚧🚧 work in progress 🚧🚧🚧
```

## 📖 Documentation

https://wsekta.github.io/orm-cxx/

## Consuming library with CMake (CMake 3.22 or newer)

1. Add config to git submodules (execute in project root):

    ```
    mkdir externals
    cd externals
    git submodule add https://github.com/wsekta/orm-cxx.git
    ```

2. Link with library:

    ```cmake
    set(BUILD_CONFIG_CXX_TESTS OFF)
    
    add_subdirectory(externals/orm-cxx)
    
    add_executable(main Main.cpp)
    
    target_link_libraries(main orm-cxx)
    ```

## Compiler support

- [MSVC➚](https://en.wikipedia.org/wiki/Microsoft_Visual_Studio) version 143 or newer.
- [GCC➚](https://gcc.gnu.org/) version 13 or newer.
- [Clang➚](https://clang.llvm.org/) version 16 or newer.

## Dependencies

- GTest (```BUILD_CONFIG_CXX_TESTS=OFF``` CMake flag to disable)

## ✨ Contributing

Feel free to join Config C++ development! 🚀

Please check [CONTRIBUTING](https://github.com/wsekta/orm-cxx/blob/main/CONTRIBUTING.md) guide.

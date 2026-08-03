<div align="center">
  <h1>ORM C++</h1>
  <p>ORM Library for Modern C++ (C++20) using compile-time reflection.</p>

[![clang++](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml?select=branch%3Amain)
[![g++](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml?select=branch%3Amain)
[![msvc](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml?select=branch%3Amain)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat-square)](https://makeapullrequest.com)

</div>

ORM C++ is a compile-time ORM for C++20 that maps plain aggregate structs to
database tables using native reflection — no macros, no code generation.
It ships with SQLite and PostgreSQL backends built on SOCI.

**Minimum compilers:** GCC 13+, Clang 16+, MSVC 17+.

1. [Model](model.md)
2. [Collection relations](relations.md)
3. [Query](query.md)
4. [Database](database.md)
5. [Backends](backends.md)
6. [Backend portability](backend-portability.md)
7. [Backend extension contract](backend-extension.md)

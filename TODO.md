# TODO:

* Add where clause to queries
* Add internal notation for where clause with support for:
    * Column/field name checking
    * Operators (>, <, =, !=, etc)
    * Logical operators (AND, OR, NOT)
    * String operators (LIKE, ILIKE, etc)
* Add support for models with relations OneToMany and ManyToMany
* Refactor CMake files
* Add support for other databases and drivers:
    * PostgreSQL
    * MySQL
    * Oracle
    * ODBC
    * Firebird
    * DB2
* Add update command
* Add support for other types:
    * char (int8_t)
    * unsigned char (uint8_t)
    * short (int16_t)
    * unsigned short (uint16_t)
    * long (int32_t)
    * unsigned long (uint32_t)
    * long long (int64_t)
    * unsigned long long (uint64_t)
    * std::tm
    * boost::uuids::uuid
    * boost::optional
* Add .clang-tidy and .cmake-format

# Far future:

* Add to vcpkg and conan package managers
* Replace reflect-cpp with own reflection library, due to warnings and errors generated in reflect-cpp
* Move as much as possible to compile time
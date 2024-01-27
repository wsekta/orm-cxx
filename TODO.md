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

# Far future:

* Add to vcpkg and conan package managers
* Replace reflect-cpp with own reflection library, due to warnings and errors generated in reflect-cpp
* Move as much as possible to compile time
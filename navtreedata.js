/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "ORM C++", "index.html", [
    [ "Start here", "index.html#docs-main-start-here", null ],
    [ "Integration reference", "index.html#docs-main-integration-reference", null ],
    [ "For contributors and maintainers", "index.html#docs-main-for-contributors-and-maintainers", null ],
    [ "API reference", "index.html#docs-main-api-reference", null ],
    [ "Backend extension contract", "docs-backend-extension-backend-extension-contract.html", [
      [ "Concrete extension points", "docs-backend-extension-backend-extension-contract.html#docs-backend-extension-concrete-extension-points", null ],
      [ "Required backend responsibilities", "docs-backend-extension-backend-extension-contract.html#docs-backend-extension-required-backend-responsibilities", null ],
      [ "Capabilities versus dialect", "docs-backend-extension-backend-extension-contract.html#docs-backend-extension-capabilities-versus-dialect", null ],
      [ "SOCI boundary", "docs-backend-extension-backend-extension-contract.html#docs-backend-extension-soci-boundary", null ],
      [ "Raw SQL", "docs-backend-extension-backend-extension-contract.html#docs-backend-extension-raw-sql", null ],
      [ "Error contract", "docs-backend-extension-backend-extension-contract.html#docs-backend-extension-error-contract", null ],
      [ "Conformance tests", "docs-backend-extension-backend-extension-contract.html#docs-backend-extension-conformance-tests", null ],
      [ "Build and CI integration", "docs-backend-extension-backend-extension-contract.html#docs-backend-extension-build-and-ci-integration", null ],
      [ "Backend Definition of Done", "docs-backend-extension-backend-extension-contract.html#docs-backend-extension-backend-definition-of-done", null ]
    ] ],
    [ "Backend portability", "docs-backend-portability-backend-portability.html", [
      [ "Portability layers", "docs-backend-portability-backend-portability.html#docs-backend-portability-portability-layers", null ],
      [ "Portable behavior", "docs-backend-portability-backend-portability.html#docs-backend-portability-portable-behavior", null ],
      [ "Capabilities and dialect differences", "docs-backend-portability-backend-portability.html#docs-backend-portability-capabilities-and-dialect-differences", null ],
      [ "Binding and SOCI", "docs-backend-portability-backend-portability.html#docs-backend-portability-binding-and-soci", null ],
      [ "Raw SQL", "docs-backend-portability-backend-portability.html#docs-backend-portability-raw-sql", null ],
      [ "Verification model", "docs-backend-portability-backend-portability.html#docs-backend-portability-verification-model", null ],
      [ "Minimum CI matrix", "docs-backend-portability-backend-portability.html#docs-backend-portability-minimum-ci-matrix", null ],
      [ "Definition of Done", "docs-backend-portability-backend-portability.html#docs-backend-portability-definition-of-done", null ]
    ] ],
    [ "Backends", "docs-backends-backends.html", [
      [ "Support status", "docs-backends-backends.html#docs-backends-support-status", null ],
      [ "Compatibility at a glance", "docs-backends-backends.html#docs-backends-compatibility-at-a-glance", null ],
      [ "SQLite", "docs-backends-backends.html#docs-backends-sqlite", null ],
      [ "PostgreSQL", "docs-backends-backends.html#docs-backends-postgresql", null ],
      [ "MySQL and MariaDB", "docs-backends-backends.html#docs-backends-mysql-and-mariadb", null ],
      [ "Build configuration", "docs-backends-backends.html#docs-backends-build-configuration", null ],
      [ "Capability reporting", "docs-backends-backends.html#docs-backends-capability-reporting", null ],
      [ "Raw SQL portability", "docs-backends-backends.html#docs-backends-raw-sql-portability", null ]
    ] ],
    [ "CI Compiler Matrix", "docs-ci-compiler-matrix-ci-compiler-matrix.html", [
      [ "CI Status", "docs-ci-compiler-matrix-ci-compiler-matrix.html#docs-ci-compiler-matrix-ci-status", null ],
      [ "Tested Compiler Versions", "docs-ci-compiler-matrix-ci-compiler-matrix.html#docs-ci-compiler-matrix-tested-compiler-versions", [
        [ "GCC", "docs-ci-compiler-matrix-ci-compiler-matrix.html#docs-ci-compiler-matrix-gcc", null ],
        [ "Clang", "docs-ci-compiler-matrix-ci-compiler-matrix.html#docs-ci-compiler-matrix-clang", null ],
        [ "MSVC", "docs-ci-compiler-matrix-ci-compiler-matrix.html#docs-ci-compiler-matrix-msvc", null ]
      ] ],
      [ "Backend and consumer matrix", "docs-ci-compiler-matrix-ci-compiler-matrix.html#docs-ci-compiler-matrix-backend-and-consumer-matrix", null ],
      [ "Coverage policy", "docs-ci-compiler-matrix-ci-compiler-matrix.html#docs-ci-compiler-matrix-coverage-policy", null ],
      [ "CMake Presets", "docs-ci-compiler-matrix-ci-compiler-matrix.html#docs-ci-compiler-matrix-cmake-presets", null ],
      [ "Adding New Compiler Versions", "docs-ci-compiler-matrix-ci-compiler-matrix.html#docs-ci-compiler-matrix-adding-new-compiler-versions", null ]
    ] ],
    [ "Database", "docs-database-database.html", [
      [ "Connect", "docs-database-database.html#docs-database-connect", null ],
      [ "Capabilities and errors", "docs-database-database.html#docs-database-capabilities-and-errors", null ],
      [ "Create table", "docs-database-database.html#docs-database-create-table", null ],
      [ "Delete table", "docs-database-database.html#docs-database-delete-table", null ],
      [ "Create and delete relation tables", "docs-database-database.html#docs-database-create-and-delete-relation-tables", null ],
      [ "Insert objects", "docs-database-database.html#docs-database-insert-objects", null ],
      [ "Link and unlink relations", "docs-database-database.html#docs-database-link-and-unlink-relations", null ],
      [ "Query objects", "docs-database-database.html#docs-database-query-objects", null ],
      [ "Update objects", "docs-database-database.html#docs-database-update-objects", null ],
      [ "Remove objects", "docs-database-database.html#docs-database-remove-objects", null ],
      [ "Transactions", "docs-database-database.html#docs-database-transactions", null ]
    ] ],
    [ "Model", "docs-model-model.html", [
      [ "Create a model", "docs-model-model.html#docs-model-create-a-model", null ],
      [ "Static schema", "docs-model-model.html#docs-model-static-schema", null ],
      [ "Supported field types", "docs-model-model.html#docs-model-supported-field-types", null ],
      [ "Bounded strings", "docs-model-model.html#docs-model-bounded-strings", null ],
      [ "Optional fields", "docs-model-model.html#docs-model-optional-fields", null ],
      [ "Table name", "docs-model-model.html#docs-model-table-name", null ],
      [ "Column names", "docs-model-model.html#docs-model-column-names", null ],
      [ "Primary key", "docs-model-model.html#docs-model-primary-key", null ],
      [ "Auto-increment primary key", "docs-model-model.html#docs-model-auto-increment-primary-key", null ],
      [ "One-to-one relations", "docs-model-model.html#docs-model-one-to-one-relations", null ],
      [ "Collection relations", "docs-model-model.html#docs-model-collection-relations", null ],
      [ "Current limitations", "docs-model-model.html#docs-model-current-limitations", null ]
    ] ],
    [ "C++20 modules", "docs-modules-c20-modules.html", [
      [ "Source organization", "docs-modules-c20-modules.html#docs-modules-source-organization", null ],
      [ "CMake consumers and installation", "docs-modules-c20-modules.html#docs-modules-cmake-consumers-and-installation", null ]
    ] ],
    [ "Package managers and releases", "docs-packaging-package-managers-and-releases.html", [
      [ "Consume from vcpkg after acceptance", "docs-packaging-package-managers-and-releases.html#docs-packaging-consume-from-vcpkg-after-acceptance", null ],
      [ "Consume from ConanCenter after acceptance", "docs-packaging-package-managers-and-releases.html#docs-packaging-consume-from-conancenter-after-acceptance", null ],
      [ "Maintainer setup and publishing", "docs-packaging-package-managers-and-releases.html#docs-packaging-maintainer-setup-and-publishing", [
        [ "Retry without changing the release", "docs-packaging-package-managers-and-releases.html#docs-packaging-retry-without-changing-the-release", null ]
      ] ],
      [ "Local package verification", "docs-packaging-package-managers-and-releases.html#docs-packaging-local-package-verification", null ],
      [ "Native CMake installation", "docs-packaging-package-managers-and-releases.html#docs-packaging-native-cmake-installation", null ]
    ] ],
    [ "Partial-result queries", "docs-partial-result-queries-partial-result-queries.html", [
      [ "Contract", "docs-partial-result-queries-partial-result-queries.html#docs-partial-result-queries-contract", null ],
      [ "Query behavior", "docs-partial-result-queries-partial-result-queries.html#docs-partial-result-queries-query-behavior", null ],
      [ "Aggregate result queries", "docs-partial-result-queries-partial-result-queries.html#docs-partial-result-queries-aggregate-result-queries", null ],
      [ "Result DTO rules", "docs-partial-result-queries-partial-result-queries.html#docs-partial-result-queries-result-dto-rules", null ],
      [ "Limitations", "docs-partial-result-queries-partial-result-queries.html#docs-partial-result-queries-limitations", null ]
    ] ],
    [ "Query", "docs-query-query.html", [
      [ "Build select", "docs-query-query.html#docs-query-build-select", null ],
      [ "Loading collections", "docs-query-query.html#docs-query-loading-collections", null ],
      [ "Where predicates", "docs-query-query.html#docs-query-where-predicates", null ],
      [ "Collection predicates", "docs-query-query.html#docs-query-collection-predicates", null ],
      [ "Column names and relations", "docs-query-query.html#docs-query-column-names-and-relations", null ],
      [ "Ordering", "docs-query-query.html#docs-query-ordering", null ],
      [ "Distinct", "docs-query-query.html#docs-query-distinct", null ],
      [ "Limit and offset", "docs-query-query.html#docs-query-limit-and-offset", null ],
      [ "Raw SQL fragments", "docs-query-query.html#docs-query-raw-sql-fragments", null ],
      [ "Partial-result queries", "docs-query-query.html#docs-query-partial-result-queries", null ],
      [ "Full-model grouping and HAVING", "docs-query-query.html#docs-query-full-model-grouping-and-having", null ],
      [ "Aggregate projection queries", "docs-query-query.html#docs-query-aggregate-projection-queries", null ],
      [ "Write predicates", "docs-query-query.html#docs-query-write-predicates", null ],
      [ "Limitations", "docs-query-query.html#docs-query-limitations", null ]
    ] ],
    [ "Quickstart: SQLite CRUD", "docs-quickstart-quickstart-sqlite-crud.html", [
      [ "Requirements", "docs-quickstart-quickstart-sqlite-crud.html#docs-quickstart-requirements", null ],
      [ "Get the sources", "docs-quickstart-quickstart-sqlite-crud.html#docs-quickstart-get-the-sources", null ],
      [ "Build and run", "docs-quickstart-quickstart-sqlite-crud.html#docs-quickstart-build-and-run", null ],
      [ "The application", "docs-quickstart-quickstart-sqlite-crud.html#docs-quickstart-the-application", null ],
      [ "If the build fails", "docs-quickstart-quickstart-sqlite-crud.html#docs-quickstart-if-the-build-fails", null ],
      [ "Next steps", "docs-quickstart-quickstart-sqlite-crud.html#docs-quickstart-next-steps", null ]
    ] ],
    [ "Collection relations", "docs-relations-collection-relations.html", [
      [ "Collection wrappers", "docs-relations-collection-relations.html#docs-relations-collection-wrappers", null ],
      [ "One-to-many", "docs-relations-collection-relations.html#docs-relations-one-to-many", null ],
      [ "Many-to-many", "docs-relations-collection-relations.html#docs-relations-many-to-many", null ],
      [ "Junction column names", "docs-relations-collection-relations.html#docs-relations-junction-column-names", null ],
      [ "Schema lifecycle", "docs-relations-collection-relations.html#docs-relations-schema-lifecycle", null ],
      [ "Link and unlink", "docs-relations-collection-relations.html#docs-relations-link-and-unlink", null ],
      [ "Loading collections", "docs-relations-collection-relations.html#docs-relations-loading-collections", null ],
      [ "Filtering by collections", "docs-relations-collection-relations.html#docs-relations-filtering-by-collections", null ],
      [ "End-to-end workflow", "docs-relations-collection-relations.html#docs-relations-end-to-end-workflow", null ],
      [ "Transactions and consistency", "docs-relations-collection-relations.html#docs-relations-transactions-and-consistency", null ],
      [ "Validation and unsupported mappings", "docs-relations-collection-relations.html#docs-relations-validation-and-unsupported-mappings", null ],
      [ "Common mistakes", "docs-relations-collection-relations.html#docs-relations-common-mistakes", null ],
      [ "Migrating an existing schema", "docs-relations-collection-relations.html#docs-relations-migrating-an-existing-schema", null ]
    ] ],
    [ "Schema migrations", "docs-schema-migrations-schema-migrations.html", [
      [ "Application CLI", "docs-schema-migrations-schema-migrations.html#docs-schema-migrations-application-cli", null ],
      [ "Durable state and connection loss", "docs-schema-migrations-schema-migrations.html#docs-schema-migrations-durable-state-and-connection-loss", null ],
      [ "Project files", "docs-schema-migrations-schema-migrations.html#docs-schema-migrations-project-files", null ],
      [ "Model changes and decisions", "docs-schema-migrations-schema-migrations.html#docs-schema-migrations-model-changes-and-decisions", null ],
      [ "C++ API and execution", "docs-schema-migrations-schema-migrations.html#docs-schema-migrations-c-api-and-execution", null ],
      [ "History, transactions and recovery", "docs-schema-migrations-schema-migrations.html#docs-schema-migrations-history-transactions-and-recovery", null ],
      [ "SQL contract", "docs-schema-migrations-schema-migrations.html#docs-schema-migrations-sql-contract", null ]
    ] ],
    [ "Static query plans", "docs-static-queries-static-query-plans.html", [
      [ "Projections and writes", "docs-static-queries-static-query-plans.html#docs-static-queries-projections-and-writes", null ],
      [ "Runtime shapes and explicit conversion", "docs-static-queries-static-query-plans.html#docs-static-queries-runtime-shapes-and-explicit-conversion", null ]
    ] ],
    [ "Date, time and UUID values", "docs-value-types-date-time-and-uuid-values.html", [
      [ "Native mappings", "docs-value-types-date-time-and-uuid-values.html#docs-value-types-native-mappings", null ],
      [ "Models and values", "docs-value-types-date-time-and-uuid-values.html#docs-value-types-models-and-values", null ],
      [ "Ranges and time zones", "docs-value-types-date-time-and-uuid-values.html#docs-value-types-ranges-and-time-zones", null ],
      [ "Queries and projections", "docs-value-types-date-time-and-uuid-values.html#docs-value-types-queries-and-projections", null ],
      [ "UUID representation", "docs-value-types-date-time-and-uuid-values.html#docs-value-types-uuid-representation", null ]
    ] ],
    [ "How to contribute", "CONTRIBUTING-how-to-contribute.html", [
      [ "Getting started", "CONTRIBUTING-how-to-contribute.html#CONTRIBUTING-getting-started", null ],
      [ "Fastest supported setup", "CONTRIBUTING-how-to-contribute.html#CONTRIBUTING-fastest-supported-setup", null ],
      [ "Presets and CI-equivalent commands", "CONTRIBUTING-how-to-contribute.html#CONTRIBUTING-presets-and-ci-equivalent-commands", null ],
      [ "Database backend work", "CONTRIBUTING-how-to-contribute.html#CONTRIBUTING-database-backend-work", null ],
      [ "Native Linux setup", "CONTRIBUTING-how-to-contribute.html#CONTRIBUTING-native-linux-setup", null ],
      [ "Native Windows and MSVC", "CONTRIBUTING-how-to-contribute.html#CONTRIBUTING-native-windows-and-msvc", null ],
      [ "Documentation", "CONTRIBUTING-how-to-contribute.html#CONTRIBUTING-documentation", null ],
      [ "Build configuration and macro policy", "CONTRIBUTING-how-to-contribute.html#CONTRIBUTING-build-configuration-and-macro-policy", null ],
      [ "Before submitting a pull request", "CONTRIBUTING-how-to-contribute.html#CONTRIBUTING-before-submitting-a-pull-request", null ]
    ] ],
    [ "ORM C++ Roadmap", "ROADMAP-orm-c-roadmap.html", [
      [ "Current State", "ROADMAP-orm-c-roadmap.html#ROADMAP-current-state", null ],
      [ "Long Term", "ROADMAP-orm-c-roadmap.html#ROADMAP-long-term", null ],
      [ "Far Future", "ROADMAP-orm-c-roadmap.html#ROADMAP-far-future", null ],
      [ "Roadmap Principles", "ROADMAP-orm-c-roadmap.html#ROADMAP-roadmap-principles", null ]
    ] ],
    [ "Modules", "modules.html", [
      [ "Modules List", "modules.html", "modules_dup" ]
    ] ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ],
      [ "Namespace Members", "namespacemembers.html", [
        [ "All", "namespacemembers.html", "namespacemembers_dup" ],
        [ "Functions", "namespacemembers_func.html", null ],
        [ "Variables", "namespacemembers_vars.html", null ],
        [ "Typedefs", "namespacemembers_type.html", null ],
        [ "Enumerations", "namespacemembers_enum.html", null ]
      ] ]
    ] ],
    [ "Concepts", "concepts.html", "concepts" ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", "functions_vars" ],
        [ "Typedefs", "functions_type.html", null ],
        [ "Related Symbols", "functions_rela.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"BackendRelations_8cppm.html",
"classorm_1_1Query.html#aae966c137c3e919f57366a85f7478ca0",
"classorm_1_1db_1_1DefaultCommandGenerator.html#a79fecb0755beaf1d4c0968ecbe1f96a5",
"classorm_1_1db_1_1postgresql_1_1PostgresqlDialect.html#ab21997633e69e8554971cbdd8c40ad07",
"classorm_1_1query_1_1QueryValue.html#a4e71a0f6f754358b67d3a3fb61ece924",
"classorm_1_1query_1_1TypedColumn.html#a62b68f22d96565eb011ed9e3e92f179f",
"docs-packaging-package-managers-and-releases.html#docs-packaging-consume-from-vcpkg-after-acceptance",
"module__orm.html#a5f32d4a72ffe926c0d02f0cc8df39f6a",
"namespaceorm.html#a2753e67871ca8adab07de8977b9e1b80",
"namespaceorm_1_1query.html#a4b1bf69942f7f7946b91194256742fa3",
"structorm_1_1PostgresTls.html#a75ebce33288fe009599ec459ff5c5008",
"structorm_1_1db_1_1RelationCapabilities.html#aa849f593fc4dcb3989bc2ca08ba3b0a7",
"structorm_1_1migrations_1_1GenerationDecisions.html#a376e1959e813401c8e2e8902448969b2",
"structorm_1_1migrations_1_1RebuildTable.html#a94ed51fa0f329c08f04ae33274242909",
"structorm_1_1model_1_1ModelDataView.html",
"update_8cppm_source.html"
];

const SYNCONMSG = 'click to disable panel synchronization';
const SYNCOFFMSG = 'click to enable panel synchronization';
const LISTOFALLMEMBERS = 'List of all members';
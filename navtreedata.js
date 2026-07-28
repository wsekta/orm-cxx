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
    [ "Backend extension contract", "md_docs_2backend-extension.html", [
      [ "Concrete extension points", "md_docs_2backend-extension.html#autotoc_md1", null ],
      [ "Required backend responsibilities", "md_docs_2backend-extension.html#autotoc_md2", null ],
      [ "Capabilities versus dialect", "md_docs_2backend-extension.html#autotoc_md3", null ],
      [ "SOCI boundary", "md_docs_2backend-extension.html#autotoc_md4", null ],
      [ "Raw SQL", "md_docs_2backend-extension.html#autotoc_md5", null ],
      [ "Error contract", "md_docs_2backend-extension.html#autotoc_md6", null ],
      [ "Conformance tests", "md_docs_2backend-extension.html#autotoc_md7", null ],
      [ "Build and CI integration", "md_docs_2backend-extension.html#autotoc_md8", null ],
      [ "Backend Definition of Done", "md_docs_2backend-extension.html#autotoc_md9", null ]
    ] ],
    [ "Backend portability", "md_docs_2backend-portability.html", [
      [ "Portability layers", "md_docs_2backend-portability.html#autotoc_md11", null ],
      [ "Portable behavior", "md_docs_2backend-portability.html#autotoc_md12", null ],
      [ "Capabilities and dialect differences", "md_docs_2backend-portability.html#autotoc_md13", null ],
      [ "Binding and SOCI", "md_docs_2backend-portability.html#autotoc_md14", null ],
      [ "Raw SQL", "md_docs_2backend-portability.html#autotoc_md15", null ],
      [ "Verification model", "md_docs_2backend-portability.html#autotoc_md16", null ],
      [ "Minimum CI matrix", "md_docs_2backend-portability.html#autotoc_md17", null ],
      [ "Definition of Done", "md_docs_2backend-portability.html#autotoc_md18", null ]
    ] ],
    [ "Backends", "md_docs_2backends.html", [
      [ "Support status", "md_docs_2backends.html#autotoc_md20", null ],
      [ "Compatibility at a glance", "md_docs_2backends.html#autotoc_md21", null ],
      [ "SQLite", "md_docs_2backends.html#autotoc_md22", null ],
      [ "PostgreSQL", "md_docs_2backends.html#autotoc_md23", null ],
      [ "Build configuration", "md_docs_2backends.html#autotoc_md24", null ],
      [ "Capability reporting", "md_docs_2backends.html#autotoc_md25", null ],
      [ "Raw SQL portability", "md_docs_2backends.html#autotoc_md26", null ]
    ] ],
    [ "Database", "md_docs_2database.html", [
      [ "Connect", "md_docs_2database.html#autotoc_md28", null ],
      [ "Capabilities and errors", "md_docs_2database.html#autotoc_md29", null ],
      [ "Create table", "md_docs_2database.html#autotoc_md30", null ],
      [ "Delete table", "md_docs_2database.html#autotoc_md31", null ],
      [ "Create and delete relation tables", "md_docs_2database.html#autotoc_md32", null ],
      [ "Insert objects", "md_docs_2database.html#autotoc_md33", null ],
      [ "Link and unlink relations", "md_docs_2database.html#autotoc_md34", null ],
      [ "Query objects", "md_docs_2database.html#autotoc_md35", null ],
      [ "Update objects", "md_docs_2database.html#autotoc_md36", null ],
      [ "Remove objects", "md_docs_2database.html#autotoc_md37", null ],
      [ "Transactions", "md_docs_2database.html#autotoc_md38", null ]
    ] ],
    [ "Model", "md_docs_2model.html", [
      [ "Create a model", "md_docs_2model.html#autotoc_md40", null ],
      [ "Supported field types", "md_docs_2model.html#autotoc_md41", null ],
      [ "Optional fields", "md_docs_2model.html#autotoc_md42", null ],
      [ "Table name", "md_docs_2model.html#autotoc_md43", null ],
      [ "Column names", "md_docs_2model.html#autotoc_md44", null ],
      [ "Primary key", "md_docs_2model.html#autotoc_md45", null ],
      [ "Auto-increment primary key", "md_docs_2model.html#autotoc_md46", null ],
      [ "One-to-one relations", "md_docs_2model.html#autotoc_md47", null ],
      [ "Collection relations", "md_docs_2model.html#autotoc_md48", null ],
      [ "Current limitations", "md_docs_2model.html#autotoc_md49", null ]
    ] ],
    [ "Partial-result queries", "md_docs_2partial-result-queries.html", [
      [ "Contract", "md_docs_2partial-result-queries.html#autotoc_md51", null ],
      [ "Query behavior", "md_docs_2partial-result-queries.html#autotoc_md52", null ],
      [ "Aggregate result queries", "md_docs_2partial-result-queries.html#autotoc_md53", null ],
      [ "Result DTO rules", "md_docs_2partial-result-queries.html#autotoc_md54", null ],
      [ "V1 limitations", "md_docs_2partial-result-queries.html#autotoc_md55", null ]
    ] ],
    [ "Query", "md_docs_2query.html", [
      [ "Build select", "md_docs_2query.html#autotoc_md57", null ],
      [ "Loading collections", "md_docs_2query.html#autotoc_md58", null ],
      [ "Where predicates", "md_docs_2query.html#autotoc_md59", null ],
      [ "Collection predicates", "md_docs_2query.html#autotoc_md60", null ],
      [ "Column names and relations", "md_docs_2query.html#autotoc_md61", null ],
      [ "Ordering", "md_docs_2query.html#autotoc_md62", null ],
      [ "Distinct", "md_docs_2query.html#autotoc_md63", null ],
      [ "Limit and offset", "md_docs_2query.html#autotoc_md64", null ],
      [ "Raw SQL fragments", "md_docs_2query.html#autotoc_md65", null ],
      [ "Partial-result queries", "md_docs_2query.html#autotoc_md66", null ],
      [ "Full-model grouping and HAVING", "md_docs_2query.html#autotoc_md67", null ],
      [ "Aggregate projection queries", "md_docs_2query.html#autotoc_md68", null ],
      [ "Write predicates", "md_docs_2query.html#autotoc_md69", null ],
      [ "Current limitations", "md_docs_2query.html#autotoc_md70", null ]
    ] ],
    [ "Collection relations", "md_docs_2relations.html", [
      [ "Collection wrappers", "md_docs_2relations.html#autotoc_md72", null ],
      [ "One-to-many", "md_docs_2relations.html#autotoc_md73", null ],
      [ "Many-to-many", "md_docs_2relations.html#autotoc_md74", null ],
      [ "Junction column names", "md_docs_2relations.html#autotoc_md75", null ],
      [ "Schema lifecycle", "md_docs_2relations.html#autotoc_md76", null ],
      [ "Link and unlink", "md_docs_2relations.html#autotoc_md77", null ],
      [ "Loading collections", "md_docs_2relations.html#autotoc_md78", null ],
      [ "Filtering by collections", "md_docs_2relations.html#autotoc_md79", null ],
      [ "End-to-end workflow", "md_docs_2relations.html#autotoc_md80", null ],
      [ "Transactions and consistency", "md_docs_2relations.html#autotoc_md81", null ],
      [ "Validation and unsupported mappings", "md_docs_2relations.html#autotoc_md82", null ],
      [ "Common mistakes", "md_docs_2relations.html#autotoc_md83", null ],
      [ "Migrating an existing schema", "md_docs_2relations.html#autotoc_md84", null ]
    ] ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", null ],
        [ "Functions", "functions_func.html", null ],
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
"annotated.html"
];

const SYNCONMSG = 'click to disable panel synchronization';
const SYNCOFFMSG = 'click to enable panel synchronization';
const LISTOFALLMEMBERS = 'List of all members';
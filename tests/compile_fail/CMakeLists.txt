cmake_minimum_required(VERSION 3.22)

project(orm-cxx-compile-fail LANGUAGES CXX)

if(NOT ORM_CXX_SOURCE_DIR)
    message(FATAL_ERROR "ORM_CXX_SOURCE_DIR must identify the orm-cxx checkout.")
endif()

add_library(orm-cxx-reflection INTERFACE)
target_compile_features(orm-cxx-reflection INTERFACE cxx_std_20)
target_include_directories(orm-cxx-reflection INTERFACE "${ORM_CXX_SOURCE_DIR}/include")

function(orm_cxx_expect_compile_failure name source expected_diagnostic)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    if(MSVC)
        set(orm_cxx_include_flag "/I${ORM_CXX_SOURCE_DIR}/include")
    else()
        set(orm_cxx_include_flag "-I${ORM_CXX_SOURCE_DIR}/include")
    endif()
    try_compile(
        compile_succeeded SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/${source}" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        COMPILE_DEFINITIONS "${orm_cxx_include_flag}"
        OUTPUT_VARIABLE compiler_output
    )
    if(compile_succeeded)
        message(FATAL_ERROR "${name} unexpectedly compiled successfully.")
    endif()
    string(FIND "${compiler_output}" "${expected_diagnostic}" diagnostic_position)
    if(diagnostic_position EQUAL -1)
        message(
            FATAL_ERROR
                "${name} failed without the required diagnostic '${expected_diagnostic}'.\nCompiler output:\n${compiler_output}"
        )
    endif()
endfunction()

orm_cxx_expect_compile_failure(
    reflection-too-many-fields ReflectionTooManyFields.cpp
    "orm::reflection supports aggregates with at most 128 fields"
)
orm_cxx_expect_compile_failure(reflection-union ReflectionUnion.cpp "orm::reflection does not support unions")
orm_cxx_expect_compile_failure(schema-duplicate-model SchemaDuplicateModel.cpp "ORM_SCHEMA_DUPLICATE_MODEL")
orm_cxx_expect_compile_failure(schema-duplicate-table SchemaDuplicateTable.cpp "ORM_SCHEMA_DUPLICATE_TABLE")
orm_cxx_expect_compile_failure(schema-optional-collection SchemaOptionalCollection.cpp "ORM_MODEL_OPTIONAL_COLLECTION")
orm_cxx_expect_compile_failure(
    schema-missing-relation-descriptor SchemaMissingRelationDescriptor.cpp "ORM_RELATION_DESCRIPTOR_COUNT"
)
orm_cxx_expect_compile_failure(schema-invalid-mapped-by SchemaInvalidMappedBy.cpp "ORM_SCHEMA_RELATION_GRAPH")
orm_cxx_expect_compile_failure(schema-invalid-auto-increment SchemaInvalidAutoIncrement.cpp "ORM_MODEL_AUTO_INCREMENT")
orm_cxx_expect_compile_failure(schema-invalid-primary-key SchemaInvalidPrimaryKey.cpp "ORM_MODEL_PRIMARY_KEY")
orm_cxx_expect_compile_failure(schema-keyless-relation SchemaKeylessRelation.cpp "ORM_RELATION_KEYLESS")
orm_cxx_expect_compile_failure(schema-missing-junction SchemaMissingJunction.cpp "ORM_RELATION_JUNCTION_MISSING")
orm_cxx_expect_compile_failure(
    schema-invalid-junction-columns SchemaInvalidJunctionColumns.cpp "ORM_RELATION_OWNER_COLUMN_COUNT"
)
orm_cxx_expect_compile_failure(schema-duplicate-junction SchemaDuplicateJunction.cpp "ORM_SCHEMA_JUNCTION")
orm_cxx_expect_compile_failure(
    schema-self-relation-default-columns SchemaSelfRelationDefaultColumns.cpp "ORM_SCHEMA_JUNCTION"
)
orm_cxx_expect_compile_failure(
    schema-missing-one-to-many-mapped-by SchemaMissingOneToManyMappedBy.cpp "ORM_RELATION_ONE_TO_MANY_MAPPED_BY"
)
orm_cxx_expect_compile_failure(
    schema-invalid-inverse-configuration SchemaInvalidInverseConfiguration.cpp "ORM_RELATION_INVERSE_CONFIG"
)
orm_cxx_expect_compile_failure(schema-default-id-collection SchemaDefaultIdCollection.cpp "ORM_RELATION_KEYLESS")
orm_cxx_expect_compile_failure(
    schema-generated-foreign-key-collision SchemaGeneratedForeignKeyCollision.cpp "ORM_SCHEMA_COLUMN_COLLISION"
)
orm_cxx_expect_compile_failure(reflection-raw-array-field ReflectionRawArrayField.cpp "ReflectionRawArrayField.cpp")
orm_cxx_expect_compile_failure(reflection-bit-field ReflectionBitField.cpp "ORM_REFLECTION_BIT_FIELD")
orm_cxx_expect_compile_failure(reflection-non-aggregate ReflectionNonAggregate.cpp "ORM_REFLECTION_AGGREGATE")
orm_cxx_expect_compile_failure(reflection-inheritance ReflectionInheritance.cpp "ReflectionInheritance.cpp")
orm_cxx_expect_compile_failure(schema-model-missing SchemaModelMissing.cpp "ORM_SCHEMA_MODEL_MISSING")
orm_cxx_expect_compile_failure(schema-duplicate-column SchemaDuplicateColumn.cpp "ORM_MODEL_COLUMN_NAME")
orm_cxx_expect_compile_failure(schema-relation-primary-key SchemaRelationPrimaryKey.cpp "ORM_MODEL_PRIMARY_KEY")
orm_cxx_expect_compile_failure(
    schema-invalid-mapped-by-alias SchemaInvalidMappedByAlias.cpp "ORM_SCHEMA_RELATION_GRAPH"
)
orm_cxx_expect_compile_failure(
    schema-collection-column-mapping SchemaCollectionColumnMapping.cpp "ORM_MODEL_COLUMN_MAPPING"
)
orm_cxx_expect_compile_failure(
    schema-collection-auto-increment SchemaCollectionAutoIncrement.cpp "ORM_MODEL_AUTO_INCREMENT_DEFINITION"
)
orm_cxx_expect_compile_failure(
    schema-invalid-table-name-definition SchemaInvalidTableNameDefinition.cpp "ORM_MODEL_TABLE_NAME"
)
orm_cxx_expect_compile_failure(
    schema-invalid-column-mapping-definition SchemaInvalidColumnMappingDefinition.cpp "ORM_MODEL_COLUMN_MAPPING"
)
orm_cxx_expect_compile_failure(
    schema-invalid-primary-key-definition SchemaInvalidPrimaryKeyDefinition.cpp "ORM_MODEL_PRIMARY_KEY_DEFINITION"
)
orm_cxx_expect_compile_failure(
    schema-invalid-auto-increment-definition SchemaInvalidAutoIncrementDefinition.cpp
    "ORM_MODEL_AUTO_INCREMENT_DEFINITION"
)
orm_cxx_expect_compile_failure(
    schema-invalid-relations-definition SchemaInvalidRelationsDefinition.cpp "ORM_MODEL_RELATIONS_DEFINITION"
)
orm_cxx_expect_compile_failure(
    schema-relation-target-missing SchemaRelationTargetMissing.cpp "ORM_RELATION_TARGET_MISSING"
)
orm_cxx_expect_compile_failure(
    schema-invalid-target-junction-columns SchemaInvalidTargetJunctionColumns.cpp "ORM_RELATION_TARGET_COLUMN_COUNT"
)
orm_cxx_expect_compile_failure(relation-kind RelationKind.cpp "ORM_RELATION_KIND")
orm_cxx_expect_compile_failure(relation-mapped-by-owner RelationMappedByOwner.cpp "ORM_RELATION_MAPPED_BY_OWNER")
orm_cxx_expect_compile_failure(relation-mapped-by-empty RelationMappedByEmpty.cpp "ORM_RELATION_MAPPED_BY_EMPTY")
orm_cxx_expect_compile_failure(relation-junction-empty RelationJunctionEmpty.cpp "ORM_RELATION_JUNCTION_EMPTY")
orm_cxx_expect_compile_failure(
    relation-owner-columns-empty RelationOwnerColumnsEmpty.cpp "ORM_RELATION_OWNER_COLUMNS_EMPTY"
)
orm_cxx_expect_compile_failure(
    relation-owner-column-empty RelationOwnerColumnEmpty.cpp "ORM_RELATION_OWNER_COLUMN_EMPTY"
)
orm_cxx_expect_compile_failure(
    relation-target-columns-empty RelationTargetColumnsEmpty.cpp "ORM_RELATION_TARGET_COLUMNS_EMPTY"
)
orm_cxx_expect_compile_failure(
    relation-target-column-empty RelationTargetColumnEmpty.cpp "ORM_RELATION_TARGET_COLUMN_EMPTY"
)
orm_cxx_expect_compile_failure(
    schema-unsupported-logical-type SchemaUnsupportedLogicalType.cpp "ORM_MODEL_LOGICAL_TYPE"
)

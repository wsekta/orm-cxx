cmake_minimum_required(VERSION 3.22)

project(orm-cxx-compile-fail LANGUAGES CXX)

if(NOT ORM_CXX_SOURCE_DIR)
    message(FATAL_ERROR "ORM_CXX_SOURCE_DIR must identify the orm-cxx checkout.")
endif()

add_library(orm-cxx-reflection INTERFACE)
target_compile_features(orm-cxx-reflection INTERFACE cxx_std_20)
target_include_directories(orm-cxx-reflection INTERFACE "${ORM_CXX_SOURCE_DIR}/include")

string(REPLACE "|" ";" orm_cxx_dependency_include_paths "${ORM_CXX_DEPENDENCY_INCLUDE_PATHS}")
set(orm_cxx_compile_include_flags)
foreach(include_directory IN ITEMS "${ORM_CXX_SOURCE_DIR}/include" ${orm_cxx_dependency_include_paths})
    if(MSVC)
        list(APPEND orm_cxx_compile_include_flags "/I${include_directory}")
    else()
        list(APPEND orm_cxx_compile_include_flags "-I${include_directory}")
    endif()
endforeach()

function(orm_cxx_expect_compile_success name source)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    try_compile(
        compile_succeeded SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/${source}" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        COMPILE_DEFINITIONS ${orm_cxx_compile_include_flags}
        OUTPUT_VARIABLE compiler_output
    )
    if(NOT compile_succeeded)
        message(FATAL_ERROR "${name} failed to compile.\nCompiler output:\n${compiler_output}")
    endif()
endfunction()

function(orm_cxx_expect_compile_failure name source expected_diagnostic)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    try_compile(
        compile_succeeded SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/${source}" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        COMPILE_DEFINITIONS ${orm_cxx_compile_include_flags}
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

# Positive controls use the same headers and dependency include paths as the failures.
orm_cxx_expect_compile_success(typed-query-positive TypedQueryPositive.cpp)
orm_cxx_expect_compile_failure(query-missing-member QueryMissingMember.cpp "missingAge")
orm_cxx_expect_compile_failure(query-invalid-member QueryInvalidMember.cpp "ORM_QUERY_MEMBER")
orm_cxx_expect_compile_failure(query-collection-column QueryCollectionColumn.cpp "ORM_QUERY_COLUMN")
orm_cxx_expect_compile_failure(query-invalid-path QueryInvalidPath.cpp "ORM_QUERY_PATH")
orm_cxx_expect_compile_failure(query-wrong-path-target QueryWrongPathTarget.cpp "ORM_QUERY_PATH")
orm_cxx_expect_compile_failure(query-too-deep-path QueryTooDeepPath.cpp "ORM_QUERY_PATH")
orm_cxx_expect_compile_failure(query-wrong-model QueryWrongModel.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-composed-wrong-model QueryComposedWrongModel.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-wrong-order-model QueryWrongOrderModel.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-wrong-group-model QueryWrongGroupModel.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-wrong-projection-model QueryWrongProjectionModel.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-wrong-having-model QueryWrongHavingModel.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-value-type QueryValueType.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-signed-narrowing QuerySignedNarrowing.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-unsigned-to-signed QueryUnsignedToSigned.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-signed-to-unsigned QuerySignedToUnsigned.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-floating-narrowing QueryFloatingNarrowing.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-integer-to-float-loss QueryIntegerToFloatLoss.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-float-to-integer QueryFloatToInteger.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-boolean-to-number QueryBooleanToNumber.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-numeric-like QueryNumericLike.cpp "ORM_QUERY_STRING")
orm_cxx_expect_compile_failure(query-text-sum QueryTextSum.cpp "ORM_QUERY_NUMERIC")
orm_cxx_expect_compile_failure(query-non-nullable-null QueryNonNullableNull.cpp "ORM_QUERY_NULLABLE")
orm_cxx_expect_compile_failure(query-optional-filter-value QueryOptionalFilterValue.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-list-value-type QueryListValueType.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-between-value-type QueryBetweenValueType.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-update-wrong-model QueryUpdateWrongModel.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-update-null-not-null QueryUpdateNullNotNull.cpp "ORM_QUERY_NULLABLE")
orm_cxx_expect_compile_failure(query-update-narrowing QueryUpdateNarrowing.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-update-related-non-key QueryUpdateRelatedNonKey.cpp "ORM_QUERY_WRITE_SAFE")
orm_cxx_expect_compile_failure(query-write-related-non-key QueryWriteRelatedNonKey.cpp "ORM_QUERY_WRITE_SAFE")
orm_cxx_expect_compile_failure(query-raw-wrong-model QueryRawWrongModel.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-raw-order-wrong-model QueryRawOrderWrongModel.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-collection-wrong-predicate QueryCollectionWrongPredicate.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-nested-collection QueryNestedCollection.cpp "ORM_QUERY_NO_NESTED_COLLECTION")
orm_cxx_expect_compile_failure(query-exists-scalar QueryExistsScalar.cpp "ORM_QUERY_COLLECTION")
orm_cxx_expect_compile_failure(query-include-wrong-owner QueryIncludeWrongOwner.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-include-scalar QueryIncludeScalar.cpp "ORM_QUERY_COLLECTION")
orm_cxx_expect_compile_failure(query-link-wrong-owner QueryLinkWrongOwner.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-link-wrong-target QueryLinkWrongTarget.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-unlink-wrong-target QueryUnlinkWrongTarget.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-removed-string-column QueryRemovedStringColumn.cpp "QueryRemovedStringColumn.cpp")
orm_cxx_expect_compile_failure(query-removed-string-field QueryRemovedStringField.cpp "QueryRemovedStringField.cpp")
orm_cxx_expect_compile_failure(query-removed-string-any QueryRemovedStringAny.cpp "QueryRemovedStringAny.cpp")
orm_cxx_expect_compile_failure(query-removed-string-none QueryRemovedStringNone.cpp "QueryRemovedStringNone.cpp")
orm_cxx_expect_compile_failure(query-removed-string-exists QueryRemovedStringExists.cpp "QueryRemovedStringExists.cpp")
orm_cxx_expect_compile_failure(
    query-removed-string-include QueryRemovedStringInclude.cpp "QueryRemovedStringInclude.cpp"
)
orm_cxx_expect_compile_failure(query-removed-string-link QueryRemovedStringLink.cpp "QueryRemovedStringLink.cpp")
orm_cxx_expect_compile_failure(query-removed-string-unlink QueryRemovedStringUnlink.cpp "QueryRemovedStringUnlink.cpp")
orm_cxx_expect_compile_failure(query-removed-untyped-raw QueryRemovedUntypedRaw.cpp "QueryRemovedUntypedRaw.cpp")
orm_cxx_expect_compile_failure(
    query-removed-untyped-raw-order QueryRemovedUntypedRawOrder.cpp "QueryRemovedUntypedRawOrder.cpp"
)
orm_cxx_expect_compile_failure(
    query-removed-untyped-count-all QueryRemovedUntypedCountAll.cpp "QueryRemovedUntypedCountAll.cpp"
)
orm_cxx_expect_compile_failure(query-member-function QueryMemberFunction.cpp "ORM_QUERY_MEMBER")
orm_cxx_expect_compile_failure(query-non-member-function QueryNonMemberFunction.cpp "ORM_QUERY_MEMBER")
orm_cxx_expect_compile_failure(query-static-member QueryStaticMember.cpp "ORM_QUERY_MEMBER")
orm_cxx_expect_compile_failure(query-null-member QueryNullMember.cpp "ORM_QUERY_MEMBER")
orm_cxx_expect_compile_failure(query-unsupported-field QueryUnsupportedField.cpp "ORM_QUERY_COLUMN")
orm_cxx_expect_compile_failure(query-boolean-ordering QueryBooleanOrdering.cpp "ORM_QUERY_ORDERABLE")
orm_cxx_expect_compile_failure(query-boolean-between QueryBooleanBetween.cpp "ORM_QUERY_ORDERABLE")
orm_cxx_expect_compile_failure(query-boolean-sum QueryBooleanSum.cpp "ORM_QUERY_NUMERIC")
orm_cxx_expect_compile_failure(query-aggregate-narrowing QueryAggregateNarrowing.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-aggregate-floating-count QueryAggregateFloatingCount.cpp "ORM_QUERY_VALUE")
orm_cxx_expect_compile_failure(query-composed-wrong-model-or QueryComposedWrongModelOr.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(query-negated-wrong-model QueryNegatedWrongModel.cpp "ORM_QUERY_MODEL")
orm_cxx_expect_compile_failure(
    query-aggregate-composed-wrong-model QueryAggregateComposedWrongModel.cpp "ORM_QUERY_MODEL"
)
orm_cxx_expect_compile_failure(
    query-aggregate-composed-wrong-model-or QueryAggregateComposedWrongModelOr.cpp "ORM_QUERY_MODEL"
)
orm_cxx_expect_compile_failure(
    query-aggregate-negated-wrong-model QueryAggregateNegatedWrongModel.cpp "ORM_QUERY_MODEL"
)
orm_cxx_expect_compile_failure(
    query-write-composed-related-non-key QueryWriteComposedRelatedNonKey.cpp "ORM_QUERY_WRITE_SAFE"
)
orm_cxx_expect_compile_failure(
    query-write-disjunction-related-non-key QueryWriteDisjunctionRelatedNonKey.cpp "ORM_QUERY_WRITE_SAFE"
)
orm_cxx_expect_compile_failure(
    query-write-negated-related-non-key QueryWriteNegatedRelatedNonKey.cpp "ORM_QUERY_WRITE_SAFE"
)
orm_cxx_expect_compile_failure(
    query-nested-collection-composed QueryNestedCollectionComposed.cpp "ORM_QUERY_NO_NESTED_COLLECTION"
)
orm_cxx_expect_compile_failure(
    query-nested-collection-negated QueryNestedCollectionNegated.cpp "ORM_QUERY_NO_NESTED_COLLECTION"
)
orm_cxx_expect_compile_failure(query-private-column-construction QueryPrivateColumnConstruction.cpp "private")
orm_cxx_expect_compile_failure(query-private-predicate-construction QueryPrivatePredicateConstruction.cpp "private")
orm_cxx_expect_compile_failure(query-private-order-construction QueryPrivateOrderConstruction.cpp "private")
orm_cxx_expect_compile_failure(query-private-projection-construction QueryPrivateProjectionConstruction.cpp "private")
orm_cxx_expect_compile_failure(query-private-aggregate-construction QueryPrivateAggregateConstruction.cpp "private")
orm_cxx_expect_compile_failure(
    query-private-aggregate-predicate-construction QueryPrivateAggregatePredicateConstruction.cpp "private"
)
orm_cxx_expect_compile_failure(query-internal-ast-predicate QueryInternalAstPredicate.cpp "PredicateFor")
orm_cxx_expect_compile_failure(query-internal-ast-order QueryInternalAstOrder.cpp "OrderFor")
orm_cxx_expect_compile_failure(query-internal-ast-group QueryInternalAstGroup.cpp "ColumnFor")
orm_cxx_expect_compile_failure(query-internal-ast-having QueryInternalAstHaving.cpp "AggregatePredicateFor")
orm_cxx_expect_compile_failure(query-internal-ast-projection QueryInternalAstProjection.cpp "ProjectionFor")
orm_cxx_expect_compile_failure(query-internal-ast-update QueryInternalAstUpdate.cpp "set")
orm_cxx_expect_compile_failure(query-internal-ast-remove QueryInternalAstRemove.cpp "PredicateFor")
orm_cxx_expect_compile_failure(query-impersonated-predicate QueryImpersonatedPredicate.cpp "PredicateFor")

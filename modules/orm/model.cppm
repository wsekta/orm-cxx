module;

// Make comparison types visible to MSVC when it imports std::optional.
#include <compare>

export module orm:model;

export import :model_mapping;
export import :model_views;
export import :model_columns;
export import :model_relations;
export import :model_schema;

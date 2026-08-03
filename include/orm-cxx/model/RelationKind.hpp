#pragma once

namespace orm::model
{
enum class RelationKind
{
    ToOne,
    OneToMany,
    ManyToMany,
};
} // namespace orm::model

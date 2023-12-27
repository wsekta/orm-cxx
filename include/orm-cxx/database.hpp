#pragma once

#include "model.hpp"
#include "query.hpp"

namespace orm
{
class Database
{
public:
    Database() = default;

    void connect(std::string const& connectionString);
};
}
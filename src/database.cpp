#include "orm-cxx/database.hpp"

#include "soci/soci.h"
#include "soci/empty/soci-empty.h"

namespace orm
{
void Database::connect(const std::string& connectionString)
{
    sql.open(connectionString);
}
}

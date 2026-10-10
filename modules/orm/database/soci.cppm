module;

#include "soci/soci.h"
#include "soci/type-conversion.h"
#include "soci/values.h"

export module orm:database_soci;

// Share SOCI's full declarations across the binding partitions. The using
// declarations expose the original global-module entities, retaining SOCI's ABI.
export namespace soci
{
using ::soci::dt_double;
using ::soci::dt_integer;
using ::soci::dt_long_long;
using ::soci::dt_string;
using ::soci::dt_unsigned_long_long;
using ::soci::i_null;
using ::soci::i_ok;
using ::soci::indicator;
using ::soci::rowset;
using ::soci::session;
using ::soci::soci_error;
using ::soci::transaction;
using ::soci::use;
using ::soci::values;
}

// The primary type_conversion template stays in its ordinary header beside the
// specializations. Re-exporting it here duplicates MSVC's specialization lookup
// when a consumer also includes SOCI's headers.

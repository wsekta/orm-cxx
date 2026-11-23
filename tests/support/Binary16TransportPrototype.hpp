#pragma once

// Temporary CI feasibility probe for UUID Binary16 transport through SOCI.
// This deliberately bypasses ORM binding and uses a session-owned blob directly.
#include <array>
#include <gtest/gtest.h>
#include <soci/soci.h>
#include <string>

namespace binary16_transport_prototype
{
inline auto run(soci::session& session, bool sqlite) -> void
{
    session << (sqlite ? "CREATE TEMPORARY TABLE uuid_binary_probe (id INTEGER PRIMARY KEY, value BLOB)" :
                         "CREATE TEMPORARY TABLE uuid_binary_probe (id INTEGER PRIMARY KEY, value BINARY(16))");
    soci::blob input{session};
    soci::indicator inputIndicator = soci::i_ok;
    int id{};
    soci::statement insert = (session.prepare << "INSERT INTO uuid_binary_probe VALUES (:id, :value)",
                              soci::use(id, "id"), soci::use(input, inputIndicator, "value"));
    std::array<unsigned char, 16> expected{};
    for (id = 0; id < 16; ++id)
    {
        for (int index = 0; index < 16; ++index)
            expected[static_cast<std::size_t>(index)] = static_cast<unsigned char>(id * 16 + index);
        input.trim(0);
        ASSERT_EQ(input.write_from_start(expected.data(), expected.size()), expected.size());
        insert.execute(true);
    }
    // The prepared statement retains references to both values until execute().
    id = 16;                       // NOLINT(clang-analyzer-deadcode.DeadStores)
    inputIndicator = soci::i_null; // NOLINT(clang-analyzer-deadcode.DeadStores)
    insert.execute(true);

    soci::blob output{session};
    for (id = 0; id < 16; ++id)
    {
        soci::indicator outputIndicator = soci::i_null;
        int length{};
        std::string storageType;
        session << (sqlite ? "SELECT value, length(value), typeof(value) FROM uuid_binary_probe WHERE id=:id" :
                             "SELECT value, OCTET_LENGTH(value), 'binary' FROM uuid_binary_probe WHERE id=:id"),
            soci::into(output, outputIndicator), soci::into(length), soci::into(storageType), soci::use(id, "id");
        ASSERT_EQ(outputIndicator, soci::i_ok);
        ASSERT_EQ(length, 16);
        ASSERT_EQ(output.get_len(), 16U);
        EXPECT_EQ(storageType, sqlite ? "blob" : "binary");
        std::array<unsigned char, 16> actual{};
        ASSERT_EQ(output.read_from_start(actual.data(), actual.size()), actual.size());
        for (int index = 0; index < 16; ++index)
            expected[static_cast<std::size_t>(index)] = static_cast<unsigned char>(id * 16 + index);
        EXPECT_EQ(actual, expected);
    }
    soci::indicator outputIndicator = soci::i_ok;
    id = 16;
    session << "SELECT value FROM uuid_binary_probe WHERE id=:id", soci::into(output, outputIndicator),
        soci::use(id, "id");
    EXPECT_EQ(outputIndicator, soci::i_null);

    if (sqlite)
    {
        // SQLite can store TEXT and short BLOBs in a BLOB-affinity column; decoding must inspect both.
        session << "INSERT INTO uuid_binary_probe VALUES (17, 'abcdefghijklmnop'), (18, X'000102')";
        std::string storageType;
        int length{};
        session << "SELECT typeof(value), length(value) FROM uuid_binary_probe WHERE id=17", soci::into(storageType),
            soci::into(length);
        EXPECT_EQ(storageType, "text");
        EXPECT_EQ(length, 16);
        session << "SELECT typeof(value), length(value) FROM uuid_binary_probe WHERE id=18", soci::into(storageType),
            soci::into(length);
        EXPECT_EQ(storageType, "blob");
        EXPECT_EQ(length, 3);
    }
    else
    {
        // BINARY(16) pads short inputs, so the original input length cannot be recovered on read.
        session << "INSERT INTO uuid_binary_probe VALUES (17, 0x000102)";
        id = 17;
        outputIndicator = soci::i_null;
        session << "SELECT value FROM uuid_binary_probe WHERE id=:id", soci::into(output, outputIndicator),
            soci::use(id, "id");
        ASSERT_EQ(outputIndicator, soci::i_ok);
        ASSERT_EQ(output.get_len(), 16U);
        std::array<unsigned char, 16> actual{};
        ASSERT_EQ(output.read_from_start(actual.data(), actual.size()), actual.size());
        std::array<unsigned char, 16> padded{};
        padded[1] = 1;
        padded[2] = 2;
        EXPECT_EQ(actual, padded);
    }
}
} // namespace binary16_transport_prototype

#include <catch2/catch_test_macros.hpp>
#include "trace/TraceEvent.h"
#include <cctype>

TEST_CASE("generate_uuid produces RFC 4122 v4 UUIDs") {
    const auto id = ea::trace::generate_uuid();
    REQUIRE(id.size() == 36);
    REQUIRE(id[8] == '-');
    REQUIRE(id[13] == '-');
    REQUIRE(id[18] == '-');
    REQUIRE(id[23] == '-');
    REQUIRE(id[14] == '4');
    REQUIRE((id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'b'));
    for (const char c : id) {
        if (c != '-') REQUIRE(std::isxdigit(static_cast<unsigned char>(c)));
    }

    std::string first = id, second;
    for (int i = 0; i < 100; ++i) {
        second = ea::trace::generate_uuid();
        REQUIRE(second != first);
    }
}

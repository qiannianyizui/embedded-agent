#include <catch2/catch_test_macros.hpp>
#include "provider/CredentialPool.h"

using namespace ea::provider;

TEST_CASE("CredentialPool acquire returns slot", "[credential_pool]") {
    CredentialPool pool;
    pool.add({"key1", "model-a"});
    auto slot = pool.acquire();
    REQUIRE(slot.ok());
    REQUIRE(slot.value().api_key == "key1");
    REQUIRE(slot.value().model == "model-a");
    REQUIRE(slot.value().healthy == true);
}

TEST_CASE("CredentialPool rotates through slots", "[credential_pool]") {
    CredentialPool pool;
    pool.add({"key1", "model-a"});
    pool.add({"key2", "model-b"});

    auto s1 = pool.acquire();
    REQUIRE(s1.value().api_key == "key1");
    pool.release(s1.value(), true);

    auto s2 = pool.acquire();
    REQUIRE(s2.value().api_key == "key2");
    pool.release(s2.value(), true);

    auto s3 = pool.acquire();
    REQUIRE(s3.value().api_key == "key1");
    pool.release(s3.value(), true);
}

TEST_CASE("CredentialPool skips unhealthy slots", "[credential_pool]") {
    CredentialPool pool;
    pool.add({"key1", "model-a"});
    pool.add({"key2", "model-b"});

    auto s1 = pool.acquire();
    REQUIRE(s1.value().api_key == "key1");
    pool.release(s1.value(), false);

    auto s2 = pool.acquire();
    REQUIRE(s2.value().api_key == "key2");
    pool.release(s2.value(), true);
}

TEST_CASE("CredentialPool empty pool returns error", "[credential_pool]") {
    CredentialPool pool;
    auto slot = pool.acquire();
    REQUIRE_FALSE(slot.ok());
}

TEST_CASE("CredentialPool all unhealthy returns error", "[credential_pool]") {
    CredentialPool pool;
    pool.add({"key1", "model-a"});

    // Need 3 consecutive errors to mark slot as unhealthy
    for (int i = 0; i < 3; ++i) {
        auto s = pool.acquire();
        REQUIRE(s.ok());
        pool.release(s.value(), false);
    }

    auto s2 = pool.acquire();
    REQUIRE_FALSE(s2.ok());
}

// tests/test_usage_store.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "budget/SqliteUsageStore.h"
#include <cstdio>
#include <filesystem>
#include <random>
#include <atomic>

using namespace ea;
using namespace ea::budget;

// Helper: create a temp DB path, auto-cleanup
struct TempUsageStore {
    std::string path;
    SqliteUsageStore store;

    TempUsageStore()
        : path(make_temp_path("_usage.db")),
          store(SqliteUsageStore::Config{path, false}) {
        auto r = store.open();
        REQUIRE(r.ok());
    }
    ~TempUsageStore() {
        store.close();
        std::remove(path.c_str());
    }

private:
    static std::string make_temp_path(const std::string& suffix) {
        static std::atomic<unsigned> counter{0};
        auto dir = std::filesystem::temp_directory_path();
        std::random_device rd;
        unsigned val = rd() + counter.fetch_add(1);
        auto p = dir / ("ea_test_" + std::to_string(val) + suffix);
        return p.string();
    }
};

static UsageRecord make_record(const std::string& session_id,
                                const std::string& model,
                                int input, int output,
                                int cache_read = 0, int cache_write = 0,
                                double cost = 0.0) {
    UsageRecord rec;
    rec.session_id = session_id;
    rec.model = model;
    rec.input_tokens = input;
    rec.output_tokens = output;
    rec.cache_read_tokens = cache_read;
    rec.cache_write_tokens = cache_write;
    rec.cost_usd = cost;
    return rec;
}

TEST_CASE("UsageStore record returns valid ID", "[budget]") {
    TempUsageStore t;
    auto r = t.store.record(make_record("s1", "gpt-4", 100, 50, 0, 0, 0.01));
    REQUIRE(r.ok());
    REQUIRE(r.value().substr(0, 6) == "usage_");
    REQUIRE(r.value().size() == 14);  // "usage_" + 8 hex
}

TEST_CASE("UsageStore record and query", "[budget]") {
    TempUsageStore t;
    t.store.record(make_record("s1", "gpt-4", 100, 50, 0, 0, 0.01));
    t.store.record(make_record("s1", "gpt-4", 200, 100, 0, 0, 0.02));
    t.store.record(make_record("s1", "gpt-4", 300, 150, 0, 0, 0.03));

    auto r = t.store.query();
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 3);
}

TEST_CASE("UsageStore query by session_id", "[budget]") {
    TempUsageStore t;
    t.store.record(make_record("session_a", "gpt-4", 100, 50));
    t.store.record(make_record("session_b", "gpt-4", 200, 100));
    t.store.record(make_record("session_a", "gpt-4", 300, 150));
    t.store.record(make_record("session_b", "gpt-4", 400, 200));

    auto r = t.store.query("session_a");
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 2);
    for (const auto& rec : r.value()) {
        REQUIRE(rec.session_id == "session_a");
    }
}

TEST_CASE("UsageStore query with limit/offset", "[budget]") {
    TempUsageStore t;
    for (int i = 0; i < 5; ++i) {
        t.store.record(make_record("s1", "gpt-4", (i + 1) * 100, (i + 1) * 50));
    }

    auto page1 = t.store.query("", 2, 0);
    REQUIRE(page1.ok());
    REQUIRE(page1.value().size() == 2);

    auto page2 = t.store.query("", 2, 2);
    REQUIRE(page2.ok());
    REQUIRE(page2.value().size() == 2);

    // No overlap — IDs must differ
    REQUIRE(page1.value()[0].id != page2.value()[0].id);
    REQUIRE(page1.value()[1].id != page2.value()[1].id);
}

TEST_CASE("UsageStore total_usage aggregation", "[budget]") {
    TempUsageStore t;
    t.store.record(make_record("s1", "gpt-4", 100, 50, 10, 5));
    t.store.record(make_record("s1", "gpt-4", 200, 100, 20, 10));
    t.store.record(make_record("s1", "gpt-4", 300, 150, 30, 15));

    auto r = t.store.total_usage();
    REQUIRE(r.ok());
    REQUIRE(r.value().input_tokens == 600);
    REQUIRE(r.value().output_tokens == 300);
    REQUIRE(r.value().cache_read_tokens == 60);
    REQUIRE(r.value().cache_write_tokens == 30);
}

TEST_CASE("UsageStore total_cost aggregation", "[budget]") {
    TempUsageStore t;
    t.store.record(make_record("s1", "gpt-4", 100, 50, 0, 0, 0.01));
    t.store.record(make_record("s1", "gpt-4", 200, 100, 0, 0, 0.02));
    t.store.record(make_record("s1", "gpt-4", 300, 150, 0, 0, 0.03));

    auto r = t.store.total_cost();
    REQUIRE(r.ok());
    REQUIRE(r.value().total() == Catch::Approx(0.06));
}

TEST_CASE("UsageStore total_usage by session_id", "[budget]") {
    TempUsageStore t;
    t.store.record(make_record("session_a", "gpt-4", 100, 50));
    t.store.record(make_record("session_b", "gpt-4", 200, 100));
    t.store.record(make_record("session_a", "gpt-4", 300, 150));

    auto r = t.store.total_usage("session_a");
    REQUIRE(r.ok());
    REQUIRE(r.value().input_tokens == 400);
    REQUIRE(r.value().output_tokens == 200);

    auto r2 = t.store.total_usage("session_b");
    REQUIRE(r2.ok());
    REQUIRE(r2.value().input_tokens == 200);
    REQUIRE(r2.value().output_tokens == 100);
}

TEST_CASE("UsageStore clear by session_id", "[budget]") {
    TempUsageStore t;
    t.store.record(make_record("session_a", "gpt-4", 100, 50));
    t.store.record(make_record("session_b", "gpt-4", 200, 100));
    t.store.record(make_record("session_a", "gpt-4", 300, 150));

    auto r = t.store.clear("session_a");
    REQUIRE(r.ok());
    REQUIRE(r.value() == true);  // rows were deleted

    // session_a records gone
    auto q = t.store.query("session_a");
    REQUIRE(q.ok());
    REQUIRE(q.value().empty());

    // session_b records remain
    auto q2 = t.store.query("session_b");
    REQUIRE(q2.ok());
    REQUIRE(q2.value().size() == 1);
}

TEST_CASE("UsageStore clear all", "[budget]") {
    TempUsageStore t;
    t.store.record(make_record("s1", "gpt-4", 100, 50));
    t.store.record(make_record("s2", "gpt-4", 200, 100));
    t.store.record(make_record("s3", "gpt-4", 300, 150));

    auto r = t.store.clear();
    REQUIRE(r.ok());
    REQUIRE(r.value() == true);

    auto q = t.store.query();
    REQUIRE(q.ok());
    REQUIRE(q.value().empty());
}

TEST_CASE("UsageStore query empty store", "[budget]") {
    TempUsageStore t;
    auto r = t.store.query();
    REQUIRE(r.ok());
    REQUIRE(r.value().empty());
}

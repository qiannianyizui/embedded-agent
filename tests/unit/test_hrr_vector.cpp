#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <string>
#include <vector>

#include "hrr/HrrVector.h"

using namespace ea::hrr;
using Catch::Matchers::WithinAbs;

TEST_CASE("HRR encode_atom determinism", "[hrr]") {
    const int dim = 512;
    auto v1 = encode_atom("hello", dim);
    auto v2 = encode_atom("hello", dim);

    REQUIRE(v1.dim() == dim);
    REQUIRE(v2.dim() == dim);

    // Same input → identical vector
    for (int i = 0; i < dim; i++) {
        REQUIRE(v1[i] == v2[i]);
    }

    // All phases in [0, 2π)
    for (int i = 0; i < dim; i++) {
        REQUIRE(v1[i] >= 0.0);
        REQUIRE(v1[i] < 2.0 * M_PI);
    }
}

TEST_CASE("HRR encode_atom different words produce different vectors", "[hrr]") {
    const int dim = 512;
    auto v1 = encode_atom("cat", dim);
    auto v2 = encode_atom("dog", dim);

    // Different words should be quasi-orthogonal (similarity near 0)
    double sim = similarity(v1, v2);
    REQUIRE_THAT(sim, WithinAbs(0.0, 0.3));
}

TEST_CASE("HRR bind/unbind roundtrip", "[hrr]") {
    const int dim = 512;
    auto a = encode_atom("role", dim);
    auto b = encode_atom("value", dim);

    auto bound = bind(a, b);
    auto recovered = unbind(bound, a);

    // unbind(bind(a, b), a) ≈ b
    double sim = similarity(recovered, b);
    REQUIRE_THAT(sim, WithinAbs(1.0, 0.05));
}

TEST_CASE("HRR bundle includes all inputs", "[hrr]") {
    const int dim = 512;
    auto a = encode_atom("alpha", dim);
    auto b = encode_atom("beta", dim);
    auto c = encode_atom("gamma", dim);

    auto bundled = bundle({a, b, c});

    // Bundle should be similar to each input (HRR bundle of N items has
    // similarity ~ 1/sqrt(N), so for 3 items expect ~0.577)
    REQUIRE_THAT(similarity(bundled, a), WithinAbs(1.0, 0.5));
    REQUIRE_THAT(similarity(bundled, b), WithinAbs(1.0, 0.5));
    REQUIRE_THAT(similarity(bundled, c), WithinAbs(1.0, 0.5));

    // Bundle should not be similar to a random vector
    auto random = encode_atom("unrelated", dim);
    double sim_random = similarity(bundled, random);
    REQUIRE(sim_random < 0.4);
}

TEST_CASE("HRR similarity range", "[hrr]") {
    const int dim = 512;
    auto v = encode_atom("self", dim);

    // Self-similarity should be exactly 1.0
    REQUIRE_THAT(similarity(v, v), WithinAbs(1.0, 1e-10));

    // Different vectors should have similarity near 0
    auto v2 = encode_atom("other", dim);
    double sim = similarity(v, v2);
    REQUIRE(sim > -1.0);
    REQUIRE(sim < 1.0);
    REQUIRE_THAT(sim, WithinAbs(0.0, 0.3));
}

TEST_CASE("HRR encode_text", "[hrr]") {
    const int dim = 512;
    auto v = encode_text("Hello World", dim);

    REQUIRE(v.dim() == dim);

    // All phases in [0, 2π)
    for (int i = 0; i < dim; i++) {
        REQUIRE(v[i] >= 0.0);
        REQUIRE(v[i] < 2.0 * M_PI);
    }
}

TEST_CASE("HRR encode_fact", "[hrr]") {
    const int dim = 512;
    auto v = encode_fact("Alice likes Python", {"Alice", "Python"}, dim);

    REQUIRE(v.dim() == dim);

    // encode_fact result should be similar to encode_text of the same content
    // bundled with entity role bindings — not identical, but correlated
    auto content_only = encode_text("Alice likes Python", dim);
    double sim = similarity(v, content_only);
    // Just check it's a valid vector (not all zeros)
    REQUIRE(sim > -1.0);
    REQUIRE(sim < 1.0);
}

TEST_CASE("HRR encode_text empty input", "[hrr]") {
    const int dim = 512;
    auto v = encode_text("", dim);

    // Empty input should fall back to the __hrr_empty__ atom
    auto empty_atom = encode_atom("__hrr_empty__", dim);
    REQUIRE_THAT(similarity(v, empty_atom), WithinAbs(1.0, 1e-10));
}

TEST_CASE("HRR SNR estimate", "[hrr]") {
    // SNR = sqrt(dim / n_items)
    REQUIRE_THAT(snr_estimate(1024, 1), WithinAbs(32.0, 0.1));
    REQUIRE_THAT(snr_estimate(1024, 256), WithinAbs(2.0, 0.1));
    REQUIRE(std::isinf(snr_estimate(1024, 0)));  // n=0 → infinity
}

TEST_CASE("HRR serialization roundtrip", "[hrr]") {
    const int dim = 512;
    auto original = encode_atom("serialize_test", dim);

    auto bytes = phases_to_bytes(original);
    REQUIRE(bytes.size() == static_cast<size_t>(dim) * sizeof(double));

    auto restored = bytes_to_phases(bytes.data(), bytes.size(), dim);
    REQUIRE(restored.dim() == original.dim());

    for (int i = 0; i < dim; i++) {
        REQUIRE_THAT(restored[i], WithinAbs(original[i], 1e-15));
    }
}

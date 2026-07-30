#include "HrrVector.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <stdexcept>

#include <mbedtls/sha256.h>

namespace ea::hrr {

static constexpr double kTwoPi = 2.0 * M_PI;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HrrVector::HrrVector(int dim)
    : phases_(static_cast<size_t>(dim), 0.0), dim_(dim) {}

HrrVector::HrrVector(int dim, std::vector<double> phases)
    : phases_(std::move(phases)), dim_(dim) {
    if (static_cast<int>(phases_.size()) != dim_) {
        throw std::invalid_argument("HrrVector: phase count mismatch");
    }
}

// ---------------------------------------------------------------------------
// Algebraic operations
// ---------------------------------------------------------------------------

HrrVector bind(const HrrVector& a, const HrrVector& b) {
    if (a.dim() != b.dim()) {
        throw std::invalid_argument("bind: dimension mismatch");
    }
    HrrVector result(a.dim());
    for (int i = 0; i < a.dim(); i++) {
        result[i] = fmod(a[i] + b[i], kTwoPi);
        if (result[i] < 0.0) result[i] += kTwoPi;
    }
    return result;
}

HrrVector unbind(const HrrVector& memory, const HrrVector& key) {
    if (memory.dim() != key.dim()) {
        throw std::invalid_argument("unbind: dimension mismatch");
    }
    HrrVector result(memory.dim());
    for (int i = 0; i < memory.dim(); i++) {
        result[i] = fmod(memory[i] - key[i], kTwoPi);
        if (result[i] < 0.0) result[i] += kTwoPi;
    }
    return result;
}

HrrVector bundle(const std::vector<HrrVector>& vectors) {
    if (vectors.empty()) {
        throw std::invalid_argument("bundle: need at least one vector");
    }
    int dim = vectors[0].dim();
    HrrVector result(dim);
    for (int i = 0; i < dim; i++) {
        double re = 0.0, im = 0.0;
        for (const auto& v : vectors) {
            re += std::cos(v[i]);
            im += std::sin(v[i]);
        }
        result[i] = std::atan2(im, re);
        if (result[i] < 0.0) result[i] += kTwoPi;
    }
    return result;
}

double similarity(const HrrVector& a, const HrrVector& b) {
    if (a.dim() != b.dim()) {
        throw std::invalid_argument("similarity: dimension mismatch");
    }
    double sum = 0.0;
    for (int i = 0; i < a.dim(); i++) {
        sum += std::cos(a[i] - b[i]);
    }
    return sum / static_cast<double>(a.dim());
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

HrrVector encode_atom(const std::string& word, int dim) {
    // Each SHA-256 digest is 32 bytes = 16 uint16 values.
    static constexpr int kValuesPerBlock = 16;
    const int blocks_needed = (dim + kValuesPerBlock - 1) / kValuesPerBlock;

    std::vector<double> phases;
    phases.reserve(static_cast<size_t>(dim));

    for (int blk = 0; blk < blocks_needed; blk++) {
        std::string input = word + ":" + std::to_string(blk);
        unsigned char digest[32];
        mbedtls_sha256(
            reinterpret_cast<const unsigned char*>(input.data()),
            input.size(),
            digest,
            0);  // 0 = SHA-256 (not SHA-224)

        // Unpack 16 uint16_t values from 32 bytes (little-endian)
        for (int j = 0; j < kValuesPerBlock; j++) {
            uint16_t val = static_cast<uint16_t>(digest[j * 2])
                         | (static_cast<uint16_t>(digest[j * 2 + 1]) << 8);
            phases.push_back(static_cast<double>(val) * (kTwoPi / 65536.0));
        }
    }

    phases.resize(static_cast<size_t>(dim));
    return HrrVector(dim, std::move(phases));
}

HrrVector encode_text(const std::string& text, int dim) {
    // Tokenize: lowercase, split on whitespace, strip punctuation
    std::vector<HrrVector> atoms;
    std::string lowered;
    lowered.reserve(text.size());
    for (auto c : text) lowered.push_back(static_cast<char>(std::tolower(c)));

    std::string token;
    for (auto c : lowered) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!token.empty()) {
                // Strip leading/trailing punctuation
                size_t start = 0, end = token.size();
                while (start < end && std::ispunct(static_cast<unsigned char>(token[start]))) start++;
                while (end > start && std::ispunct(static_cast<unsigned char>(token[end - 1]))) end--;
                if (start < end) {
                    atoms.push_back(encode_atom(token.substr(start, end - start), dim));
                }
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    // Last token
    if (!token.empty()) {
        size_t start = 0, end = token.size();
        while (start < end && std::ispunct(static_cast<unsigned char>(token[start]))) start++;
        while (end > start && std::ispunct(static_cast<unsigned char>(token[end - 1]))) end--;
        if (start < end) {
            atoms.push_back(encode_atom(token.substr(start, end - start), dim));
        }
    }

    if (atoms.empty()) {
        return encode_atom("__hrr_empty__", dim);
    }
    return bundle(atoms);
}

HrrVector encode_fact(const std::string& content,
                      const std::vector<std::string>& entities,
                      int dim) {
    // Role vectors are reserved atoms
    HrrVector role_content = encode_atom("__hrr_role_content__", dim);
    HrrVector role_entity = encode_atom("__hrr_role_entity__", dim);

    std::vector<HrrVector> components;
    components.reserve(1 + entities.size());

    // bind(encode_text(content), ROLE_CONTENT)
    components.push_back(bind(encode_text(content, dim), role_content));

    // For each entity: bind(encode_atom(entity), ROLE_ENTITY)
    for (const auto& entity : entities) {
        components.push_back(bind(encode_atom(entity, dim), role_entity));
    }

    return bundle(components);
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

double snr_estimate(int dim, int n_items) {
    if (n_items <= 0) return std::numeric_limits<double>::infinity();
    return std::sqrt(static_cast<double>(dim) / static_cast<double>(n_items));
}

std::vector<uint8_t> phases_to_bytes(const HrrVector& v) {
    const size_t count = static_cast<size_t>(v.dim());
    std::vector<uint8_t> bytes(count * sizeof(double));
    std::memcpy(bytes.data(), v.data(), bytes.size());
    return bytes;
}

HrrVector bytes_to_phases(const uint8_t* data, size_t len, int dim) {
    const size_t expected = static_cast<size_t>(dim) * sizeof(double);
    if (len != expected) {
        throw std::invalid_argument("bytes_to_phases: size mismatch");
    }
    std::vector<double> phases(static_cast<size_t>(dim));
    std::memcpy(phases.data(), data, len);
    return HrrVector(dim, std::move(phases));
}

}  // namespace ea::hrr

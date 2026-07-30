#pragma once

// HRR — Holographic Reduced Representations with phase encoding.
//
// HRRs are a vector symbolic architecture for encoding compositional structure
// into fixed-width distributed representations. This module uses *phase vectors*:
// each concept is a vector of angles in [0, 2π). The algebraic operations are:
//
//   bind   — circular convolution (phase addition)  — associates two concepts
//   unbind — circular correlation  (phase subtraction) — retrieves a bound value
//   bundle — superposition (circular mean)           — merges multiple concepts
//
// Phase encoding is numerically stable, avoids the magnitude collapse of
// traditional complex-number HRRs, and maps cleanly to cosine similarity.
//
// Atoms are generated deterministically from SHA-256 so representations are
// identical across processes, machines, and language versions.
//
// References:
//   Plate (1995) — Holographic Reduced Representations
//   Gayler (2004) — Vector Symbolic Architectures answer Jackendoff's challenges

#include <cstdint>
#include <string>
#include <vector>

namespace ea::hrr {

/// A phase vector — the fundamental HRR data structure.
/// Stores angles in [0, 2π) as double-precision floats.
class HrrVector {
public:
    /// Create an uninitialized vector of the given dimension.
    explicit HrrVector(int dim = 1024);

    /// Create a vector from existing phase data.
    HrrVector(int dim, std::vector<double> phases);

    HrrVector(const HrrVector&) = default;
    HrrVector(HrrVector&&) noexcept = default;
    HrrVector& operator=(const HrrVector&) = default;
    HrrVector& operator=(HrrVector&&) noexcept = default;

    int dim() const { return dim_; }
    double& operator[](int i) { return phases_[static_cast<size_t>(i)]; }
    const double& operator[](int i) const { return phases_[static_cast<size_t>(i)]; }
    const double* data() const { return phases_.data(); }
    double* data() { return phases_.data(); }

private:
    std::vector<double> phases_;
    int dim_;
};

// --- Algebraic operations (return new vectors) ---

/// Circular convolution = element-wise phase addition.
///
/// Binding associates two concepts into a single composite vector.
/// The result is dissimilar to both inputs (quasi-orthogonal).
HrrVector bind(const HrrVector& a, const HrrVector& b);

/// Circular correlation = element-wise phase subtraction.
///
/// Unbinding retrieves the value associated with a key from a memory vector.
/// unbind(bind(a, b), a) ≈ b  (up to superposition noise)
HrrVector unbind(const HrrVector& memory, const HrrVector& key);

/// Superposition via circular mean of complex exponentials.
///
/// Bundling merges multiple vectors into one that is similar to each input.
/// The result can hold O(sqrt(dim)) items before similarity degrades.
HrrVector bundle(const std::vector<HrrVector>& vectors);

/// Phase cosine similarity. Range [-1, 1].
///
/// Returns 1.0 for identical vectors, near 0.0 for random (unrelated) vectors,
/// and -1.0 for perfectly anti-correlated vectors.
double similarity(const HrrVector& a, const HrrVector& b);

// --- Encoding ---

/// Deterministic phase vector via SHA-256 counter blocks.
///
/// Uses mbedtls SHA-256 (not random RNG) for cross-platform reproducibility.
///
/// Algorithm:
/// - Generate enough SHA-256 blocks by hashing "word:i" for i=0,1,2,...
/// - Concatenate digests, interpret as uint16 values via little-endian unpack
/// - Scale to [0, 2π): phases = values * (2π / 65536)
/// - Truncate to dim elements
HrrVector encode_atom(const std::string& word, int dim = 1024);

/// Bag-of-words: bundle of atom vectors for each token.
///
/// Tokenizes by lowercasing, splitting on whitespace, and stripping
/// leading/trailing punctuation from each token.
HrrVector encode_text(const std::string& text, int dim = 1024);

/// Structured encoding: content bound to ROLE_CONTENT, each entity bound to
/// ROLE_ENTITY, all bundled together.
///
/// This enables algebraic extraction:
///     unbind(fact, bind(entity, ROLE_ENTITY)) ≈ content_vector
HrrVector encode_fact(const std::string& content,
                      const std::vector<std::string>& entities,
                      int dim = 1024);

// --- Utilities ---

/// Signal-to-noise ratio estimate for holographic storage.
///
/// SNR = sqrt(dim / n_items) when n_items > 0, else inf.
double snr_estimate(int dim, int n_items);

/// Serialize phase vector to bytes (float64 tobytes — 8 KB at dim=1024).
std::vector<uint8_t> phases_to_bytes(const HrrVector& v);

/// Deserialize bytes back to phase vector.
HrrVector bytes_to_phases(const uint8_t* data, size_t len, int dim);

}  // namespace ea::hrr

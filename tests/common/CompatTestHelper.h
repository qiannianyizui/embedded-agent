// tests/common/CompatTestHelper.h
#pragma once
#include <catch2/catch_test_macros.hpp>

namespace ea::test {

// All subsystems are always enabled now (no compile-time toggles).
// This file is kept for backward compatibility with any test code
// that references the namespace.

}  // namespace ea::test

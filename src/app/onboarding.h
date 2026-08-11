#pragma once
#include "config/Config.h"
#include "base/Result.h"
#include <string>

namespace ea::app {

// Whether the first-run profile-build offer should fire this launch.
bool should_offer_profile(const config::AppConfig& cfg);

// Hermes-style consent-gated directive appended to the very first message.
std::string profile_build_directive();

// One-time latch: persist onboarding.profile_build_offered = true.
Result<void> mark_profile_offered(config::AppConfig& cfg);

}  // namespace ea::app

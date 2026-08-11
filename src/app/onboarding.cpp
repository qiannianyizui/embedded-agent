#include "app/onboarding.h"
#include "config/Config.h"
#include "log/Logger.h"

namespace ea::app {

bool should_offer_profile(const config::AppConfig& cfg) {
    return cfg.onboarding.profile_build && !cfg.onboarding.profile_build_offered;
}

std::string profile_build_directive() {
    return
        "\n\n[System note: This is the user's very first message ever. "
        "After a one-sentence introduction (mention /help shows commands), "
        "OFFER — do not assume — to build a short profile of them so you can "
        "be more useful, and explain they can decline or do it later. If and "
        "ONLY IF they accept:\n"
        "  1. Ask for whatever they're comfortable sharing (name, what they "
        "do, how they like you to work). Volunteered facts come first.\n"
        "  2. Before ANY external lookup, say what you intend to look up and "
        "get explicit consent for that step.\n"
        "  3. Save each confirmed, durable fact with the memory tool using "
        "target=\"user\" — keep entries compact and high-signal.\n"
        "If they decline at any point, stop immediately and continue normally. "
        "Keep the whole exchange light and conversational, not an interrogation.]";
}

Result<void> mark_profile_offered(config::AppConfig& cfg) {
    cfg.onboarding.profile_build_offered = true;
    auto r = config::save(cfg);
    if (!r.ok()) {
        EA_WARN("Failed to persist onboarding latch: {}", r.error().message);
        return r.error();
    }
    return {};
}

}  // namespace ea::app

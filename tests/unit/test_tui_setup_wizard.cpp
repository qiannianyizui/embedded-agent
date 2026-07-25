// Unit tests for SetupWizard component
#include <catch2/catch_test_macros.hpp>
#include "tui/SetupWizard.h"
#include "config/Config.h"

using namespace ea::tui;

TEST_CASE("WizardState default values", "[tui]") {
    WizardState state;
    REQUIRE(state.provider_type == "openai_compatible");
    REQUIRE(state.autonomy == "supervised");
    REQUIRE_FALSE(state.finished);
    REQUIRE_FALSE(state.cancelled);
    REQUIRE(state.current_step == WizardStep::Welcome);
}

TEST_CASE("make_setup_wizard returns non-null component", "[tui]") {
    WizardState state;
    auto comp = make_setup_wizard(state);
    REQUIRE(comp != nullptr);
}

TEST_CASE("build_config_from_wizard maps fields correctly", "[tui]") {
    WizardState state;
    state.provider_type = "anthropic";
    state.base_url = "https://api.anthropic.com";
    state.api_key = "sk-ant-test";
    state.default_model = "claude-sonnet-4-6";
    state.workspace = "/home/user/project";
    state.autonomy = "autonomous";

    auto cfg = build_config_from_wizard(state);
    REQUIRE(cfg.provider.type == "anthropic");
    REQUIRE(cfg.provider.base_url == "https://api.anthropic.com");
    REQUIRE(cfg.provider.api_key == "sk-ant-test");
    REQUIRE(cfg.provider.default_model == "claude-sonnet-4-6");
    REQUIRE(cfg.agent.model == "claude-sonnet-4-6");
    REQUIRE(cfg.security.workspace == "/home/user/project");
    REQUIRE(cfg.security.autonomy == "autonomous");
}

TEST_CASE("run_non_interactive_setup respects env vars", "[tui]") {
    // Set env vars for the test
    setenv("EMBEDDED_AGENT_API_KEY", "sk-env-test", 1);
    setenv("EMBEDDED_AGENT_MODEL", "gpt-4o", 1);
    setenv("EMBEDDED_AGENT_PROVIDER", "openai_compatible", 1);

    auto cfg = run_non_interactive_setup();
    REQUIRE(cfg.provider.api_key == "sk-env-test");
    REQUIRE(cfg.agent.model == "gpt-4o");
    REQUIRE(cfg.provider.type == "openai_compatible");

    // Cleanup env vars
    unsetenv("EMBEDDED_AGENT_API_KEY");
    unsetenv("EMBEDDED_AGENT_MODEL");
    unsetenv("EMBEDDED_AGENT_PROVIDER");
}

TEST_CASE("WizardStep enum values are sequential", "[tui]") {
    REQUIRE(static_cast<int>(WizardStep::Welcome) == 0);
    REQUIRE(static_cast<int>(WizardStep::Provider) == 1);
    REQUIRE(static_cast<int>(WizardStep::ApiKey) == 2);
    REQUIRE(static_cast<int>(WizardStep::Model) == 3);
    REQUIRE(static_cast<int>(WizardStep::Workspace) == 4);
    REQUIRE(static_cast<int>(WizardStep::Security) == 5);
    REQUIRE(static_cast<int>(WizardStep::Review) == 6);
    REQUIRE(static_cast<int>(WizardStep::Count) == 7);
}

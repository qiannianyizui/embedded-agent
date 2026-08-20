# tests/unit/CMakeLists.txt
add_executable(ea-unit-tests
    test_common_result.cpp
    test_common_string.cpp
    test_common_json.cpp
    test_common_sse.cpp
    test_config.cpp
    test_build_config.cpp
    test_core_types.cpp
    test_filesystem.cpp
    test_hrr_vector.cpp
    test_holographic_memory.cpp
    test_scoped_memory.cpp
    test_memory_manager.cpp
    test_memory_extractor.cpp
    test_memory_strategy.cpp
    test_provider_openai.cpp
    test_provider_anthropic.cpp
    test_provider_ollama.cpp
    test_reliable_provider.cpp
    test_router_provider.cpp
    test_credential_pool.cpp
    test_error_classifier.cpp
    test_prompt_guided_tools.cpp
    test_tool_registry.cpp
    test_toolset.cpp
    test_tool_shell.cpp
    test_tool_file.cpp
    test_tool_memory.cpp
    test_tool_web.cpp
    test_web_search.cpp
    test_tool_search.cpp
    test_tool_output_config.cpp
    test_curated_memory_store.cpp
    test_onboarding.cpp
    test_agent_loop.cpp
    test_system_prompt.cpp
    test_loop_detector.cpp
    test_context_compressor.cpp
    test_turn_context.cpp
    test_turn_steps.cpp
    test_subagent.cpp
    test_plan_mode.cpp
    test_streaming.cpp
    test_events.cpp
    test_trace_uuid.cpp
    test_security_policy.cpp
    test_approval_handler.cpp
    test_stdin_approval_handler.cpp
    test_pending_approval_handler.cpp
    test_conversation_store.cpp
    test_budget_tracker.cpp
    test_usage_store.cpp
    test_mcp_client.cpp
    test_skills.cpp
    test_retry_policy.cpp
    test_regression.cpp
    test_config_save.cpp
    test_app_builder.cpp
    test_auto_resume.cpp
)
target_link_libraries(ea-unit-tests PRIVATE
    ea-test-common
    ea-base ea-io ea-net ea-log ea-config ea-hrr ea-memory ea-conversation ea-budget
    ea-provider ea-tool ea-agent ea-trace ea-mcp ea-platform ea-security
    ea-app ea-skill ea-tui
    nlohmann_json::nlohmann_json
    httplib::httplib
    Catch2::Catch2WithMain
)
target_include_directories(ea-unit-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)
target_compile_definitions(ea-unit-tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)

# TUI unit tests
add_executable(ea-tui-tests
        test_tui_chat_area.cpp
        test_text_wrap.cpp
        test_tui_approval_handler.cpp
        test_tui_event_listener.cpp
        test_tui_command_palette.cpp
        test_tui_skills_dialog.cpp
        test_tui_input_bar.cpp
        test_tui_sessions_dialog.cpp
        test_tui_app.cpp
        test_tui_theme.cpp
        test_tui_spinner.cpp
        test_tui_setup_wizard.cpp
    )
    target_link_libraries(ea-tui-tests PRIVATE
        ea-test-common
        ea-base ea-io ea-net ea-log ea-config ea-security ea-memory ea-hrr
        ea-agent ea-trace ea-conversation ea-budget
        ea-provider ea-tool ea-skill ea-mcp ea-platform
        ea-tui
        ftxui::component
        nlohmann_json::nlohmann_json
        httplib::httplib
        Catch2::Catch2WithMain
    )
    target_include_directories(ea-tui-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/include
    )
    add_test(NAME ea-tui-tests COMMAND ea-tui-tests)

include(CTest)
add_test(NAME ea-unit-tests COMMAND ea-unit-tests)

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
    test_memory_sqlite.cpp
    test_in_memory_backend.cpp
    test_memory_factory.cpp
    test_scoped_memory.cpp
    test_memory_manager.cpp
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
    test_tool_search.cpp
    test_tool_output_config.cpp
    test_agent_loop.cpp
    test_system_prompt.cpp
    test_loop_detector.cpp
    test_context_compressor.cpp
    test_turn_context.cpp
    test_turn_steps.cpp
    test_subagent.cpp
    test_streaming.cpp
    test_events.cpp
    test_security_policy.cpp
    test_approval_handler.cpp
    test_stdin_approval_handler.cpp
    test_pending_approval_handler.cpp
    test_session_manager.cpp
    test_conversation_store.cpp
    test_budget_tracker.cpp
    test_usage_store.cpp
    test_mcp_client.cpp
    test_plugin_manifest.cpp
    test_plugin_loader.cpp
    test_plugin_tool_adapter.cpp
    test_plugin_host.cpp
    test_retry_policy.cpp
    test_regression.cpp
)
target_link_libraries(ea-unit-tests PRIVATE
    ea-test-common
    ea-common ea-config ea-memory ea-conversation ea-budget
    ea-provider ea-tool ea-agent ea-mcp ea-platform ea-security
    ea-server ea-plugin
    nlohmann_json::nlohmann_json
    httplib::httplib
    Catch2::Catch2WithMain
    ${CMAKE_DL_LIBS}
)
target_include_directories(ea-unit-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)
target_compile_definitions(ea-unit-tests PRIVATE
    EA_MOCK_PLUGIN_PATH="${CMAKE_BINARY_DIR}/tests/plugins/mock_plugin/ea_mock_plugin.so"
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)

# TUI unit tests (only when TUI is enabled)
if(EA_ENABLE_TUI AND NOT EA_MODE_EMBEDDED)
    add_executable(ea-tui-tests
        test_tui_chat_area.cpp
        test_tui_approval_handler.cpp
    )
    target_link_libraries(ea-tui-tests PRIVATE
        ea-test-common
        ea-common ea-config ea-core ea-security
        ea-tui
        ftxui::component
        nlohmann_json::nlohmann_json
        Catch2::Catch2WithMain
    )
    target_include_directories(ea-tui-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/include
    )
    add_test(NAME ea-tui-tests COMMAND ea-tui-tests)
endif()
include(CTest)
add_test(NAME ea-unit-tests COMMAND ea-unit-tests)

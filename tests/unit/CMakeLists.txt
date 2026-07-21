# tests/unit/CMakeLists.txt
add_executable(ea-unit-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_common_result.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_common_string.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_common_json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_common_sse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_build_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_core_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_filesystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_memory_sqlite.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_in_memory_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_memory_factory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_scoped_memory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_memory_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_memory_strategy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_provider_openai.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_provider_anthropic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_provider_ollama.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_reliable_provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_router_provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_credential_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_error_classifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_prompt_guided_tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_tool_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_toolset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_tool_shell.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_tool_file.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_tool_memory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_tool_web.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_tool_search.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_tool_output_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_agent_loop.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_system_prompt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_loop_detector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_context_compressor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_turn_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_turn_steps.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_subagent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_streaming.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_events.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_security_policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_approval_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_stdin_approval_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_pending_approval_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_session_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_conversation_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_budget_tracker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_usage_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_mcp_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_plugin_manifest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_plugin_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_plugin_tool_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_plugin_host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_retry_policy.cpp
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
include(CTest)
add_test(NAME ea-unit-tests COMMAND ea-unit-tests)

add_library(ea-provider OBJECT
    OpenAIProvider.cpp
    AnthropicProvider.cpp
    OllamaProvider.cpp
    ProviderFactory.cpp
    ErrorClassifier.cpp
    CredentialPool.cpp
    PromptGuidedTools.cpp
    ReliableProvider.cpp
    RouterProvider.cpp
)
ea_target_compile_options(ea-provider)
target_include_directories(ea-provider PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-provider PUBLIC ea-base ea-net ea-log)

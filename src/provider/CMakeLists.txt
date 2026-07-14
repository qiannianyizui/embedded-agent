add_library(ea-provider OBJECT
    OpenAIProvider.cpp
    OllamaProvider.cpp
    ProviderFactory.cpp
)
target_include_directories(ea-provider PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-provider PUBLIC ea-core ea-common)

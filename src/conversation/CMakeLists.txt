add_library(ea-conversation OBJECT
    SqliteConversationStore.cpp
)
ea_target_compile_options(ea-conversation)
target_include_directories(ea-conversation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-conversation PUBLIC ea-base ea-io ea-log sqlite3)

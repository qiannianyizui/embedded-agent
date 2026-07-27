add_library(ea-budget OBJECT
    SqliteUsageStore.cpp
    BudgetTracker.cpp
)
ea_target_compile_options(ea-budget)
target_include_directories(ea-budget PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-budget PUBLIC ea-base ea-io ea-log sqlite3)

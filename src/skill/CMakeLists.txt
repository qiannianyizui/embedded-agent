add_library(ea-skill OBJECT
    Skill.cpp
)
ea_target_compile_options(ea-skill)
target_include_directories(ea-skill PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-skill PUBLIC ea-base ea-io ea-log ea-platform)

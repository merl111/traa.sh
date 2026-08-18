cmake_minimum_required(VERSION 3.16)

add_executable(test_core unit/test_core.c
  ${CMAKE_SOURCE_DIR}/src/ui/overview_overlay.c
  ${CMAKE_SOURCE_DIR}/src/ui/quit_confirm.c)
target_include_directories(test_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
  ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_core PRIVATE traash_core)
add_test(NAME test_core COMMAND test_core)

add_executable(test_agent unit/test_agent.c)
target_include_directories(test_agent PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
  ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_agent PRIVATE traash_core)
add_test(NAME test_agent COMMAND test_agent)

add_test(NAME headless_smoke COMMAND $<TARGET_FILE:traash> --headless-test)
add_test(NAME headless_demo COMMAND $<TARGET_FILE:traash> --headless-test --demo --auto)

add_test(NAME agent_integration COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_agent.sh)
set_tests_properties(agent_integration PROPERTIES
  ENVIRONMENT "TRAASH_BIN=$<TARGET_FILE:traash>")

find_program(LUA_EXECUTABLE NAMES lua lua5.4 lua-5.4)
if(LUA_EXECUTABLE)
  add_test(NAME lua_themes COMMAND ${LUA_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/lua/test_themes.lua)
  add_test(NAME lua_status COMMAND ${LUA_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/lua/test_status.lua)
  add_test(NAME lua_plugins COMMAND ${LUA_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/lua/test_plugins.lua)
  add_test(NAME lua_demo COMMAND ${LUA_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/lua/test_demo.lua)
  set_tests_properties(lua_themes lua_status lua_plugins lua_demo PROPERTIES
    ENVIRONMENT "TRAASH_LUA_PATH=${CMAKE_SOURCE_DIR}/lua")
endif()

add_test(NAME vt_fixture COMMAND test_core) # covered in unit; placeholder name stability

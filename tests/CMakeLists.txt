include(FetchContent)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(giga_drill_tests
  test_matcher_engine.cpp
  test_rules_parser.cpp
  test_transforms.cpp
  test_adl_ctad.cpp
  test_coverage_diag.cpp
  test_dead_code.cpp
  test_control_flow.cpp
  test_mcp.cpp
  test_deep_chains.cpp
)

target_link_libraries(giga_drill_tests PRIVATE
  giga_drill_lib
  Catch2::Catch2WithMain
)

target_compile_definitions(giga_drill_tests PRIVATE
  PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(giga_drill_tests)

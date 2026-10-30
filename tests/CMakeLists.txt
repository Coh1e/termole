add_executable(termole_test_console_helper console_helper.cpp)
target_link_libraries(termole_test_console_helper PRIVATE termole_build_options)
set_target_properties(termole_test_console_helper PROPERTIES
  OUTPUT_NAME "termole-test-helper"
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

add_executable(termole_test_host_api host_api_test.cpp)
target_link_libraries(termole_test_host_api PRIVATE termole termole_build_options)
add_dependencies(termole_test_host_api termole_test_console_helper termole_smoke_app termole_smoke_host)
set_target_properties(termole_test_host_api PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

add_test(NAME t01_host_api COMMAND termole_test_host_api)
set_tests_properties(t01_host_api PROPERTIES
  TIMEOUT 120
  WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

add_test(NAME t01_standalone_smoke
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:termole_smoke_app>"
    "-DRESULT=${CMAKE_CURRENT_BINARY_DIR}/standalone-smoke.txt"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.cmake"
)
set_tests_properties(t01_standalone_smoke PROPERTIES TIMEOUT 60)

add_test(NAME t01_host_smoke
  COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:termole_smoke_host>"
    "-DRESULT=${CMAKE_CURRENT_BINARY_DIR}/host-smoke.txt"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.cmake"
)
set_tests_properties(t01_host_smoke PROPERTIES TIMEOUT 60)

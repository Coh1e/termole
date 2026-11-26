# Invoked only by CPackSourceConfig.cmake, with a fresh staging directory.
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH repo)
if(NOT EXISTS "${repo}/.git")
  message(FATAL_ERROR "Creating a source package requires a Termole Git checkout.")
endif()
find_program(git NAMES git REQUIRED)
set(archive "${CMAKE_INSTALL_PREFIX}.tar")
execute_process(
  COMMAND "${git}" -C "${repo}" rev-parse --verify HEAD
  OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "Exporting Termole sources from ${revision}")
execute_process(
  COMMAND "${git}" -C "${repo}" archive --format=tar "--output=${archive}" "${revision}"
  COMMAND_ERROR_IS_FATAL ANY)
file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${CMAKE_INSTALL_PREFIX}")
file(REMOVE "${archive}")

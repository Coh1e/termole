if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Keep existing standalone Linux build directories usable.
  cmake_minimum_required(VERSION 3.20)
  project(termole_linux_core LANGUAGES C CXX)
  include(CTest)
endif()

include(../cmake/TermolePosix.cmake)

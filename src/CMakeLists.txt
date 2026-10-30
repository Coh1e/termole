add_library(termole SHARED
  api/api.cpp
  common/win.cpp
  core/terminal.cpp
  core/utf8.cpp
  session/session.cpp
  view/renderer.cpp
  view/view.cpp
)

target_include_directories(termole
  PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:include>"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(termole PRIVATE TERMOLE_BUILDING_DLL)
target_link_libraries(termole
  PRIVATE
    termole_build_options
    d2d1
    dwrite
    gdi32
    imm32
    user32
)
set_target_properties(termole PROPERTIES
  OUTPUT_NAME "termole"
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

add_library(termole STATIC
  api/api.cpp
  common/win.cpp
  core/utf8.cpp
  gui/gdi_render.c
  gui/cview.c
  script/script.c
  session/session.cpp
  vt/termole_vt.c
  view/renderer.cpp
  view/view.cpp
)

target_include_directories(termole
  PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:include>"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${TERMOLE_GHOSTTY_VT_ROOT}/include"
)
target_compile_definitions(termole PUBLIC TERMOLE_STATIC)
target_link_libraries(termole
  PRIVATE
    termole_build_options
    "$<BUILD_INTERFACE:ghostty_vt>"
    "$<INSTALL_INTERFACE:Termole::ghostty_vt>"
    d2d1
    dwrite
    gdi32
    imm32
    user32
)
set_target_properties(termole PROPERTIES
  OUTPUT_NAME "termole"
  PREFIX ""
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

# Shared compile settings for Termole-owned code. Product code links no
# third-party libraries; only Windows SDK system components and the static CRT.
add_library(termole_build_options INTERFACE)
target_compile_features(termole_build_options INTERFACE cxx_std_23)
target_compile_definitions(termole_build_options INTERFACE
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0A00
)
if(MSVC)
  target_compile_options(termole_build_options INTERFACE
    /W4
    /permissive-
    /utf-8
    /Zc:__cplusplus
    /Zc:inline
    /EHsc
    /GR-
    /Gy
    /Gw
    /Oi
  )
  target_link_options(termole_build_options INTERFACE
    "$<$<CONFIG:Release>:/OPT:REF>"
    "$<$<CONFIG:Release>:/OPT:ICF>"
    "$<$<CONFIG:Release>:/INCREMENTAL:NO>"
  )
else()
  target_compile_options(termole_build_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

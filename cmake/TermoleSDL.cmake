include(FetchContent)

FetchContent_Declare(sdl3
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
  URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
)
FetchContent_Declare(freetype
  URL https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz
  URL_HASH SHA256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
)

set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_TEST_LIBRARY OFF)
set(SDL_TESTS OFF)
set(SDL_DISABLE_INSTALL ON)
set(SDL_AUDIO OFF)
set(SDL_CAMERA OFF)
set(SDL_HAPTIC OFF)
set(SDL_JOYSTICK OFF)
set(SDL_SENSOR OFF)
set(SDL_HIDAPI OFF)
set(SDL_GPU OFF)
set(SDL_VULKAN OFF)
set(SDL_OPENGL OFF)
set(SDL_OPENGLES OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(SDL_X11 ON)
  set(SDL_X11_SHARED ON)
  set(SDL_WAYLAND OFF)
endif()
set(BUILD_SHARED_LIBS OFF)
set(FT_DISABLE_ZLIB ON)
set(FT_DISABLE_BZIP2 ON)
set(FT_DISABLE_PNG ON)
set(FT_DISABLE_HARFBUZZ ON)
set(FT_DISABLE_BROTLI ON)
set(SKIP_INSTALL_ALL ON)
FetchContent_MakeAvailable(sdl3 freetype)

add_executable(termole_sdl "${termole_root}/apps/sdl/main.cpp")
set_target_properties(termole_sdl PROPERTIES OUTPUT_NAME termole)
target_link_libraries(termole_sdl PRIVATE termole_posix_ipc SDL3::SDL3-static freetype)
if(BUILD_TESTING)
  if(APPLE)
    enable_language(OBJC)
    add_executable(macos_window_driver "${termole_root}/tests/visual/macos_window_driver.m")
    target_compile_options(macos_window_driver PRIVATE -fobjc-arc)
    target_link_libraries(macos_window_driver PRIVATE
      "-framework Cocoa" "-framework CoreGraphics" "-framework Vision" "-framework ApplicationServices")
  endif()
  add_test(NAME sdl_render_${termole_platform}
    COMMAND termole_sdl --test-render "${CMAKE_CURRENT_BINARY_DIR}/termole-render.bmp")
  set_tests_properties(sdl_render_${termole_platform} PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen" TIMEOUT 15 LABELS visual)
endif()

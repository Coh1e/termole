# Record the source present at installation, including CPack's staged install.
get_filename_component(source "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(revision "unknown")
find_program(git NAMES git)
if(git AND EXISTS "${source}/.git")
  execute_process(COMMAND "${git}" -C "${source}" rev-parse --verify HEAD
    OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE read_head)
  execute_process(COMMAND "${git}" -C "${source}" status --porcelain --untracked-files=normal
    OUTPUT_VARIABLE changes OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE read_tree)
  if(read_head STREQUAL "0" AND read_tree STREQUAL "0")
    set(revision "${head}")
    if(NOT changes STREQUAL "")
      string(APPEND revision " (working tree modified)")
    endif()
  endif()
endif()
file(MAKE_DIRECTORY "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/termole")
file(WRITE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/termole/source-revision.txt" "${revision}\n")

if(NOT DEFINED APP OR NOT DEFINED HELPER OR NOT DEFINED ROOT)
  message(FATAL_ERROR "run_cli_config.cmake requires -DAPP=... -DHELPER=... -DROOT=...")
endif()
file(MAKE_DIRECTORY "${ROOT}")
set(report "${ROOT}/report.txt")
set(config "${ROOT}/config.toml")
set(bad "${ROOT}/bad.toml")
file(TO_NATIVE_PATH "${HELPER}" helper_native)
file(TO_NATIVE_PATH "${report}" report_native)
file(WRITE "${config}"
"font_size = 20\ntheme = \"light\"\ncolumns = 30\nrows = 8\n\n[profiles.work]\ncolumns = 44\nrows = 9\nprogram = '${helper_native}'\nargs = ['--exit-code', '0']\nenv = ['TERMOLE_PROBE_REPORT=${report_native}']\n")
file(WRITE "${bad}" "columns =\n")

function(run_expect expected result_var stderr_var)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output
                  ERROR_VARIABLE error TIMEOUT 20)
  if(NOT result EQUAL expected)
    message(FATAL_ERROR "command returned ${result}, expected ${expected}. stdout:\n${output}\nstderr:\n${error}")
  endif()
  set(${result_var} "${result}" PARENT_SCOPE)
  set(${stderr_var} "${error}" PARENT_SCOPE)
endfunction()

file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${config}" --profile work)
if(NOT EXISTS "${report}")
  message(FATAL_ERROR "profile did not launch the configured program")
endif()
file(READ "${report}" text)
string(FIND "${text}" "console.cols=44\n" profile_cols)
string(FIND "${text}" "console.rows=9\n" profile_rows)
if(profile_cols EQUAL -1 OR profile_rows EQUAL -1)
  message(FATAL_ERROR "profile grid missing:\n${text}")
endif()

file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${config}" --profile work --columns 50 --rows 10 --
  "${HELPER}" --exit-code 0 --env "TERMOLE_PROBE_REPORT=${report}")
file(READ "${report}" text)
string(FIND "${text}" "console.cols=50\n" override_cols)
string(FIND "${text}" "console.rows=10\n" override_rows)
if(override_cols EQUAL -1 OR override_rows EQUAL -1)
  message(FATAL_ERROR "CLI override did not win:\n${text}")
endif()

file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${bad}" --env "TERMOLE_PROBE_REPORT=${report}" --
  "${HELPER}" --exit-code 0)
if(NOT error MATCHES "bad.toml:1")
  message(FATAL_ERROR "configuration error did not name file and line:\n${error}")
endif()
if(NOT EXISTS "${report}")
  message(FATAL_ERROR "configuration failure did not fall back to defaults")
endif()

file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --no-config --config "${bad}" --env "TERMOLE_PROBE_REPORT=${report}" --
  "${HELPER}" --exit-code 0)
if(error MATCHES "bad.toml")
  message(FATAL_ERROR "--no-config still reported the config error:\n${error}")
endif()

# T25: pipe actions are configured with [pipe.<action>] tables. The table is
# accepted; command delivery is covered by the t25 pipe tests.
set(pipe_config "${ROOT}/pipe.toml")
file(WRITE "${pipe_config}"
"columns = 80\nrows = 10\n\n[pipe.visible]\nkey = 'ctrl+shift+j'\ncommand = 'echo'\n")
file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${pipe_config}" --env "TERMOLE_PROBE_REPORT=${report}" --
  "${HELPER}" --exit-code 0)
if(error MATCHES "configuration error")
  message(FATAL_ERROR "valid pipe configuration was rejected:\n${error}")
endif()
if(NOT EXISTS "${report}")
  message(FATAL_ERROR "pipe configuration prevented the program from launching")
endif()

# A key binding without ctrl or alt would steal ordinary typing.
set(bad_key "${ROOT}/bad-pipe-key.toml")
file(WRITE "${bad_key}" "[pipe.visible]\nkey = 'v'\ncommand = 'echo'\n")
file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${bad_key}" --env "TERMOLE_PROBE_REPORT=${report}" --
  "${HELPER}" --exit-code 0)
if(NOT error MATCHES "bad-pipe-key.toml:2")
  message(FATAL_ERROR "invalid pipe key did not name file and line:\n${error}")
endif()
if(NOT error MATCHES "invalid key binding")
  message(FATAL_ERROR "invalid pipe key was not diagnosed:\n${error}")
endif()

# A pipe chord must not shadow the built-in copy/paste shortcuts.
set(builtin_key "${ROOT}/builtin-pipe-key.toml")
file(WRITE "${builtin_key}" "[pipe.visible]\nkey = 'ctrl+shift+v'\ncommand = 'echo'\n")
file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${builtin_key}" --env "TERMOLE_PROBE_REPORT=${report}" --
  "${HELPER}" --exit-code 0)
if(NOT error MATCHES "builtin-pipe-key.toml:2" OR NOT error MATCHES "built-in")
  message(FATAL_ERROR "built-in shortcut conflict was not diagnosed with file and line:\n${error}")
endif()

# Two actions cannot share one chord.
set(duplicate_key "${ROOT}/duplicate-pipe-key.toml")
file(WRITE "${duplicate_key}"
"[pipe.visible]\nkey = 'ctrl+shift+j'\ncommand = 'echo'\n\n[pipe.selected]\nkey = 'ctrl+shift+j'\ncommand = 'echo'\n")
file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${duplicate_key}" --env "TERMOLE_PROBE_REPORT=${report}" --
  "${HELPER}" --exit-code 0)
if(NOT error MATCHES "duplicate-pipe-key.toml:6")
  message(FATAL_ERROR "duplicate pipe key did not name file and line:\n${error}")
endif()
if(NOT error MATCHES "already bound")
  message(FATAL_ERROR "duplicate pipe key was not diagnosed:\n${error}")
endif()

# Each table must pair the key with a non-empty command.
set(missing_command "${ROOT}/missing-pipe-command.toml")
file(WRITE "${missing_command}" "[pipe.scrollback]\nkey = 'ctrl+shift+b'\n")
file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${missing_command}" --env "TERMOLE_PROBE_REPORT=${report}" --
  "${HELPER}" --exit-code 0)
if(NOT error MATCHES "missing-pipe-command.toml:1" OR NOT error MATCHES "requires command")
  message(FATAL_ERROR "missing pipe command was not diagnosed with file and line:\n${error}")
endif()

set(empty_command "${ROOT}/empty-pipe-command.toml")
file(WRITE "${empty_command}"
"[pipe.visible]\nkey = 'ctrl+shift+j'\ncommand = '   '\n")
file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${empty_command}" --env "TERMOLE_PROBE_REPORT=${report}" --
  "${HELPER}" --exit-code 0)
if(NOT error MATCHES "requires command")
  message(FATAL_ERROR "empty pipe command was not diagnosed:\n${error}")
endif()

# Only the three documented actions exist.
set(unknown_action "${ROOT}/unknown-pipe-action.toml")
file(WRITE "${unknown_action}"
"[pipe.clipboard]\nkey = 'ctrl+shift+c'\ncommand = 'echo'\n")
file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${unknown_action}" --env "TERMOLE_PROBE_REPORT=${report}" --
  "${HELPER}" --exit-code 0)
if(NOT error MATCHES "unknown-pipe-action.toml:1" OR NOT error MATCHES "unsupported table")
  message(FATAL_ERROR "unknown pipe action was not diagnosed with file and line:\n${error}")
endif()

set(unknown_key "${ROOT}/unknown-pipe-key.toml")
file(WRITE "${unknown_key}"
"[pipe.visible]\nkey = 'ctrl+shift+hyper'\ncommand = 'echo'\n")
file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${unknown_key}" --env "TERMOLE_PROBE_REPORT=${report}" --
  "${HELPER}" --exit-code 0)
if(NOT error MATCHES "unknown-pipe-key.toml:2" OR NOT error MATCHES "invalid key binding")
  message(FATAL_ERROR "unknown key name was not diagnosed with file and line:\n${error}")
endif()

set(profile_pipe "${ROOT}/profile-pipe.toml")
file(WRITE "${profile_pipe}"
"[profiles.work.pipe.visible]\nkey = 'ctrl+shift+v'\ncommand = 'echo'\n")
file(REMOVE "${report}")
run_expect(0 result error "${APP}" --gui --config "${profile_pipe}" --profile work --env "TERMOLE_PROBE_REPORT=${report}" --
  "${HELPER}" --exit-code 0)
if(NOT error MATCHES "profile-pipe.toml:1" OR NOT error MATCHES "pipe bindings are global")
  message(FATAL_ERROR "profile-scoped pipe binding was not diagnosed:\n${error}")
endif()

message(STATUS "TOML defaults/profile/CLI precedence and error fallback passed")

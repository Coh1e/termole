# Enforces the release executable size budget.
if(NOT DEFINED EXE OR NOT EXISTS "${EXE}")
  message(FATAL_ERROR "CheckSize: EXE is missing: ${EXE}")
endif()
if(NOT DEFINED MAX_BYTES)
  message(FATAL_ERROR "CheckSize: MAX_BYTES is required")
endif()

file(SIZE "${EXE}" actual)
if(actual GREATER MAX_BYTES)
  math(EXPR budget_mb "${MAX_BYTES} / 1048576")
  math(EXPR actual_mb "${actual} / 1048576")
  message(FATAL_ERROR
    "CheckSize: ${EXE} is ${actual} bytes (${actual_mb} MiB), budget ${MAX_BYTES} (${budget_mb} MiB)")
endif()

math(EXPR actual_mb "${actual} / 1048576")
message(STATUS "CheckSize: ${EXE} is ${actual} bytes (${actual_mb} MiB), within budget")

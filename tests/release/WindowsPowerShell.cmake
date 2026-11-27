# Windows PowerShell 5.1 discovers its own modules when the caller's path is absent.
set(zip_powershell "${CMAKE_COMMAND}" -E env --unset=PSModulePath --
  "${PowerShell}" -NoProfile -ExecutionPolicy Bypass)

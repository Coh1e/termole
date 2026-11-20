#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

int termole_cgui_run(HINSTANCE instance, PWSTR command_line, int show);

#ifdef __cplusplus
}
#endif

#pragma once
#include <termole/termole.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct termole_cgui_options {
    termole_session_options session;
    int width;
    int height;
    const wchar_t* title;
    const wchar_t* font;
    uint32_t font_size;
    uint32_t theme;
    bool smoke;
} termole_cgui_options;

int termole_cgui_run(HINSTANCE instance, const termole_cgui_options* options, int show);

#ifdef __cplusplus
}
#endif

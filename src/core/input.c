#include "input.h"

#include <string.h>

const char* termole_key_sequence(termole_key key, int application_cursor)
{
    switch (key) {
    case TERMOLE_KEY_ENTER:       return "\r";
    case TERMOLE_KEY_TAB:         return "\t";
    case TERMOLE_KEY_BACKSPACE:   return "\x7f";
    case TERMOLE_KEY_ESCAPE:      return "\x1b";
    case TERMOLE_KEY_ARROW_UP:    return application_cursor ? "\x1bOA" : "\x1b[A";
    case TERMOLE_KEY_ARROW_DOWN:  return application_cursor ? "\x1bOB" : "\x1b[B";
    case TERMOLE_KEY_ARROW_RIGHT: return application_cursor ? "\x1bOC" : "\x1b[C";
    case TERMOLE_KEY_ARROW_LEFT:  return application_cursor ? "\x1bOD" : "\x1b[D";
    case TERMOLE_KEY_HOME:        return application_cursor ? "\x1bOH" : "\x1b[H";
    case TERMOLE_KEY_END:         return application_cursor ? "\x1bOF" : "\x1b[F";
    case TERMOLE_KEY_DELETE:      return "\x1b[3~";
    case TERMOLE_KEY_INSERT:      return "\x1b[2~";
    case TERMOLE_KEY_PAGE_UP:     return "\x1b[5~";
    case TERMOLE_KEY_PAGE_DOWN:   return "\x1b[6~";
    case TERMOLE_KEY_CTRL_C:      return "\x03";
    case TERMOLE_KEY_CTRL_D:      return "\x04";
    }
    return NULL;
}

void termole_bracket_paste(char* bytes, size_t length)
{
    static const char begin[] = "\x1b[200~", end[] = "\x1b[201~";
    _Static_assert(sizeof(begin) + sizeof(end) - 2 == TERMOLE_PASTE_OVERHEAD,
                   "paste delimiters must fit the advertised buffer");
    memmove(bytes + sizeof(begin) - 1, bytes, length);
    memcpy(bytes, begin, sizeof(begin) - 1);
    memcpy(bytes + sizeof(begin) - 1 + length, end, sizeof(end) - 1);
}

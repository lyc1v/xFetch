// src/terminalfont.c
#include "common.h"
#include "terminalfont.h"
#include <stdio.h>
#include <stdlib.h>

void terminal_font_string(char* out, size_t n){
    // Best effort: baca env kustom kalau user set, else N/A
    const char* f = getenv("TERMINAL_FONT");
    if(f && *f){ snprintf(out,n,"%s", f); return; }
    snprintf(out,n,"N/A");
}

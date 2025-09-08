// src/terminalshell.c
#include "common.h"
#include "terminalshell.h"
#include <stdio.h>
#include <stdlib.h>

void shell_string(char* out, size_t n){
    const char* s = getenv("SHELL");
    if(s && *s){ snprintf(out,n,"%s", s); return; }
    char buf[128]={0};
    if(uf_read_first_line("/proc/self/comm", buf, sizeof(buf))){
        snprintf(out,n,"%s", buf); return;
    }
    snprintf(out,n,"?");
}

void terminal_string(char* out, size_t n){
    const char* term = getenv("TERM");
    const char* col = getenv("COLORTERM");
    if(term && col) snprintf(out,n,"%s (%s)", term, col);
    else if(term)   snprintf(out,n,"%s", term);
    else            snprintf(out,n,"?");
}

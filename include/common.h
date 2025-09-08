#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#include <stddef.h>

#define C0 "\x1b[0m"
#define C1 "\x1b[36m"  // cyan
#define C2 "\x1b[35m"  // magenta
#define C3 "\x1b[34m"  // blue
#define C4 "\x1b[32m"  // green
#define C5 "\x1b[33m"  // yellow
#define C6 "\x1b[31m"  // red
#define CX "\x1b[1m"   // bold

extern int UF_USE_ICONS;     // set via arg
extern int UF_IS_ANDROID;    // set by detect_android()

void uf_detect_android(void);

void uf_trim(char* s);
char* uf_read_first_line(const char* path, char* buf, size_t n);
char* uf_exec_read(const char* cmd, char* buf, size_t n);
void uf_human_bytes(unsigned long long bytes, char out[32]);

#endif

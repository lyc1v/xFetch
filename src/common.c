#include "common.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

int UF_USE_ICONS = 1;
int UF_IS_ANDROID = 0;

void uf_trim(char* s){
    if(!s) return;
    size_t n = strlen(s);
    while(n && isspace((unsigned char)s[n-1])) s[--n] = 0;
    size_t i=0; while(s[i] && isspace((unsigned char)s[i])) i++;
    if(i) memmove(s, s+i, strlen(s+i)+1);
}

char* uf_read_first_line(const char* path, char* buf, size_t n){
    FILE* f = fopen(path, "r");
    if(!f) return NULL;
    if(!fgets(buf, (int)n, f)){ fclose(f); return NULL; }
    fclose(f);
    uf_trim(buf);
    return buf;
}

char* uf_exec_read(const char* cmd, char* buf, size_t n){
    FILE* p = popen(cmd, "r");
    if(!p) return NULL;
    size_t m = fread(buf, 1, n-1, p);
    pclose(p);
    if(m == 0){ buf[0]=0; return NULL; }
    buf[m] = 0;
    // cut at newline
    for(size_t i=0;i<m;i++){ if(buf[i]=='\n'){ buf[i]=0; break; } }
    uf_trim(buf);
    return buf;
}

void uf_human_bytes(unsigned long long bytes, char out[32]){
    static const char* sfx[] = {"B","KB","MB","GB","TB","PB"};
    int i=0; double v = (double)bytes;
    while(v >= 1024.0 && i < 5){ v/=1024.0; i++; }
    snprintf(out, 32, "%.1f %s", v, sfx[i]);
}

void uf_detect_android(void){
    char buf[256];
    buf[0]=0;
    if(uf_exec_read("getprop ro.product.manufacturer 2>/dev/null", buf, sizeof(buf)))
        if(buf[0]) UF_IS_ANDROID = 1;
}

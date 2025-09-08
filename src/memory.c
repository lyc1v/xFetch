// src/memory.c
#include "common.h"
#include "memory.h"
#include <sys/sysinfo.h>
#include <stdio.h>

void memory_summary(char* out, size_t n){
    struct sysinfo si;
    if(sysinfo(&si)==0){
        unsigned long long total = (unsigned long long)si.totalram * si.mem_unit;
        unsigned long long freeb = (unsigned long long)si.freeram * si.mem_unit;
        unsigned long long buff = (unsigned long long)si.bufferram * si.mem_unit;
        unsigned long long avail = freeb + buff;
        unsigned long long used = total - avail;
        int pct = total ? (int)((used*100)/total) : 0;
        char t[32], a[32];
        uf_human_bytes(total, t);
        uf_human_bytes(avail, a);
        snprintf(out,n,"Total %s, Avail %s (%d%% used)", t, a, pct);
    }else{
        snprintf(out,n,"N/A");
    }
}

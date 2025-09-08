// src/swap.c
#include "common.h"
#include "swap.h"
#include <sys/sysinfo.h>
#include <stdio.h>

void swap_string(char* out, size_t n){
    struct sysinfo si;
    if(sysinfo(&si)==0){
        unsigned long long total = (unsigned long long)si.totalswap * si.mem_unit;
        unsigned long long freeb = (unsigned long long)si.freeswap * si.mem_unit;
        unsigned long long used = total - freeb;
        char a[32], b[32];
        uf_human_bytes(used, a); uf_human_bytes(total, b);
        snprintf(out,n,"%s / %s", a, b);
    }else{
        snprintf(out,n,"N/A");
    }
}

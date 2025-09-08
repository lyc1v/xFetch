// src/uptime.c
#include "common.h"
#include "uptime.h"
#include <sys/sysinfo.h>
#include <stdio.h>

void uptime_string(char* out, size_t n){
    struct sysinfo si;
    if(sysinfo(&si)==0){
        long long up = si.uptime;
        long long d = up/86400;
        long long h = (up%86400)/3600;
        long long m = (up%3600)/60;
        snprintf(out,n,"%lldd %02lld:%02lld", d, h, m);
    }else{
        snprintf(out,n,"0d 00:00");
    }
}

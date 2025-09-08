// src/os.c
#include "common.h"
#include "os.h"
#include <stdio.h>
#include <sys/utsname.h>

void os_string(char* out, size_t n){
    if(UF_IS_ANDROID){
        char ver[128]={0}, brand[128]={0}, model[128]={0};
        uf_exec_read("getprop ro.build.version.release 2>/dev/null", ver, sizeof(ver));
        uf_exec_read("getprop ro.product.manufacturer 2>/dev/null", brand, sizeof(brand));
        uf_exec_read("getprop ro.product.model 2>/dev/null", model, sizeof(model));
        snprintf(out, n, "Android %s (%s %s)", ver[0]?ver:"?", brand[0]?brand:"?", model[0]?model:"?");
        return;
    }
    FILE* f = fopen("/etc/os-release","r");
    if(f){
        char line[256], pretty[256]="";
        while(fgets(line, sizeof(line), f)){
            if(strncmp(line, "PRETTY_NAME=",12)==0){
                char* v = line+12;
                if(*v=='"'||*v=='\''){ v++; char *q=strrchr(v, line[12]); if(q) *q=0; }
                uf_trim(v);
                snprintf(pretty, sizeof(pretty), "%s", v);
                break;
            }
        }
        fclose(f);
        if(pretty[0]){ snprintf(out,n,"%s",pretty); return; }
    }
    struct utsname u; uname(&u);
    snprintf(out,n,"%s", u.sysname);
}

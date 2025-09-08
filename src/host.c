// src/host.c
#include "common.h"
#include "host.h"
#include <stdio.h>
#include <unistd.h>

void host_string(char* out, size_t n){
    if(UF_IS_ANDROID){
        char brand[128]={0}, model[128]={0};
        uf_exec_read("getprop ro.product.brand 2>/dev/null", brand, sizeof(brand));
        uf_exec_read("getprop ro.product.model 2>/dev/null", model, sizeof(model));
        snprintf(out,n,"%s %s", brand[0]?brand:"Android", model[0]?model:"Device");
        return;
    }
    char prod[256]={0}, vers[256]={0};
    if(uf_read_first_line("/sys/devices/virtual/dmi/id/product_name", prod, sizeof(prod))){
        if(uf_read_first_line("/sys/devices/virtual/dmi/id/product_version", vers, sizeof(vers)) && vers[0])
            snprintf(out,n,"%s %s", prod, vers);
        else
            snprintf(out,n,"%s", prod);
        return;
    }
    gethostname(out, n);
}

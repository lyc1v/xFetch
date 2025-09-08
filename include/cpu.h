#ifndef CPU_H
#define CPU_H

#include <stddef.h>

void cpu_string(char* out, size_t n);
void cpu_info_detailed(char* out, size_t n);
void cpu_performance_info(char* out, size_t n);
void cpu_soc_info(char* out, size_t n);

#endif

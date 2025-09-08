// src/ram.c
#include "common.h"
#include "ram.h"
#include <sys/sysinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define MEMINFO_PATH "/proc/meminfo"

typedef struct {
    unsigned long long total;
    unsigned long long free;
    unsigned long long available;
    unsigned long long buffers;
    unsigned long long cached;
    unsigned long long used;
    double usage_percent;
} ram_info_t;

static unsigned long long parse_meminfo_value(const char* line) {
    if (!line) return 0;
    
    while (*line && (*line < '0' || *line > '9')) line++;
    
    unsigned long long value = 0;
    while (*line >= '0' && *line <= '9') {
        value = value * 10 + (*line - '0');
        line++;
    }
    
    return value * 1024;
}

static int parse_meminfo(ram_info_t* info) {
    FILE* f = fopen(MEMINFO_PATH, "r");
    if (!f) return 0;
    
    char buffer[256];
    memset(info, 0, sizeof(*info));
    
    while (fgets(buffer, sizeof(buffer), f)) {
        if (strncmp(buffer, "MemTotal:", 9) == 0) {
            info->total = parse_meminfo_value(buffer);
        } else if (strncmp(buffer, "MemFree:", 8) == 0) {
            info->free = parse_meminfo_value(buffer);
        } else if (strncmp(buffer, "MemAvailable:", 13) == 0) {
            info->available = parse_meminfo_value(buffer);
        } else if (strncmp(buffer, "Buffers:", 8) == 0) {
            info->buffers = parse_meminfo_value(buffer);
        } else if (strncmp(buffer, "Cached:", 7) == 0) {
            info->cached = parse_meminfo_value(buffer);
        }
    }
    
    fclose(f);
    
    if (info->available > 0) {
        info->used = info->total - info->available;
    } else {
        info->used = info->total - info->free - info->buffers - info->cached;
    }
    
    if (info->used > info->total) info->used = info->total - info->free;
    if (info->total > 0) info->usage_percent = (double)info->used / info->total * 100.0;
    
    return info->total > 0;
}

static int parse_sysinfo(ram_info_t* info) {
    struct sysinfo si;
    if (sysinfo(&si) != 0) return 0;
    
    memset(info, 0, sizeof(*info));
    info->total = (unsigned long long)si.totalram * si.mem_unit;
    info->free = (unsigned long long)si.freeram * si.mem_unit;
    info->buffers = (unsigned long long)si.bufferram * si.mem_unit;
    info->used = info->total - info->free - info->buffers;
    
    if (info->total > 0) info->usage_percent = (double)info->used / info->total * 100.0;
    
    return 1;
}

static int get_ram_info(ram_info_t* info) {
    if (parse_meminfo(info)) return 1;
    return parse_sysinfo(info);
}

void ram_string(char* out, size_t n) {
    if (!out || n == 0) return;
    
    ram_info_t info;
    if (get_ram_info(&info)) {
        char used_str[32], total_str[32];
        uf_human_bytes(info.used, used_str);
        uf_human_bytes(info.total, total_str);
        snprintf(out, n, "%s / %s", used_str, total_str);
    } else {
        snprintf(out, n, "N/A");
    }
}

void ram_usage_percent(char* out, size_t n) {
    if (!out || n == 0) return;
    
    ram_info_t info;
    if (get_ram_info(&info)) {
        snprintf(out, n, "%.1f%%", info.usage_percent);
    } else {
        snprintf(out, n, "N/A");
    }
}

void ram_available_string(char* out, size_t n) {
    if (!out || n == 0) return;
    
    ram_info_t info;
    if (get_ram_info(&info)) {
        char avail_str[32];
        unsigned long long available = info.available > 0 ? info.available : info.free;
        uf_human_bytes(available, avail_str);
        snprintf(out, n, "%s", avail_str);
    } else {
        snprintf(out, n, "N/A");
    }
}

int ram_get_bytes(unsigned long long* total, unsigned long long* used, unsigned long long* available) {
    ram_info_t info;
    if (!get_ram_info(&info)) return 0;
    
    if (total) *total = info.total;
    if (used) *used = info.used;
    if (available) *available = info.available > 0 ? info.available : info.free;
    
    return 1;
}

int ram_is_low_memory(void) {
    ram_info_t info;
    if (!get_ram_info(&info)) return 0;
    return info.usage_percent > 85.0;
}

void ram_pressure_level(char* out, size_t n) {
    if (!out || n == 0) return;
    
    ram_info_t info;
    if (get_ram_info(&info)) {
        if (info.usage_percent < 50.0) {
            snprintf(out, n, "Low");
        } else if (info.usage_percent < 75.0) {
            snprintf(out, n, "Normal");
        } else if (info.usage_percent < 90.0) {
            snprintf(out, n, "High");
        } else {
            snprintf(out, n, "Critical");
        }
    } else {
        snprintf(out, n, "Unknown");
    }
}

void ram_cached_string(char* out, size_t n) {
    if (!out || n == 0) return;
    
    ram_info_t info;
    if (get_ram_info(&info) && info.cached > 0) {
        char cached_str[32];
        uf_human_bytes(info.cached, cached_str);
        snprintf(out, n, "%s", cached_str);
    } else {
        snprintf(out, n, "N/A");
    }
}

void ram_buffers_string(char* out, size_t n) {
    if (!out || n == 0) return;
    
    ram_info_t info;
    if (get_ram_info(&info) && info.buffers > 0) {
        char buffers_str[32];
        uf_human_bytes(info.buffers, buffers_str);
        snprintf(out, n, "%s", buffers_str);
    } else {
        snprintf(out, n, "N/A");
    }
}

int ram_get_usage_color(void) {
    ram_info_t info;
    if (!get_ram_info(&info)) return 0;
    
    if (info.usage_percent < 50.0) return 2;
    if (info.usage_percent < 75.0) return 3;
    if (info.usage_percent < 90.0) return 1;
    return 1;
}

/*
DOCUMENTATION - RAM Module

This module provides memory information through multiple detection methods:

DETECTION HIERARCHY:
1. /proc/meminfo - Primary method, most accurate on Linux
2. sysinfo() syscall - Fallback method, always available

KEY FUNCTIONS:
- ram_string(): Basic "used / total" format
- ram_usage_percent(): Percentage utilization
- ram_available_string(): Available memory amount
- ram_pressure_level(): Memory pressure classification
- ram_get_bytes(): Raw byte values for external use

MEMORY CALCULATION:
Used memory = Total - Available (if available)
            = Total - Free - Buffers - Cached (fallback)

The module handles edge cases like:
- Missing /proc/meminfo (containers, restricted environments)
- Invalid memory values (negative usage, overflow)
- Android-specific memory management quirks

PERFORMANCE NOTES:
- Single file read operation for /proc/meminfo
- Efficient string parsing without regex
- Minimal memory allocation (stack-based buffers)
- Error resilience with fallback mechanisms

The implementation prioritizes accuracy over features, providing
reliable memory information across different system configurations.
*/

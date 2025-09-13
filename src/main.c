// src/main.c — ultrafetch (C modular)
// Build: make
// Run  : ./ultrafetch

#include <stdio.h>       
#include <string.h>      
#include <sys/utsname.h> 
#include <stdlib.h>
#include <unistd.h>

#include "common.h"
#include "os.h"
#include "cpu.h"              
#include "gpu.h"
#include "ram.h"
#include "memory.h"
#include "swap.h"
#include "host.h"
#include "terminalshell.h"
#include "terminalfont.h"
#include "uptime.h"

#define UF_VERSION "2.1.0"
#define LABEL_WIDTH 16

typedef struct {
    int show_help;
    int show_version;
    int show_less;
    int show_icons;
    int color_mode;
    int minimal;
} uf_options_t;


static void print_logo(void) {
    FILE *f = fopen("logos/xFetch.txt", "r");
    if (!f) return;

    char line[512];
    const char *colors[] = {
        "\033[1;31m", // merah
        "\033[1;33m", // kuning
        "\033[1;32m", // hijau
        "\033[1;36m", // cyan
        "\033[1;34m", // biru
        "\033[1;35m"  // ungu
    };
    int ncolors = sizeof(colors) / sizeof(colors[0]);
    int i = 0;

    while (fgets(line, sizeof(line), f)) {
        printf("%s%s\033[0m", colors[i % ncolors], line);
        i++;
    }

    fclose(f);
}

static const char* get_icon(const char* type) {
    if (strcmp(type, "os") == 0) return "🖥️  ";
    if (strcmp(type, "host") == 0) return "💻 ";
    if (strcmp(type, "kernel") == 0) return "⚙️  ";
    if (strcmp(type, "arch") == 0) return "🏗️  ";
    if (strcmp(type, "shell") == 0) return "🐚 ";
    if (strcmp(type, "terminal") == 0) return "📟 ";
    if (strcmp(type, "font") == 0) return "🔤 ";
    if (strcmp(type, "uptime") == 0) return "⏰ ";
    if (strcmp(type, "cpu") == 0) return "🔥 ";
    if (strcmp(type, "gpu") == 0) return "🎮 ";
    if (strcmp(type, "ram") == 0) return "💾 ";
    if (strcmp(type, "memory") == 0) return "🗂️  ";
    if (strcmp(type, "swap") == 0) return "💿 ";
    return "";
}

static const char* get_color(int color_mode, const char* type) {
    if (color_mode == 0) return "";
    
    if (color_mode == 1) {
        if (strcmp(type, "label") == 0) return "\033[1;36m";
        if (strcmp(type, "value") == 0) return "\033[0;37m";
        if (strcmp(type, "reset") == 0) return "\033[0m";
    } else if (color_mode == 2) {
        if (strcmp(type, "label") == 0) return "\033[1;32m";
        if (strcmp(type, "value") == 0) return "\033[1;33m";
        if (strcmp(type, "reset") == 0) return "\033[0m";
    } else if (color_mode == 3) {
        if (strcmp(type, "label") == 0) return "\033[1;35m";
        if (strcmp(type, "value") == 0) return "\033[1;31m";
        if (strcmp(type, "reset") == 0) return "\033[0m";
    }
    return "";
}

static void kv(const char* label, const char* value, uf_options_t* opts, const char* icon_type){
    if (!label || !value) return;
    
    const char* icon = opts->show_icons ? get_icon(icon_type) : "";
    const char* label_color = get_color(opts->color_mode, "label");
    const char* value_color = get_color(opts->color_mode, "value");
    const char* reset_color = get_color(opts->color_mode, "reset");
    
    printf("%s%s%-*s%s%s: %s%s%s\n", 
           icon, label_color, LABEL_WIDTH, label, reset_color,
           value_color, (value && value[0]) ? value : "N/A", reset_color, "");
}

static int parse_args(int argc, char** argv, uf_options_t* opts) {
    if (!opts) return -1;
    
    memset(opts, 0, sizeof(*opts));
    opts->color_mode = 1;
    
    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
            opts->show_help = 1;
        }
        else if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0){
            opts->show_version = 1;
        }
        else if(strcmp(argv[i], "--show-less") == 0){
            opts->show_less = 1;
        }
        else if(strcmp(argv[i], "--icon") == 0){
            opts->show_icons = 1;
        }
        else if(strcmp(argv[i], "--color") == 0){
            if(i + 1 < argc) {
                int color = atoi(argv[++i]);
                if(color >= 0 && color <= 3) opts->color_mode = color;
            }
        }
        else if(strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--minimal") == 0){
            opts->minimal = 1;
            opts->show_less = 1;
        }
        else {
            fprintf(stderr, "ultrafetch: unknown option '%s'\n", argv[i]);
            return -1;
        }
    }
    
    return 0;
}

static void print_usage(const char* argv0){
    printf("ultrafetch %s - System information tool\n\n", UF_VERSION);
    printf("USAGE:\n    %s [OPTIONS]\n\n", argv0);
    printf("OPTIONS:\n");
    printf("    -h, --help       Show this help\n");
    printf("    -v, --version    Show version\n");
    printf("    -m, --minimal    Minimal output\n");
    printf("    --show-less      Reduce output details\n");
    printf("    --icon           Show icons\n");
    printf("    --color <0-3>    Color scheme (0=off, 1=cyan, 2=green, 3=magenta)\n");
    printf("\nEXAMPLES:\n");
    printf("    %s              # Standard output\n", argv0);
    printf("    %s --icon       # With icons\n", argv0);
    printf("    %s --color 2    # Green color scheme\n", argv0);
    printf("    %s -m           # Minimal mode\n", argv0);
}

static void print_version(void){
    printf("ultrafetch %s\n", UF_VERSION);
}

int main(int argc, char** argv){
    uf_options_t opts;
    
    if (parse_args(argc, argv, &opts) != 0) {
        return 1;
    }
    
    if (opts.show_help) {
        print_usage(argv[0]);
        return 0;
    }
    
    if (opts.show_version) {
        print_version();
        return 0;
    }
    
    uf_detect_android();
    
    char s_os[256] = {0};
    char s_host[256] = {0};
    char s_kernel[128] = {0};
    char s_arch[64] = {0};
    char s_shell[256] = {0};
    char s_term[128] = {0};
    char s_font[128] = {0};
    char s_uptime[64] = {0};
    char s_cpu[256] = {0};
    char s_gpu[256] = {0};
    char s_ram[64] = {0};
    char s_memory[128] = {0};
    char s_swap[64] = {0};
    
    os_string(s_os, sizeof(s_os));
    host_string(s_host, sizeof(s_host));
    shell_string(s_shell, sizeof(s_shell));
    terminal_string(s_term, sizeof(s_term));
    uptime_string(s_uptime, sizeof(s_uptime));
    
    cpu_string(s_cpu, sizeof(s_cpu));
    gpu_string(s_gpu, sizeof(s_gpu));
    ram_string(s_ram, sizeof(s_ram));
    swap_string(s_swap, sizeof(s_swap));
    
    if (!opts.show_less) {
        terminal_font_string(s_font, sizeof(s_font));
        memory_summary(s_memory, sizeof(s_memory));
    }
    
    struct utsname u;
    if(uname(&u) == 0){
        snprintf(s_kernel, sizeof(s_kernel), "%s", u.release);
        snprintf(s_arch, sizeof(s_arch), "%s", u.machine);
    }
    
    if (!opts.minimal) print_logo();
    
    kv("OS", s_os, &opts, "os");
    kv("Host", s_host, &opts, "host");
    kv("Kernel", s_kernel, &opts, "kernel");
    kv("Arch", s_arch, &opts, "arch");
    kv("Shell", s_shell, &opts, "shell");
    kv("Terminal", s_term, &opts, "terminal");
    
    if (!opts.show_less) {
        kv("Font", s_font, &opts, "font");
    }
    
    kv("Uptime", s_uptime, &opts, "uptime");
    kv("CPU", s_cpu, &opts, "cpu");
    kv("GPU", s_gpu, &opts, "gpu");
    kv("RAM", s_ram, &opts, "ram");
    
    if (!opts.show_less) {
        kv("Memory", s_memory, &opts, "memory");
    }
    
    kv("Swap", s_swap, &opts, "swap");
    
    const char* footer_color = get_color(opts.color_mode, "label");
    const char* reset_color = get_color(opts.color_mode, "reset");
    printf("\n%sultrafetch (C modular)%s\n", footer_color, reset_color);
    
    return 0;
}

/*
This is the main entry point that orchestrates system information collection.
The program uses a modular approach where each component (CPU, GPU, RAM, etc)
has its own detection module. Command line parsing handles various output
options including color schemes and icon display. The render loop calls
each module's string function to populate display buffers, then formats
output according to user preferences. Error handling ensures graceful
degradation when system information is unavailable.
*/

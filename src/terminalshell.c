/*
 * Terminal and Shell Detection Module
 * 
 * Provides comprehensive detection of shell and terminal emulator information
 * including version detection for major terminal applications and shells.
 * Supports cross-platform detection with fallback mechanisms.
 */

#include "common.h"
#include "terminalshell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>

#ifdef __FreeBSD__
    #include <paths.h>
    #ifndef _PATH_LOCALBASE
        #define _PATH_LOCALBASE "/usr/local"
    #endif
#elif __OpenBSD__
    #define _PATH_LOCALBASE "/usr/local"
#elif __NetBSD__
    #define _PATH_LOCALBASE "/usr/pkg"
#endif

#define BUFFER_SIZE 1024
#define VERSION_SIZE 256

static bool read_file_data(const char* path, char* buffer, size_t size)
{
    FILE* file = fopen(path, "r");
    if (!file) return false;
    
    size_t bytes_read = fread(buffer, 1, size - 1, file);
    fclose(file);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        return true;
    }
    return false;
}

static bool get_command_output(const char* cmd, char* buffer, size_t size)
{
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return false;
    
    if (fgets(buffer, size, pipe)) {
        pclose(pipe);
        char* newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';
        return true;
    }
    pclose(pipe);
    return false;
}

static const char* get_basename_path(const char* path)
{
    const char* last_slash = strrchr(path, '/');
    return last_slash ? last_slash + 1 : path;
}

static bool str_starts_with(const char* str, const char* prefix)
{
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

static bool str_equals_ignore_case(const char* a, const char* b)
{
    while (*a && *b) {
        if (tolower(*a) != tolower(*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

static bool extract_bash_version(const char* line, void* userdata)
{
    if (!str_starts_with(line, "@(#)Bash version ")) return true;
    const char* start = line + strlen("@(#)Bash version ");
    const char* end = strchr(start, '(');
    if (!end) return true;
    
    size_t len = end - start;
    if (len < VERSION_SIZE - 1) {
        strncpy((char*)userdata, start, len);
        ((char*)userdata)[len] = '\0';
        return false;
    }
    return true;
}

static bool extract_zsh_version(const char* line, void* userdata)
{
    if (!str_starts_with(line, "zsh-")) return true;
    const char* start = line + strlen("zsh-");
    const char* end = strchr(start, '-');
    if (!end) return true;
    
    size_t len = end - start;
    if (len < VERSION_SIZE - 1) {
        strncpy((char*)userdata, start, len);
        ((char*)userdata)[len] = '\0';
        return false;
    }
    return true;
}

static bool binary_extract_strings(const char* path, bool (*callback)(const char*, void*), void* userdata)
{
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    
    char buffer[4096];
    size_t bytes_read;
    char current_string[256] = {0};
    int str_len = 0;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            if (isprint(buffer[i])) {
                if (str_len < sizeof(current_string) - 1) {
                    current_string[str_len++] = buffer[i];
                }
            } else {
                if (str_len > 8) {
                    current_string[str_len] = '\0';
                    if (!callback(current_string, userdata)) {
                        fclose(file);
                        return true;
                    }
                }
                str_len = 0;
            }
        }
    }
    
    fclose(file);
    return false;
}

static bool get_shell_version_bash(const char* exe, const char* exe_path, char* version, size_t version_size)
{
    const char* path = (exe_path && *exe_path) ? exe_path : exe;
    
    if (binary_extract_strings(path, extract_bash_version, version)) {
        return true;
    }
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s --version 2>/dev/null", exe);
    char output[BUFFER_SIZE];
    if (get_command_output(cmd, output, sizeof(output))) {
        char* version_start = strstr(output, "version ");
        if (version_start) {
            version_start += strlen("version ");
            char* version_end = strchr(version_start, '(');
            if (version_end) {
                size_t len = version_end - version_start;
                if (len < version_size - 1) {
                    strncpy(version, version_start, len);
                    version[len] = '\0';
                    char* space = strrchr(version, ' ');
                    if (space) *space = '\0';
                    return true;
                }
            }
        }
    }
    return false;
}

static bool get_shell_version_zsh(const char* exe, const char* exe_path, char* version, size_t version_size)
{
    const char* path = (exe_path && *exe_path) ? exe_path : exe;
    
    if (binary_extract_strings(path, extract_zsh_version, version)) {
        return true;
    }
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s --version 2>/dev/null", exe);
    char output[BUFFER_SIZE];
    if (get_command_output(cmd, output, sizeof(output))) {
        char* space = strchr(output, ' ');
        if (space) {
            space++;
            char* next_space = strchr(space, ' ');
            if (next_space) {
                size_t len = next_space - space;
                if (len < version_size - 1) {
                    strncpy(version, space, len);
                    version[len] = '\0';
                    return true;
                }
            }
        }
    }
    return false;
}

static bool get_shell_version_fish(const char* exe, char* version, size_t version_size)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s --version 2>/dev/null", exe);
    char output[BUFFER_SIZE];
    if (get_command_output(cmd, output, sizeof(output))) {
        char* version_start = strstr(output, "version ");
        if (version_start) {
            version_start += strlen("version ");
            char* space = strchr(version_start, ' ');
            if (space) {
                size_t len = space - version_start;
                if (len < version_size - 1) {
                    strncpy(version, version_start, len);
                    version[len] = '\0';
                    return true;
                }
            }
        }
    }
    return false;
}

static bool get_shell_version_nu(const char* exe, char* version, size_t version_size)
{
    const char* nu_version = getenv("NU_VERSION");
    if (nu_version) {
        strncpy(version, nu_version, version_size - 1);
        version[version_size - 1] = '\0';
        return true;
    }
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s --version 2>/dev/null", exe);
    return get_command_output(cmd, version, version_size);
}

void shell_string(char* out, size_t n)
{
    const char* shell = getenv("SHELL");
    if (shell && *shell) {
        const char* shell_name = get_basename_path(shell);
        
        char version[VERSION_SIZE] = {0};
        bool has_version = false;
        
        if (str_equals_ignore_case(shell_name, "bash")) {
            has_version = get_shell_version_bash(shell_name, shell, version, sizeof(version));
        } else if (str_equals_ignore_case(shell_name, "zsh")) {
            has_version = get_shell_version_zsh(shell_name, shell, version, sizeof(version));
        } else if (str_equals_ignore_case(shell_name, "fish")) {
            has_version = get_shell_version_fish(shell_name, version, sizeof(version));
        } else if (str_equals_ignore_case(shell_name, "nu")) {
            has_version = get_shell_version_nu(shell_name, version, sizeof(version));
        }
        
        if (has_version && version[0]) {
            snprintf(out, n, "%s %s", shell_name, version);
        } else {
            snprintf(out, n, "%s", shell_name);
        }
        return;
    }
    
    char comm[128] = {0};
    if (read_file_data("/proc/self/comm", comm, sizeof(comm))) {
        char* newline = strchr(comm, '\n');
        if (newline) *newline = '\0';
        snprintf(out, n, "%s", comm);
        return;
    }
    
    snprintf(out, n, "unknown");
}

static bool get_terminal_version_termux(char* version, size_t version_size)
{
    const char* termux_version = getenv("TERMUX_VERSION");
    if (termux_version) {
        strncpy(version, termux_version, version_size - 1);
        version[version_size - 1] = '\0';
        return true;
    }
    return false;
}

static bool get_terminal_version_kitty(const char* exe, char* version, size_t version_size)
{
    char buffer[BUFFER_SIZE] = {0};
    
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    const char* kitty_paths[] = {
        "/usr/lib64/kitty/kitty/constants.py",
        "/usr/lib/kitty/kitty/constants.py",
#ifdef _PATH_LOCALBASE
        _PATH_LOCALBASE "/share/kitty/kitty/constants.py",
#endif
        NULL
    };
    
    for (int i = 0; kitty_paths[i]; i++) {
        if (read_file_data(kitty_paths[i], buffer, sizeof(buffer))) {
            const char* version_line = strstr(buffer, "version: Version = Version(");
            if (version_line) {
                version_line += strlen("version: Version = Version(");
                int major, minor, patch;
                if (sscanf(version_line, "%d,%d,%d", &major, &minor, &patch) == 3) {
                    snprintf(version, version_size, "%d.%d.%d", major, minor, patch);
                    return true;
                }
            }
        }
    }
#endif
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s --version 2>/dev/null", exe);
    char output[BUFFER_SIZE];
    if (get_command_output(cmd, output, sizeof(output))) {
        char* space = strchr(output, ' ');
        if (space) {
            space++;
            char* next_space = strchr(space, ' ');
            if (next_space) {
                size_t len = next_space - space;
                if (len < version_size - 1) {
                    strncpy(version, space, len);
                    version[len] = '\0';
                    return true;
                }
            }
        }
    }
    return false;
}

static bool get_terminal_version_gnome(const char* exe, char* version, size_t version_size)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "gnome-terminal --version 2>/dev/null");
    char output[BUFFER_SIZE];
    if (get_command_output(cmd, output, sizeof(output))) {
        char* terminal_pos = strstr(output, "Terminal ");
        if (terminal_pos) {
            terminal_pos += strlen("Terminal ");
            char* space = strchr(terminal_pos, ' ');
            if (space) {
                size_t len = space - terminal_pos;
                if (len < version_size - 1) {
                    strncpy(version, terminal_pos, len);
                    version[len] = '\0';
                    return true;
                }
            }
        }
    }
    return false;
}

static bool get_terminal_version_konsole(const char* exe, char* version, size_t version_size)
{
    const char* konsole_version = getenv("KONSOLE_VERSION");
    if (konsole_version) {
        long major = strtol(konsole_version, NULL, 10);
        if (major >= 0) {
            long patch = major % 100;
            major /= 100;
            long minor = major % 100;
            major /= 100;
            snprintf(version, version_size, "%ld.%ld.%ld", major, minor, patch);
            return true;
        }
    }
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s --version 2>/dev/null", exe);
    char output[BUFFER_SIZE];
    if (get_command_output(cmd, output, sizeof(output))) {
        char* space = strchr(output, ' ');
        if (space) {
            space++;
            char* next_space = strchr(space, ' ');
            if (next_space) {
                size_t len = next_space - space;
                if (len < version_size - 1) {
                    strncpy(version, space, len);
                    version[len] = '\0';
                    return true;
                }
            }
        }
    }
    return false;
}

static bool get_terminal_version_xterm(const char* exe, char* version, size_t version_size)
{
    const char* xterm_version = getenv("XTERM_VERSION");
    if (xterm_version) {
        strncpy(version, xterm_version, version_size - 1);
        version[version_size - 1] = '\0';
        return true;
    }
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s -version 2>&1", exe);
    char output[BUFFER_SIZE];
    if (get_command_output(cmd, output, sizeof(output))) {
        char* paren_start = strchr(output, '(');
        char* paren_end = strchr(output, ')');
        if (paren_start && paren_end && paren_end > paren_start) {
            paren_start++;
            size_t len = paren_end - paren_start;
            if (len < version_size - 1) {
                strncpy(version, paren_start, len);
                version[len] = '\0';
                return true;
            }
        }
    }
    return false;
}

static bool get_terminal_version_alacritty(const char* exe, char* version, size_t version_size)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s --version 2>/dev/null", exe);
    char output[BUFFER_SIZE];
    if (get_command_output(cmd, output, sizeof(output))) {
        char* space = strchr(output, ' ');
        if (space) {
            space++;
            char* next_space = strchr(space, ' ');
            if (next_space) {
                size_t len = next_space - space;
                if (len < version_size - 1) {
                    strncpy(version, space, len);
                    version[len] = '\0';
                    return true;
                }
            }
        }
    }
    return false;
}

static bool detect_terminal_by_process(char* out, size_t n)
{
    char comm[128] = {0};
    if (!read_file_data("/proc/self/comm", comm, sizeof(comm))) {
        return false;
    }
    
    char* newline = strchr(comm, '\n');
    if (newline) *newline = '\0';
    
    char version[VERSION_SIZE] = {0};
    bool has_version = false;
    
    if (strstr(comm, "termux") || strstr(comm, "com.termux")) {
        has_version = get_terminal_version_termux(version, sizeof(version));
        if (has_version) {
            snprintf(out, n, "Termux %s", version);
        } else {
            snprintf(out, n, "Termux");
        }
        return true;
    }
    
    if (strstr(comm, "kitty")) {
        has_version = get_terminal_version_kitty("kitty", version, sizeof(version));
        if (has_version) {
            snprintf(out, n, "kitty %s", version);
        } else {
            snprintf(out, n, "kitty");
        }
        return true;
    }
    
    if (strstr(comm, "gnome-terminal")) {
        has_version = get_terminal_version_gnome("gnome-terminal", version, sizeof(version));
        if (has_version) {
            snprintf(out, n, "GNOME Terminal %s", version);
        } else {
            snprintf(out, n, "GNOME Terminal");
        }
        return true;
    }
    
    if (strstr(comm, "konsole")) {
        has_version = get_terminal_version_konsole("konsole", version, sizeof(version));
        if (has_version) {
            snprintf(out, n, "Konsole %s", version);
        } else {
            snprintf(out, n, "Konsole");
        }
        return true;
    }
    
    if (strstr(comm, "alacritty")) {
        has_version = get_terminal_version_alacritty("alacritty", version, sizeof(version));
        if (has_version) {
            snprintf(out, n, "Alacritty %s", version);
        } else {
            snprintf(out, n, "Alacritty");
        }
        return true;
    }
    
    if (strstr(comm, "xterm")) {
        has_version = get_terminal_version_xterm("xterm", version, sizeof(version));
        if (has_version) {
            snprintf(out, n, "xterm (%s)", version);
        } else {
            snprintf(out, n, "xterm");
        }
        return true;
    }
    
    snprintf(out, n, "%s", comm);
    return true;
}

void terminal_string(char* out, size_t n)
{
#ifdef __ANDROID__
    if (get_terminal_version_termux(out, n)) {
        char temp[VERSION_SIZE];
        strncpy(temp, out, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
        snprintf(out, n, "Termux %s", temp);
        return;
    }
    
    if (access("/data/data/com.termux", F_OK) == 0) {
        snprintf(out, n, "Termux");
        return;
    }
#endif
    
    const char* term_program = getenv("TERM_PROGRAM");
    const char* term_program_version = getenv("TERM_PROGRAM_VERSION");
    
    if (term_program) {
        if (term_program_version) {
            snprintf(out, n, "%s %s", term_program, term_program_version);
        } else {
            snprintf(out, n, "%s", term_program);
        }
        return;
    }
    
    const char* lc_terminal = getenv("LC_TERMINAL");
    const char* lc_terminal_version = getenv("LC_TERMINAL_VERSION");
    
    if (lc_terminal) {
        if (lc_terminal_version) {
            snprintf(out, n, "%s %s", lc_terminal, lc_terminal_version);
        } else {
            snprintf(out, n, "%s", lc_terminal);
        }
        return;
    }
    
    if (detect_terminal_by_process(out, n)) {
        return;
    }
    
    const char* term = getenv("TERM");
    const char* colorterm = getenv("COLORTERM");
    
    if (term && colorterm) {
        snprintf(out, n, "%s (%s)", term, colorterm);
    } else if (term) {
        snprintf(out, n, "%s", term);
    } else {
        snprintf(out, n, "unknown");
    }
}

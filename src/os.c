#include "common.h"
#include "os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(__ANDROID__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include <sys/utsname.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <winternl.h>
typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
#endif

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

/* ---------- utils ---------- */

static void trim_newline(char* s) { 
    if (!s) return; 
    size_t n = strlen(s); 
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) 
        s[--n] = 0; 
}

static const char* arch_from_uname_machine(const char* m) {
    if (!m) return "?";
    if (!strcmp(m, "x86_64") || !strcmp(m, "amd64")) return "x86_64";
    if (!strcmp(m, "i386") || !strcmp(m, "i686")) return "x86";
    if (!strcmp(m, "arm64") || !strcmp(m, "aarch64")) return "arm64";
    if (!strncmp(m, "arm", 3)) return "arm";
    if (!strncmp(m, "riscv", 5)) return "riscv";
    return m;
}

/* ---------- macOS helpers (native) ---------- */
#ifdef __APPLE__
static int read_plist_value(const char* path, const char* key, char* out, size_t outsz) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    
    char line[1024], want[256];
    snprintf(want, sizeof(want), "<key>%s</key>", key);
    int found = 0;
    
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, want)) {
            if (fgets(line, sizeof(line), f)) {
                char* s = strstr(line, "<string>");
                char* e = strstr(line, "</string>");
                if (s && e && e > s) {
                    s += 8;
                    size_t len = (size_t)(e - s);
                    if (len >= outsz) len = outsz - 1;
                    memcpy(out, s, len);
                    out[len] = 0;
                    found = 1;
                    break;
                }
            }
        }
    }
    fclose(f);
    return found;
}
#endif

/* ---------- detectors ---------- */

static void detect_windows(char* out, size_t n) {
#ifdef _WIN32
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    RTL_OSVERSIONINFOW rovi = {0};
    
    if (hMod) {
        RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (RtlGetVersion) {
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (RtlGetVersion(&rovi) != 0) {
                rovi.dwMajorVersion = rovi.dwMinorVersion = rovi.dwBuildNumber = 0;
            }
        }
    }
    
    SYSTEM_INFO si;
    ZeroMemory(&si, sizeof(si));
    GetNativeSystemInfo(&si);
    
    const char* arch = "?";
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: arch = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: arch = "arm64"; break;
        case PROCESSOR_ARCHITECTURE_ARM: arch = "arm"; break;
        default: break;
    }
    
    if (rovi.dwMajorVersion) {
        snprintf(out, n, "Windows %lu.%lu (Build %lu) %s",
                rovi.dwMajorVersion, rovi.dwMinorVersion, rovi.dwBuildNumber, arch);
    } else {
        snprintf(out, n, "Windows (unknown) %s", arch);
    }
#endif
}

static void detect_android(char* out, size_t n) {
#ifdef __ANDROID__
    char ver[64] = {0};
    char codename[64] = {0};

    __system_property_get("ro.build.version.release", ver);
    __system_property_get("ro.build.version.codename", codename);

    struct utsname uts;
    uname(&uts);

    snprintf(out, n, "Android %s %s %s",
             codename[0] ? codename : "REL",
             ver[0] ? ver : "?",
             arch_from_uname_machine(uts.machine));
#endif
}

static void detect_linux(char* out, size_t n) {
#ifdef __linux__
    struct utsname uts;
    uname(&uts);
    
    FILE* fp = fopen("/etc/os-release", "r");
    char line[512], pretty[256] = {0};
    
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (!strncmp(line, "PRETTY_NAME=", 12)) {
                char* q = strchr(line, '"');
                if (q) {
                    char* q2 = strrchr(q + 1, '"');
                    if (q2 && q2 > q + 1) {
                        size_t len = (size_t)(q2 - (q + 1));
                        if (len >= sizeof(pretty)) len = sizeof(pretty) - 1;
                        memcpy(pretty, q + 1, len);
                        pretty[len] = 0;
                    }
                }
                break;
            }
        }
        fclose(fp);
    }
    
    if (pretty[0]) {
        snprintf(out, n, "%s %s", pretty, arch_from_uname_machine(uts.machine));
    } else {
        snprintf(out, n, "Linux %s %s", uts.release, arch_from_uname_machine(uts.machine));
    }
#endif
}

static void detect_bsd(char* out, size_t n) {
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    struct utsname uts;
    uname(&uts);
    snprintf(out, n, "%s %s %s", uts.sysname, uts.release, arch_from_uname_machine(uts.machine));
#endif
}

static void detect_macos(char* out, size_t n) {
#ifdef __APPLE__
    struct utsname uts;
    uname(&uts);

    char productVer[64] = {0}, buildVer[64] = {0};
    int gotPV = read_plist_value("/System/Library/CoreServices/SystemVersion.plist", "ProductVersion", productVer, sizeof(productVer));
    int gotBV = read_plist_value("/System/Library/CoreServices/SystemVersion.plist", "ProductBuildVersion", buildVer, sizeof(buildVer));

    const char* arch = arch_from_uname_machine(uts.machine);
    
    if (gotPV && gotBV) {
        snprintf(out, n, "macOS %s %s %s", productVer, buildVer, arch);
    } else if (gotPV) {
        snprintf(out, n, "macOS %s %s", productVer, arch);
    } else {
        snprintf(out, n, "macOS (Darwin %s) %s", uts.release, arch);
    }
#endif
}

/* ---------- main function (unchanged) ---------- */

void os_string(char* out, size_t n) {
    if (!out || n == 0) return;
    
#ifdef _WIN32
    detect_windows(out, n);
#elif defined(__APPLE__)
    detect_macos(out, n);
#elif defined(__ANDROID__)
    detect_android(out, n);
#elif defined(__linux__)
    detect_linux(out, n);
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    detect_bsd(out, n);
#else
    snprintf(out, n, "Unknown OS");
#endif
}

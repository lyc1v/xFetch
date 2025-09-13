// src/host.c
#include "common.h"
#include "host.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(__ANDROID__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include <sys/utsname.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#endif

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

// Advanced host result structure
typedef struct {
    char *family;
    char *name; 
    char *version;
    char *sku;
    char *serial;
    char *uuid;
    char *vendor;
    char *type;
    char *chassis;
    int valid;
} HostResult;

// Internal buffer management
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} HostBuffer;

static HostBuffer* hostbuf_create(size_t initial_cap) {
    HostBuffer *buf = malloc(sizeof(HostBuffer));
    if(!buf) return NULL;
    buf->data = malloc(initial_cap + 1);
    if(!buf->data) { free(buf); return NULL; }
    buf->data[0] = '\0';
    buf->len = 0;
    buf->capacity = initial_cap;
    return buf;
}

static void hostbuf_destroy(HostBuffer *buf) {
    if(buf) {
        if(buf->data) free(buf->data);
        free(buf);
    }
}

static int hostbuf_append(HostBuffer *buf, const char *str) {
    if(!buf || !str) return -1;
    size_t slen = strlen(str);
    if(buf->len + slen >= buf->capacity) {
        size_t new_cap = (buf->len + slen + 256) * 2;
        char *new_data = realloc(buf->data, new_cap + 1);
        if(!new_data) return -1;
        buf->data = new_data;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->len, str, slen);
    buf->len += slen;
    buf->data[buf->len] = '\0';
    return 0;
}

static char* hostbuf_strdup(const HostBuffer *buf) {
    if(!buf || !buf->data) return NULL;
    return strdup(buf->data);
}

// Utility functions with complex error handling
static void strip_whitespace(char *str) {
    if(!str) return;
    char *end;
    while(*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')) str++;
    if(*str == 0) return;
    end = str + strlen(str) - 1;
    while(end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    *(end + 1) = '\0';
}

static int read_file_content(const char *path, char *buffer, size_t bufsize) {
    int fd, ret = 0;
    ssize_t bytes_read;
    
    if(!path || !buffer || bufsize == 0) return -1;
    
    fd = open(path, O_RDONLY);
    if(fd < 0) return -1;
    
    bytes_read = read(fd, buffer, bufsize - 1);
    close(fd);
    
    if(bytes_read <= 0) return -1;
    
    buffer[bytes_read] = '\0';
    strip_whitespace(buffer);
    
    if(strlen(buffer) > 0) ret = 1;
    return ret;
}

static int exec_command_pipe(const char *cmd, char *output, size_t output_size) {
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int ret = 0;
    
    if(!cmd || !output || output_size == 0) return -1;
    
    fp = popen(cmd, "r");
    if(!fp) return -1;
    
    if((read = getline(&line, &len, fp)) != -1) {
        if(read > 0 && (size_t)read < output_size) {
            strncpy(output, line, output_size - 1);
            output[output_size - 1] = '\0';
            strip_whitespace(output);
            if(strlen(output) > 0) ret = 1;
        }
    }
    
    if(line) free(line);
    pclose(fp);
    return ret;
}

static int check_file_contains(const char *filepath, const char *pattern) {
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int found = 0;
    
    if(!filepath || !pattern) return 0;
    
    fp = fopen(filepath, "r");
    if(!fp) return 0;
    
    while((read = getline(&line, &len, fp)) != -1 && !found) {
        if(strstr(line, pattern)) found = 1;
    }
    
    if(line) free(line);
    fclose(fp);
    return found;
}

// WSL detection with multiple methods
static int detect_wsl_environment(void) {
#ifdef __linux__
    if(check_file_contains("/proc/version", "Microsoft") ||
       check_file_contains("/proc/version", "WSL") ||
       check_file_contains("/proc/sys/kernel/osrelease", "microsoft") ||
       check_file_contains("/proc/sys/kernel/osrelease", "WSL")) {
        return 1;
    }
    
    // Check for WSL-specific mount points
    if(access("/mnt/c", F_OK) == 0 && access("/proc/sys/fs/binfmt_misc/WSLInterop", F_OK) == 0) {
        return 1;
    }
#endif
    return 0;
}

// Advanced Linux host detection
static int detect_linux_host_info(HostResult *result) {
#ifdef __linux__
    char buffer[512];
    HostBuffer *name_buf = hostbuf_create(256);
    HostBuffer *vendor_buf = hostbuf_create(256);
    int found_something = 0;
    
    // Try multiple DMI paths
    const char *dmi_paths[] = {
        "/sys/class/dmi/id",
        "/sys/devices/virtual/dmi/id",
        NULL
    };
    
    for(int i = 0; dmi_paths[i]; i++) {
        char full_path[512];
        
        // Product name
        snprintf(full_path, sizeof(full_path), "%s/product_name", dmi_paths[i]);
        if(read_file_content(full_path, buffer, sizeof(buffer)) > 0) {
            if(name_buf->len > 0) hostbuf_append(name_buf, " ");
            hostbuf_append(name_buf, buffer);
            found_something = 1;
        }
        
        // Product family
        snprintf(full_path, sizeof(full_path), "%s/product_family", dmi_paths[i]);
        if(read_file_content(full_path, buffer, sizeof(buffer)) > 0) {
            if(!result->family) result->family = strdup(buffer);
        }
        
        // Product version
        snprintf(full_path, sizeof(full_path), "%s/product_version", dmi_paths[i]);
        if(read_file_content(full_path, buffer, sizeof(buffer)) > 0) {
            if(!result->version) result->version = strdup(buffer);
        }
        
        // Vendor info
        snprintf(full_path, sizeof(full_path), "%s/sys_vendor", dmi_paths[i]);
        if(read_file_content(full_path, buffer, sizeof(buffer)) > 0) {
            hostbuf_append(vendor_buf, buffer);
        }
        
        // SKU
        snprintf(full_path, sizeof(full_path), "%s/product_sku", dmi_paths[i]);
        if(read_file_content(full_path, buffer, sizeof(buffer)) > 0) {
            if(!result->sku) result->sku = strdup(buffer);
        }
        
        // Serial
        snprintf(full_path, sizeof(full_path), "%s/product_serial", dmi_paths[i]);
        if(read_file_content(full_path, buffer, sizeof(buffer)) > 0) {
            if(!result->serial) result->serial = strdup(buffer);
        }
        
        // UUID
        snprintf(full_path, sizeof(full_path), "%s/product_uuid", dmi_paths[i]);
        if(read_file_content(full_path, buffer, sizeof(buffer)) > 0) {
            if(!result->uuid) result->uuid = strdup(buffer);
        }
        
        if(found_something) break;
    }
    
    // ARM/embedded device fallback
    if(!found_something) {
        if(read_file_content("/proc/device-tree/model", buffer, sizeof(buffer)) > 0) {
            hostbuf_append(name_buf, buffer);
            found_something = 1;
        }
        
        // Try cpuinfo for some ARM devices
        if(!found_something) {
            FILE *fp = fopen("/proc/cpuinfo", "r");
            if(fp) {
                char *line = NULL;
                size_t len = 0;
                while(getline(&line, &len, fp) != -1) {
                    if(strncmp(line, "Hardware", 8) == 0) {
                        char *colon = strchr(line, ':');
                        if(colon) {
                            colon++;
                            while(*colon && (*colon == ' ' || *colon == '\t')) colon++;
                            strip_whitespace(colon);
                            if(strlen(colon) > 0) {
                                hostbuf_append(name_buf, colon);
                                found_something = 1;
                                break;
                            }
                        }
                    }
                }
                if(line) free(line);
                fclose(fp);
            }
        }
    }
    
    if(name_buf->len > 0) result->name = hostbuf_strdup(name_buf);
    if(vendor_buf->len > 0) result->vendor = hostbuf_strdup(vendor_buf);
    
    hostbuf_destroy(name_buf);
    hostbuf_destroy(vendor_buf);
    
    return found_something;
#else
    (void)result;
    return 0;
#endif
}

// Advanced Android detection
static int detect_android_host_info(HostResult *result) {
#ifdef __ANDROID__
    char prop_buf[PROP_VALUE_MAX];
    HostBuffer *name_buf = hostbuf_create(256);
    int found = 0;
    
    // Manufacturer
    if(__system_property_get("ro.product.manufacturer", prop_buf) > 0) {
        result->vendor = strdup(prop_buf);
        hostbuf_append(name_buf, prop_buf);
        found = 1;
    }
    
    // Model
    if(__system_property_get("ro.product.model", prop_buf) > 0) {
        if(name_buf->len > 0) hostbuf_append(name_buf, " ");
        hostbuf_append(name_buf, prop_buf);
        found = 1;
    }
    
    // Brand
    if(__system_property_get("ro.product.brand", prop_buf) > 0) {
        if(!result->family) result->family = strdup(prop_buf);
    }
    
    // Device name
    if(__system_property_get("ro.product.device", prop_buf) > 0) {
        if(!result->sku) result->sku = strdup(prop_buf);
    }
    
    // Serial number
    if(__system_property_get("ro.serialno", prop_buf) > 0) {
        result->serial = strdup(prop_buf);
    }
    
    if(name_buf->len > 0) result->name = hostbuf_strdup(name_buf);
    hostbuf_destroy(name_buf);
    
    return found;
#else
    (void)result;
    return 0;
#endif
}

// Advanced macOS detection
static int detect_macos_host_info(HostResult *result) {
#ifdef __APPLE__
    char buffer[256];
    size_t size = sizeof(buffer);
    
    // Hardware model
    if(sysctlbyname("hw.model", buffer, &size, NULL, 0) == 0) {
        char final_name[512];
        snprintf(final_name, sizeof(final_name), "Apple %s", buffer);
        result->name = strdup(final_name);
    }
    
    // Try IOKit for more detailed info
    io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault, 
        IOServiceMatching("IOPlatformExpertDevice"));
    if(service) {
        CFStringRef model_ref = IORegistryEntryCreateCFProperty(service, 
            CFSTR("model"), kCFAllocatorDefault, kNilOptions);
        if(model_ref) {
            if(CFStringGetCString(model_ref, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                if(!result->name) result->name = strdup(buffer);
            }
            CFRelease(model_ref);
        }
        
        CFStringRef serial_ref = IORegistryEntryCreateCFProperty(service,
            CFSTR("IOPlatformSerialNumber"), kCFAllocatorDefault, kNilOptions);
        if(serial_ref) {
            if(CFStringGetCString(serial_ref, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                result->serial = strdup(buffer);
            }
            CFRelease(serial_ref);
        }
        
        IOObjectRelease(service);
    }
    
    result->vendor = strdup("Apple Inc.");
    return result->name != NULL;
#else
    (void)result;
    return 0;
#endif
}

// Advanced Windows detection
static int detect_windows_host_info(HostResult *result) {
#ifdef _WIN32
    HKEY hkey;
    DWORD size;
    char buffer[256];
    HostBuffer *name_buf = hostbuf_create(256);
    int found = 0;
    
    // BIOS information
    if(RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
        "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        
        size = sizeof(buffer);
        if(RegQueryValueExA(hkey, "SystemManufacturer", NULL, NULL, 
            (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
            result->vendor = strdup(buffer);
            hostbuf_append(name_buf, buffer);
            found = 1;
        }
        
        size = sizeof(buffer);
        if(RegQueryValueExA(hkey, "SystemProductName", NULL, NULL,
            (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
            if(name_buf->len > 0) hostbuf_append(name_buf, " ");
            hostbuf_append(name_buf, buffer);
            found = 1;
        }
        
        size = sizeof(buffer);
        if(RegQueryValueExA(hkey, "SystemVersion", NULL, NULL,
            (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
            result->version = strdup(buffer);
        }
        
        RegCloseKey(hkey);
    }
    
    // Computer system information
    if(RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\HardwareConfig\\Current", 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        
        size = sizeof(buffer);
        if(RegQueryValueExA(hkey, "SystemSku", NULL, NULL,
            (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
            result->sku = strdup(buffer);
        }
        
        RegCloseKey(hkey);
    }
    
    if(name_buf->len > 0) result->name = hostbuf_strdup(name_buf);
    hostbuf_destroy(name_buf);
    
    return found;
#else
    (void)result;
    return 0;
#endif
}

// BSD detection
static int detect_bsd_host_info(HostResult *result) {
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    char buffer[256];
    size_t size = sizeof(buffer);
    
    if(sysctlbyname("hw.model", buffer, &size, NULL, 0) == 0) {
        result->name = strdup(buffer);
        return 1;
    }
    
    struct utsname uts;
    if(uname(&uts) == 0) {
        result->name = strdup(uts.machine);
        return 1;
    }
    
    return 0;
#else
    (void)result;
    return 0;
#endif
}

// Main detection function
static const char* detect_host_comprehensive(HostResult *result) {
    if(!result) return "Invalid result structure";
    
    memset(result, 0, sizeof(HostResult));
    
#ifdef __APPLE__
    if(detect_macos_host_info(result)) {
        result->valid = 1;
        return NULL;
    }
#elif defined(__ANDROID__)
    if(detect_android_host_info(result)) {
        result->valid = 1;
        return NULL;
    }
#elif defined(_WIN32)
    if(detect_windows_host_info(result)) {
        result->valid = 1;
        return NULL;
    }
#elif defined(__linux__)
    if(detect_linux_host_info(result)) {
        result->valid = 1;
        return NULL;
    }
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    if(detect_bsd_host_info(result)) {
        result->valid = 1;
        return NULL;
    }
#endif
    
    return "Unable to detect host information on this platform";
}

static void destroy_host_result(HostResult *result) {
    if(!result) return;
    
    if(result->family) { free(result->family); result->family = NULL; }
    if(result->name) { free(result->name); result->name = NULL; }
    if(result->version) { free(result->version); result->version = NULL; }
    if(result->sku) { free(result->sku); result->sku = NULL; }
    if(result->serial) { free(result->serial); result->serial = NULL; }
    if(result->uuid) { free(result->uuid); result->uuid = NULL; }
    if(result->vendor) { free(result->vendor); result->vendor = NULL; }
    if(result->type) { free(result->type); result->type = NULL; }
    if(result->chassis) { free(result->chassis); result->chassis = NULL; }
}

// Original function (preserved)
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

// Advanced host detection function (fastfetch-style)
int host_detect_advanced(char *family, size_t family_size,
                        char *name, size_t name_size,
                        char *version, size_t version_size,
                        char *vendor, size_t vendor_size,
                        char *sku, size_t sku_size,
                        char *serial, size_t serial_size,
                        char *uuid, size_t uuid_size) {
    
    HostResult result;
    const char *error = detect_host_comprehensive(&result);
    
    if(error || !result.valid) {
        destroy_host_result(&result);
        return -1;
    }
    
    // Copy results to output buffers
    if(family && family_size > 0) {
        if(result.family) {
            strncpy(family, result.family, family_size - 1);
            family[family_size - 1] = '\0';
        } else {
            family[0] = '\0';
        }
    }
    
    if(name && name_size > 0) {
        if(result.name) {
            strncpy(name, result.name, name_size - 1);
            name[name_size - 1] = '\0';
        } else {
            name[0] = '\0';
        }
    }
    
    if(version && version_size > 0) {
        if(result.version) {
            strncpy(version, result.version, version_size - 1);
            version[version_size - 1] = '\0';
        } else {
            version[0] = '\0';
        }
    }
    
    if(vendor && vendor_size > 0) {
        if(result.vendor) {
            strncpy(vendor, result.vendor, vendor_size - 1);
            vendor[vendor_size - 1] = '\0';
        } else {
            vendor[0] = '\0';
        }
    }
    
    if(sku && sku_size > 0) {
        if(result.sku) {
            strncpy(sku, result.sku, sku_size - 1);
            sku[sku_size - 1] = '\0';
        } else {
            sku[0] = '\0';
        }
    }
    
    if(serial && serial_size > 0) {
        if(result.serial) {
            strncpy(serial, result.serial, serial_size - 1);
            serial[serial_size - 1] = '\0';
        } else {
            serial[0] = '\0';
        }
    }
    
    if(uuid && uuid_size > 0) {
        if(result.uuid) {
            strncpy(uuid, result.uuid, uuid_size - 1);
            uuid[uuid_size - 1] = '\0';
        } else {
            uuid[0] = '\0';
        }
    }
    
    destroy_host_result(&result);
    return 0;
}

// WSL-aware enhanced host string
void host_string_enhanced(char *out, size_t n) {
    if(!out || n == 0) return;
    
#ifdef __linux__
    if(detect_wsl_environment()) {
        char distro[256] = {0};
        struct utsname uts;
        
        // Try to get distro name
        FILE *fp = fopen("/etc/os-release", "r");
        if(fp) {
            char *line = NULL;
            size_t len = 0;
            while(getline(&line, &len, fp) != -1) {
                if(strncmp(line, "PRETTY_NAME=", 12) == 0) {
                    char *quote = strchr(line, '"');
                    if(quote) {
                        char *end_quote = strrchr(quote + 1, '"');
                        if(end_quote && end_quote > quote + 1) {
                            size_t dist_len = end_quote - (quote + 1);
                            if(dist_len < sizeof(distro)) {
                                memcpy(distro, quote + 1, dist_len);
                                distro[dist_len] = '\0';
                                break;
                            }
                        }
                    }
                }
            }
            if(line) free(line);
            fclose(fp);
        }
        
        if(uname(&uts) == 0) {
            snprintf(out, n, "Windows Subsystem for Linux - %s (%s)",
                    distro[0] ? distro : "Linux", uts.release);
        } else {
            snprintf(out, n, "Windows Subsystem for Linux");
        }
        return;
    }
#endif
    
    HostResult result;
    const char *error = detect_host_comprehensive(&result);
    
    if(!error && result.valid) {
        if(result.name) {
            if(result.version) {
                snprintf(out, n, "%s (%s)", result.name, result.version);
            } else {
                snprintf(out, n, "%s", result.name);
            }
        } else if(result.family) {
            snprintf(out, n, "%s", result.family);
        } else {
            snprintf(out, n, "(unknown)");
        }
        destroy_host_result(&result);
    } else {
        // Fallback to hostname
        if(gethostname(out, n) != 0) {
            snprintf(out, n, "(unknown)");
        }
    }
}

// Print comprehensive host info (fastfetch-style)
void host_print_comprehensive(void) {
    HostResult result;
    const char *error = detect_host_comprehensive(&result);
    
    if(error) {
        printf("Host : Error - %s\n", error);
        return;
    }
    
    if(!result.valid || (!result.name && !result.family)) {
        printf("Host : (unknown - no product info available)\n");
        destroy_host_result(&result);
        return;
    }
    
#ifdef __linux__
    if(detect_wsl_environment()) {
        char distro[256] = {0};
        struct utsname uts;
        
        FILE *fp = fopen("/etc/os-release", "r");
        if(fp) {
            char *line = NULL;
            size_t len = 0;
            while(getline(&line, &len, fp) != -1) {
                if(strncmp(line, "PRETTY_NAME=", 12) == 0) {
                    char *quote = strchr(line, '"');
                    if(quote) {
                        char *end_quote = strrchr(quote + 1, '"');
                        if(end_quote && end_quote > quote + 1) {
                            size_t dist_len = end_quote - (quote + 1);
                            if(dist_len < sizeof(distro)) {
                                memcpy(distro, quote + 1, dist_len);
                                distro[dist_len] = '\0';
                                break;
                            }
                        }
                    }
                }
            }
            if(line) free(line);
            fclose(fp);
        }
        
        if(uname(&uts) == 0) {
            printf("Host : Windows Subsystem for Linux - %s (%s)\n",
                   distro[0] ? distro : "Linux", uts.release);
        } else {
            printf("Host : Windows Subsystem for Linux\n");
        }
        destroy_host_result(&result);
        return;
    }
#endif
    
    printf("Host : ");
    
    if(result.name) {
        printf("%s", result.name);
        if(result.version) {
            printf(" (%s)", result.version);
        }
    } else if(result.family) {
        printf("%s", result.family);
    }
    
    printf("\n");
    
    // Optional: print additional info if available
    if(result.vendor && strlen(result.vendor) > 0) {
        printf("Vendor : %s\n", result.vendor);
    }
    if(result.sku && strlen(result.sku) > 0) {
        printf("SKU : %s\n", result.sku);
    }
    if(result.serial && strlen(result.serial) > 0) {
        printf("Serial : %s\n", result.serial);
    }
    
    destroy_host_result(&result);
}

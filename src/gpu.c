#include "common.h"
#include "gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>

#define GPU_BUFFER_SIZE 1024
#define FF_GPU_TEMP_UNSET -1000.0
#define MAX_GPU_COUNT 8

typedef struct {
    char name[512];
    char vendor[128];
    char driver[64];
    int memory_mb;
    int clock_mhz;
    double temperature;
    int type;
} gpu_result_t;

typedef struct {
    void* data;
    size_t length;
    size_t capacity;
    size_t element_size;
} FFlist;

typedef struct {
    char name[256];
    char vendor[128];
    char driver[64];
    uint64_t memory;
    uint32_t coreCount;
    double temperature;
    int type;
} FFGPUResult;

typedef enum {
    FF_GPU_DETECTION_METHOD_PCI,
    FF_GPU_DETECTION_METHOD_VULKAN,
    FF_GPU_DETECTION_METHOD_OPENCL,
    FF_GPU_DETECTION_METHOD_OPENGL
} FFGPUDetectionMethod;

typedef struct {
    FFGPUDetectionMethod detectionMethod;
    int temp;
    int hideType;
} FFGPUOptions;

typedef struct {
    FFlist gpus;
    const char* error;
} FFVulkanResult;

typedef struct {
    FFlist gpus;
    const char* error;
} FFOpenCLResult;

static int read_file_content(const char* path, char* buffer, size_t size);
static int read_sysfs_value(const char* base_path, const char* file, char* buffer, size_t size);
static int detect_nvidia_gpu(gpu_result_t* gpu);
static int detect_amd_gpu(gpu_result_t* gpu);
static int detect_intel_gpu(gpu_result_t* gpu);
static int detect_vulkan_gpu(FFlist* result);
static int detect_opencl_gpu(FFlist* result);
static int detect_opengl_gpu(FFlist* result);
static int detect_android_gpu_modern(FFlist* result);
static void trim_string(char* str);
static int string_starts_with(const char* str, const char* prefix);
static int string_contains(const char* haystack, const char* needle);
static int get_android_property(const char* prop, char* buffer, size_t size);
static double parseTZDir(int dfd, char* buffer, size_t buffer_size);
static double ffGPUDetectTempFromTZ(void);
static int ffReadFileBufferRelative(int dfd, const char* filename, char* buffer, size_t size);
static int ffStrbufStartsWithS(const char* buffer, const char* prefix);
static double ffStrbufToDouble(const char* buffer, double fallback);
static FFVulkanResult* ffDetectVulkan(void);
static FFOpenCLResult* ffDetectOpenCL(void);
static const char* detectByOpenGL(FFlist* result);
static const char* ffDetectGPUImpl(const FFGPUOptions* options, FFlist* result);
static void ffListDestroy(FFlist* list);
static void ffListInitMove(FFlist* dest, FFlist* src);
static void ffListInit(FFlist* list, size_t element_size);
static void ffListAdd(FFlist* list, const void* item);

static int read_file_content(const char* path, char* buffer, size_t size) {
    if (!path || !buffer || size == 0) return 0;
    
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    
    ssize_t bytes_read = read(fd, buffer, size - 1);
    close(fd);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        trim_string(buffer);
        return bytes_read > 0 && buffer[0] != '\0';
    }
    return 0;
}

static int read_sysfs_value(const char* base_path, const char* file, char* buffer, size_t size) {
    if (!base_path || !file) return 0;
    
    char full_path[512];
    int ret = snprintf(full_path, sizeof(full_path), "%s/%s", base_path, file);
    if (ret >= (int)sizeof(full_path) || ret < 0) return 0;
    
    return read_file_content(full_path, buffer, size);
}

static int ffReadFileBufferRelative(int dfd, const char* filename, char* buffer, size_t size) {
    if (!filename || !buffer || size == 0) return 0;
    
    int fd = openat(dfd, filename, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    
    ssize_t bytes_read = read(fd, buffer, size - 1);
    close(fd);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        trim_string(buffer);
        return 1;
    }
    return 0;
}

static int ffStrbufStartsWithS(const char* buffer, const char* prefix) {
    return string_starts_with(buffer, prefix);
}

static double ffStrbufToDouble(const char* buffer, double fallback) {
    if (!buffer) return fallback;
    
    char* endptr;
    errno = 0;
    double value = strtod(buffer, &endptr);
    
    if (errno != 0 || endptr == buffer) return fallback;
    
    while (*endptr && (*endptr == ' ' || *endptr == '\t' || *endptr == '\n')) endptr++;
    if (*endptr != '\0') return fallback;
    
    return value;
}

static int detect_nvidia_gpu(gpu_result_t* gpu) {
    char buffer[GPU_BUFFER_SIZE];
    memset(gpu, 0, sizeof(*gpu));
    
    if (read_file_content("/proc/driver/nvidia/gpus/0/information", buffer, sizeof(buffer))) {
        char* line = strtok(buffer, "\n");
        while (line) {
            if (string_starts_with(line, "Model:")) {
                char* model = line + 6;
                while (*model && (*model == ' ' || *model == '\t')) model++;
                snprintf(gpu->name, sizeof(gpu->name), "%s", model);
                strcpy(gpu->vendor, "NVIDIA");
                strcpy(gpu->driver, "nvidia");
                return 1;
            }
            line = strtok(NULL, "\n");
        }
    }
    
    FILE* f = popen("nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader,nounits 2>/dev/null", "r");
    if (f) {
        if (fgets(buffer, sizeof(buffer), f) && strlen(buffer) > 0) {
            pclose(f);
            trim_string(buffer);
            
            char* name = strtok(buffer, ",");
            char* driver = strtok(NULL, ",");
            char* memory = strtok(NULL, ",");
            
            if (name) {
                trim_string(name);
                snprintf(gpu->name, sizeof(gpu->name), "%s", name);
                strcpy(gpu->vendor, "NVIDIA");
                
                if (driver) {
                    trim_string(driver);
                    snprintf(gpu->driver, sizeof(gpu->driver), "%s", driver);
                }
                
                if (memory) {
                    trim_string(memory);
                    gpu->memory_mb = atoi(memory);
                }
                return 1;
            }
        } else {
            pclose(f);
        }
    }
    
    DIR* dir = opendir("/sys/class/drm/");
    if (!dir) return 0;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!string_starts_with(entry->d_name, "card") || strchr(entry->d_name, '-')) continue;
        
        char card_path[256];
        snprintf(card_path, sizeof(card_path), "/sys/class/drm/%s/device", entry->d_name);
        
        if (read_sysfs_value(card_path, "vendor", buffer, sizeof(buffer))) {
            uint32_t vendor_id = strtoul(buffer, NULL, 0);
            if (vendor_id == 0x10de) {
                strcpy(gpu->vendor, "NVIDIA");
                strcpy(gpu->driver, "nouveau");
                
                if (read_sysfs_value(card_path, "device", buffer, sizeof(buffer))) {
                    snprintf(gpu->name, sizeof(gpu->name), "NVIDIA GPU [%s]", buffer);
                } else {
                    strcpy(gpu->name, "NVIDIA Graphics");
                }
                closedir(dir);
                return 1;
            }
        }
    }
    
    closedir(dir);
    return 0;
}

static int detect_amd_gpu(gpu_result_t* gpu) {
    char buffer[GPU_BUFFER_SIZE];
    memset(gpu, 0, sizeof(*gpu));
    
    FILE* f = popen("rocm-smi --showproductname --csv 2>/dev/null | tail -n +2", "r");
    if (f) {
        if (fgets(buffer, sizeof(buffer), f) && strlen(buffer) > 0) {
            pclose(f);
            trim_string(buffer);
            if (!string_contains(buffer, "Not supported")) {
                snprintf(gpu->name, sizeof(gpu->name), "%s", buffer);
                strcpy(gpu->vendor, "AMD");
                strcpy(gpu->driver, "amdgpu");
                return 1;
            }
        } else {
            pclose(f);
        }
    }
    
    DIR* dir = opendir("/sys/class/drm/");
    if (!dir) return 0;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!string_starts_with(entry->d_name, "card") || strchr(entry->d_name, '-')) continue;
        
        char card_path[256];
        snprintf(card_path, sizeof(card_path), "/sys/class/drm/%s/device", entry->d_name);
        
        if (read_sysfs_value(card_path, "vendor", buffer, sizeof(buffer))) {
            uint32_t vendor_id = strtoul(buffer, NULL, 0);
            if (vendor_id == 0x1002) {
                strcpy(gpu->vendor, "AMD");
                
                if (read_sysfs_value(card_path, "driver", buffer, sizeof(buffer))) {
                    snprintf(gpu->driver, sizeof(gpu->driver), "%s", buffer);
                } else {
                    strcpy(gpu->driver, "amdgpu");
                }
                
                if (read_sysfs_value(card_path, "device", buffer, sizeof(buffer))) {
                    snprintf(gpu->name, sizeof(gpu->name), "AMD GPU [%s]", buffer);
                } else {
                    strcpy(gpu->name, "AMD Radeon Graphics");
                }
                closedir(dir);
                return 1;
            }
        }
    }
    
    closedir(dir);
    
    const char* amd_libs[] = {
        "/usr/lib/x86_64-linux-gnu/libdrm_amdgpu.so",
        "/usr/lib/libdrm_amdgpu.so",
        "/usr/lib64/libdrm_amdgpu.so",
        NULL
    };
    
    for (int i = 0; amd_libs[i]; i++) {
        if (access(amd_libs[i], F_OK) == 0) {
            strcpy(gpu->vendor, "AMD");
            strcpy(gpu->name, "AMD Radeon Graphics");
            strcpy(gpu->driver, "amdgpu");
            return 1;
        }
    }
    
    return 0;
}

static int detect_intel_gpu(gpu_result_t* gpu) {
    char buffer[GPU_BUFFER_SIZE];
    memset(gpu, 0, sizeof(*gpu));
    
    DIR* dir = opendir("/sys/class/drm/");
    if (!dir) return 0;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!string_starts_with(entry->d_name, "card") || strchr(entry->d_name, '-')) continue;
        
        char card_path[256];
        snprintf(card_path, sizeof(card_path), "/sys/class/drm/%s/device", entry->d_name);
        
        if (read_sysfs_value(card_path, "vendor", buffer, sizeof(buffer))) {
            uint32_t vendor_id = strtoul(buffer, NULL, 0);
            if (vendor_id == 0x8086) {
                strcpy(gpu->vendor, "Intel");
                
                if (read_sysfs_value(card_path, "driver", buffer, sizeof(buffer))) {
                    snprintf(gpu->driver, sizeof(gpu->driver), "%s", buffer);
                } else {
                    strcpy(gpu->driver, "i915");
                }
                
                if (read_sysfs_value(card_path, "device", buffer, sizeof(buffer))) {
                    uint32_t device_id = strtoul(buffer, NULL, 0);
                    
                    if ((device_id & 0xff00) == 0x5600) {
                        strcpy(gpu->name, "Intel Arc Graphics");
                    } else if ((device_id & 0xff00) == 0x4600 || (device_id & 0xff00) == 0x9a00) {
                        strcpy(gpu->name, "Intel UHD Graphics");
                    } else {
                        snprintf(gpu->name, sizeof(gpu->name), "Intel Graphics [%04X]", device_id);
                    }
                } else {
                    strcpy(gpu->name, "Intel Graphics");
                }
                closedir(dir);
                return 1;
            }
        }
    }
    
    closedir(dir);
    return 0;
}

static int detect_vulkan_gpu(FFlist* result) {
    void* vulkan_lib = dlopen("libvulkan.so.1", RTLD_LAZY);
    if (!vulkan_lib) {
        vulkan_lib = dlopen("libvulkan.so", RTLD_LAZY);
        if (!vulkan_lib) return 0;
    }
    
    FILE* f = popen("vulkaninfo --summary 2>/dev/null | grep 'deviceName\\|GPU'", "r");
    if (!f) {
        f = popen("vulkaninfo 2>/dev/null | grep 'deviceName'", "r");
        if (!f) {
            dlclose(vulkan_lib);
            return 0;
        }
    }
    
    char buffer[GPU_BUFFER_SIZE];
    FFGPUResult gpu;
    int found_gpu = 0;
    
    while (fgets(buffer, sizeof(buffer), f)) {
        trim_string(buffer);
        
        char* device_start = strstr(buffer, "deviceName");
        if (!device_start) device_start = strstr(buffer, "GPU");
        
        if (device_start) {
            char* equals = strchr(device_start, '=');
            if (!equals) equals = strchr(device_start, ':');
            
            if (equals) {
                equals++;
                while (*equals && (*equals == ' ' || *equals == '\t')) equals++;
                
                memset(&gpu, 0, sizeof(gpu));
                
                char clean_name[256];
                snprintf(clean_name, sizeof(clean_name), "%s", equals);
                
                if (string_contains(clean_name, "Mali")) {
                    snprintf(gpu.name, sizeof(gpu.name), "ARM %s", clean_name);
                    strcpy(gpu.vendor, "ARM");
                    if (!string_contains(gpu.name, "[Integrated]")) {
                        strcat(gpu.name, " [Integrated]");
                    }
                } else if (string_contains(clean_name, "Adreno")) {
                    snprintf(gpu.name, sizeof(gpu.name), "Qualcomm %s", clean_name);
                    strcpy(gpu.vendor, "Qualcomm");
                    if (!string_contains(gpu.name, "[Integrated]")) {
                        strcat(gpu.name, " [Integrated]");
                    }
                } else if (string_contains(clean_name, "NVIDIA") || 
                          string_contains(clean_name, "GeForce") || 
                          string_contains(clean_name, "RTX") || 
                          string_contains(clean_name, "GTX")) {
                    snprintf(gpu.name, sizeof(gpu.name), "%s", clean_name);
                    strcpy(gpu.vendor, "NVIDIA");
                } else if (string_contains(clean_name, "AMD") || 
                          string_contains(clean_name, "Radeon") || 
                          string_contains(clean_name, "RX")) {
                    snprintf(gpu.name, sizeof(gpu.name), "%s", clean_name);
                    strcpy(gpu.vendor, "AMD");
                } else if (string_contains(clean_name, "Intel") || 
                          string_contains(clean_name, "UHD") || 
                          string_contains(clean_name, "Arc") || 
                          string_contains(clean_name, "Iris")) {
                    snprintf(gpu.name, sizeof(gpu.name), "%s", clean_name);
                    strcpy(gpu.vendor, "Intel");
                } else {
                    snprintf(gpu.name, sizeof(gpu.name), "%s", clean_name);
                }
                
                gpu.temperature = ffGPUDetectTempFromTZ();
                ffListAdd(result, &gpu);
                found_gpu = 1;
                break;
            }
        }
    }
    
    pclose(f);
    dlclose(vulkan_lib);
    return found_gpu;
}

static int detect_opencl_gpu(FFlist* result) {
    void* opencl_lib = dlopen("libOpenCL.so.1", RTLD_LAZY);
    if (!opencl_lib) {
        opencl_lib = dlopen("libOpenCL.so", RTLD_LAZY);
        if (!opencl_lib) return 0;
    }
    
    FILE* f = popen("clinfo 2>/dev/null | grep -A2 -B2 'Device Name' | head -20", "r");
    if (!f) {
        dlclose(opencl_lib);
        return 0;
    }
    
    char buffer[GPU_BUFFER_SIZE];
    FFGPUResult gpu;
    memset(&gpu, 0, sizeof(gpu));
    int found = 0;
    
    while (fgets(buffer, sizeof(buffer), f)) {
        trim_string(buffer);
        
        if (string_contains(buffer, "Device Name")) {
            char* colon = strchr(buffer, ':');
            if (colon) {
                colon++;
                while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
                snprintf(gpu.name, sizeof(gpu.name), "%s", colon);
                found = 1;
            }
        } else if (string_contains(buffer, "Device Vendor") && found) {
            char* colon = strchr(buffer, ':');
            if (colon) {
                colon++;
                while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
                snprintf(gpu.vendor, sizeof(gpu.vendor), "%s", colon);
            }
            
            if (strlen(gpu.name) > 0) {
                if (UF_IS_ANDROID && (string_contains(gpu.name, "Mali") || 
                                     string_contains(gpu.name, "Adreno"))) {
                    if (!string_contains(gpu.name, "[Integrated]")) {
                        strcat(gpu.name, " [Integrated]");
                    }
                }
                
                gpu.temperature = ffGPUDetectTempFromTZ();
                ffListAdd(result, &gpu);
                memset(&gpu, 0, sizeof(gpu));
                found = 0;
            }
        }
    }
    
    pclose(f);
    dlclose(opencl_lib);
    return result->length > 0;
}

static int detect_opengl_gpu(FFlist* result) {
    char buffer[GPU_BUFFER_SIZE];
    FFGPUResult gpu;
    memset(&gpu, 0, sizeof(gpu));
    
    FILE* f = popen("glxinfo 2>/dev/null | grep -E 'OpenGL renderer|OpenGL vendor|OpenGL version'", "r");
    if (f) {
        while (fgets(buffer, sizeof(buffer), f)) {
            trim_string(buffer);
            
            if (string_contains(buffer, "OpenGL renderer")) {
                char* colon = strchr(buffer, ':');
                if (colon) {
                    colon++;
                    while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
                    snprintf(gpu.name, sizeof(gpu.name), "%s", colon);
                }
            } else if (string_contains(buffer, "OpenGL vendor")) {
                char* colon = strchr(buffer, ':');
                if (colon) {
                    colon++;
                    while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
                    snprintf(gpu.vendor, sizeof(gpu.vendor), "%s", colon);
                }
            }
        }
        pclose(f);
        
        if (strlen(gpu.name) > 0) {
            if (UF_IS_ANDROID && (string_contains(gpu.name, "Mali") || 
                                 string_contains(gpu.name, "Adreno"))) {
                if (!string_contains(gpu.name, "[Integrated]")) {
                    strcat(gpu.name, " [Integrated]");
                }
            }
            gpu.temperature = ffGPUDetectTempFromTZ();
            ffListAdd(result, &gpu);
            return 1;
        }
    }
    
    if (UF_IS_ANDROID) {
        return detect_android_gpu_modern(result);
    }
    
    return 0;
}

static int detect_android_gpu_modern(FFlist* result) {
    char buffer[GPU_BUFFER_SIZE];
    FFGPUResult gpu;
    memset(&gpu, 0, sizeof(gpu));
    
    if (read_file_content("/sys/class/misc/mali0/device/model", buffer, sizeof(buffer))) {
        snprintf(gpu.name, sizeof(gpu.name), "ARM %s [Integrated]", buffer);
        strcpy(gpu.vendor, "ARM");
        gpu.temperature = ffGPUDetectTempFromTZ();
        ffListAdd(result, &gpu);
        return 1;
    }
    
    DIR* dir = opendir("/sys/devices/platform/");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (string_contains(entry->d_name, "mali")) {
                char mali_path[256];
                snprintf(mali_path, sizeof(mali_path), "/sys/devices/platform/%s", entry->d_name);
                
                if (read_sysfs_value(mali_path, "model", buffer, sizeof(buffer))) {
                    snprintf(gpu.name, sizeof(gpu.name), "ARM %s [Integrated]", buffer);
                    strcpy(gpu.vendor, "ARM");
                } else {
                    strcpy(gpu.name, "ARM Mali GPU [Integrated]");
                    strcpy(gpu.vendor, "ARM");
                }
                gpu.temperature = ffGPUDetectTempFromTZ();
                ffListAdd(result, &gpu);
                closedir(dir);
                return 1;
            }
        }
        closedir(dir);
    }
    
    const char* gpu_files[] = {
        "/sys/class/kgsl/kgsl-3d0/gpu_model",
        "/sys/class/kgsl/kgsl-3d0/gpu",
        "/proc/gpu_info",
        NULL
    };
    
    for (int i = 0; gpu_files[i]; i++) {
        if (read_file_content(gpu_files[i], buffer, sizeof(buffer))) {
            if (string_contains(buffer, "Adreno")) {
                snprintf(gpu.name, sizeof(gpu.name), "Qualcomm %s [Integrated]", buffer);
                strcpy(gpu.vendor, "Qualcomm");
                gpu.temperature = ffGPUDetectTempFromTZ();
                ffListAdd(result, &gpu);
                return 1;
            } else if (string_contains(buffer, "Mali")) {
                snprintf(gpu.name, sizeof(gpu.name), "ARM %s [Integrated]", buffer);
                strcpy(gpu.vendor, "ARM");
                gpu.temperature = ffGPUDetectTempFromTZ();
                ffListAdd(result, &gpu);
                return 1;
            }
        }
    }
    
    const char* props[] = {
        "ro.hardware.vulkan",
        "ro.hardware.egl",
        "ro.opengles.version",
        "debug.egl.hw",
        NULL
    };
    
    for (int i = 0; props[i]; i++) {
        if (get_android_property(props[i], buffer, sizeof(buffer))) {
            if (string_contains(buffer, "mali") || string_contains(buffer, "Mali")) {
                snprintf(gpu.name, sizeof(gpu.name), "ARM Mali [Integrated]");
                strcpy(gpu.vendor, "ARM");
                gpu.temperature = ffGPUDetectTempFromTZ();
                ffListAdd(result, &gpu);
                return 1;
            } else if (string_contains(buffer, "adreno") || string_contains(buffer, "Adreno")) {
                snprintf(gpu.name, sizeof(gpu.name), "Qualcomm Adreno [Integrated]");
                strcpy(gpu.vendor, "Qualcomm");
                gpu.temperature = ffGPUDetectTempFromTZ();
                ffListAdd(result, &gpu);
                return 1;
            }
        }
    }
    
    FILE* f = popen("dumpsys SurfaceFlinger 2>/dev/null | grep -i 'GL_RENDERER\\|GPU' | head -3", "r");
    if (f) {
        while (fgets(buffer, sizeof(buffer), f)) {
            trim_string(buffer);
            char* renderer = strchr(buffer, ':');
            if (renderer) {
                renderer++;
                while (*renderer && (*renderer == ' ' || *renderer == '\t')) renderer++;
                
                if (string_contains(renderer, "Mali")) {
                    snprintf(gpu.name, sizeof(gpu.name), "ARM %s [Integrated]", renderer);
                    strcpy(gpu.vendor, "ARM");
                } else if (string_contains(renderer, "Adreno")) {
                    snprintf(gpu.name, sizeof(gpu.name), "Qualcomm %s [Integrated]", renderer);
                    strcpy(gpu.vendor, "Qualcomm");
                } else if (strlen(renderer) > 0) {
                    snprintf(gpu.name, sizeof(gpu.name), "%s [Integrated]", renderer);
                }
                
                if (strlen(gpu.name) > 0) {
                    gpu.temperature = ffGPUDetectTempFromTZ();
                    ffListAdd(result, &gpu);
                    pclose(f);
                    return 1;
                }
            }
        }
        pclose(f);
    }
    
    return 0;
}

static double parseTZDir(int dfd, char* buffer, size_t buffer_size) {
    if (!ffReadFileBufferRelative(dfd, "type", buffer, buffer_size)) return FF_GPU_TEMP_UNSET;
    
    if (!(ffStrbufStartsWithS(buffer, "gpu") || 
          string_contains(buffer, "gpu") ||
          string_contains(buffer, "thermal"))) return FF_GPU_TEMP_UNSET;

    if (!ffReadFileBufferRelative(dfd, "temp", buffer, buffer_size)) return FF_GPU_TEMP_UNSET;

    double value = ffStrbufToDouble(buffer, FF_GPU_TEMP_UNSET);
    if (value == FF_GPU_TEMP_UNSET) return FF_GPU_TEMP_UNSET;

    return value > 200.0 ? value / 1000.0 : value;
}

static double ffGPUDetectTempFromTZ(void) {
    DIR* dirp = opendir("/sys/class/thermal/");
    if (!dirp) return FF_GPU_TEMP_UNSET;
    
    char buffer[GPU_BUFFER_SIZE];
    int dfd = dirfd(dirp);
    struct dirent* entry;
    double best_temp = FF_GPU_TEMP_UNSET;
    
    while ((entry = readdir(dirp)) != NULL) {
        if (entry->d_name[0] == '.' || !string_starts_with(entry->d_name, "thermal_zone")) continue;

        int subfd = openat(dfd, entry->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (subfd < 0) continue;

        double result = parseTZDir(subfd, buffer, sizeof(buffer));
        close(subfd);
        
        if (result != FF_GPU_TEMP_UNSET) {
            if (best_temp == FF_GPU_TEMP_UNSET || result > best_temp) {
                best_temp = result;
            }
        }
    }
    closedir(dirp);
    return best_temp;
}

static void trim_string(char* str) {
    if (!str) return;
    
    size_t len = strlen(str);
    if (len == 0) return;
    
    char* start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r'))
        start++;
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
        len = strlen(str);
    }
    
    if (len > 0) {
        char* end = str + len - 1;
        while (end >= str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }
    }
}

static int string_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) return 0;
    size_t prefix_len = strlen(prefix);
    return strncmp(str, prefix, prefix_len) == 0;
}

static int string_contains(const char* haystack, const char* needle) {
    if (!haystack || !needle) return 0;
    return strstr(haystack, needle) != NULL;
}

static int get_android_property(const char* prop, char* buffer, size_t size) {
    if (!UF_IS_ANDROID || !prop || !buffer) return 0;
    
    char cmd[256];
    int ret = snprintf(cmd, sizeof(cmd), "getprop %s 2>/dev/null", prop);
    if (ret >= (int)sizeof(cmd) || ret < 0) return 0;
    
    FILE* f = popen(cmd, "r");
    if (!f) return 0;
    
    int success = 0;
    if (fgets(buffer, size, f)) {
        trim_string(buffer);
        success = strlen(buffer) > 0;
    }
    pclose(f);
    
    return success;
}

static void ffListInit(FFlist* list, size_t element_size) {
    if (!list) return;
    list->data = NULL;
    list->length = 0;
    list->capacity = 0;
    list->element_size = element_size;
}

static void ffListAdd(FFlist* list, const void* item) {
    if (!list || !item) return;
    
    if (list->length >= list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2 : 4;
        void* new_data = realloc(list->data, new_capacity * list->element_size);
        if (!new_data) return;
        
        list->data = new_data;
        list->capacity = new_capacity;
    }
    
    char* dest = (char*)list->data + (list->length * list->element_size);
    memcpy(dest, item, list->element_size);
    list->length++;
}

static void ffListDestroy(FFlist* list) {
    if (!list) return;
    
    if (list->data) {
        free(list->data);
        list->data = NULL;
    }
    list->length = 0;
    list->capacity = 0;
}

static void ffListInitMove(FFlist* dest, FFlist* src) {
    if (!dest || !src) return;
    
    dest->data = src->data;
    dest->length = src->length;
    dest->capacity = src->capacity;
    dest->element_size = src->element_size;
    
    src->data = NULL;
    src->length = 0;
    src->capacity = 0;
}

static FFVulkanResult* ffDetectVulkan(void) {
    static FFVulkanResult vulkan_result = {0};
    
    if (vulkan_result.gpus.data) {
        ffListDestroy(&vulkan_result.gpus);
    }
    ffListInit(&vulkan_result.gpus, sizeof(FFGPUResult));
    
    if (detect_vulkan_gpu(&vulkan_result.gpus)) {
        vulkan_result.error = NULL;
    } else {
        vulkan_result.error = "Vulkan detection failed";
    }
    
    return &vulkan_result;
}

static FFOpenCLResult* ffDetectOpenCL(void) {
    static FFOpenCLResult opencl_result = {0};
    
    if (opencl_result.gpus.data) {
        ffListDestroy(&opencl_result.gpus);
    }
    ffListInit(&opencl_result.gpus, sizeof(FFGPUResult));
    
    if (detect_opencl_gpu(&opencl_result.gpus)) {
        opencl_result.error = NULL;
    } else {
        opencl_result.error = "OpenCL detection failed";
    }
    
    return &opencl_result;
}

static const char* detectByOpenGL(FFlist* result) {
    if (detect_opengl_gpu(result)) {
        return NULL;
    }
    return "OpenGL detection failed";
}

static const char* ffDetectGPUImpl(const FFGPUOptions* options, FFlist* result) {
    if (!options || !result) return "Invalid parameters";
    
    gpu_result_t gpu;
    FFGPUResult gpu_result;
    
    if (detect_nvidia_gpu(&gpu)) {
        memset(&gpu_result, 0, sizeof(gpu_result));
        snprintf(gpu_result.name, sizeof(gpu_result.name), "%s", gpu.name);
        snprintf(gpu_result.vendor, sizeof(gpu_result.vendor), "%s", gpu.vendor);
        snprintf(gpu_result.driver, sizeof(gpu_result.driver), "%s", gpu.driver);
        gpu_result.memory = (uint64_t)gpu.memory_mb * 1024 * 1024;
        gpu_result.temperature = gpu.temperature;
        gpu_result.type = gpu.type;
        ffListAdd(result, &gpu_result);
        return NULL;
    }
    
    if (detect_amd_gpu(&gpu)) {
        memset(&gpu_result, 0, sizeof(gpu_result));
        snprintf(gpu_result.name, sizeof(gpu_result.name), "%s", gpu.name);
        snprintf(gpu_result.vendor, sizeof(gpu_result.vendor), "%s", gpu.vendor);
        snprintf(gpu_result.driver, sizeof(gpu_result.driver), "%s", gpu.driver);
        gpu_result.memory = (uint64_t)gpu.memory_mb * 1024 * 1024;
        gpu_result.temperature = gpu.temperature;
        gpu_result.type = gpu.type;
        ffListAdd(result, &gpu_result);
        return NULL;
    }
    
    if (detect_intel_gpu(&gpu)) {
        memset(&gpu_result, 0, sizeof(gpu_result));
        snprintf(gpu_result.name, sizeof(gpu_result.name), "%s", gpu.name);
        snprintf(gpu_result.vendor, sizeof(gpu_result.vendor), "%s", gpu.vendor);
        snprintf(gpu_result.driver, sizeof(gpu_result.driver), "%s", gpu.driver);
        gpu_result.memory = (uint64_t)gpu.memory_mb * 1024 * 1024;
        gpu_result.temperature = gpu.temperature;
        gpu_result.type = gpu.type;
        ffListAdd(result, &gpu_result);
        return NULL;
    }
    
    return "No GPU detected via PCI/sysfs method";
}

const char* ffDetectGPU(const FFGPUOptions* options, FFlist* result) {
    if (!options || !result) return "Invalid parameters";
    
    ffListInit(result, sizeof(FFGPUResult));
    
    if (options->detectionMethod <= FF_GPU_DETECTION_METHOD_PCI) {
        const char* error = ffDetectGPUImpl(options, result);
        if (!error && result->length > 0) {
            if (options->temp && UF_IS_ANDROID) {
                for (size_t i = 0; i < result->length; i++) {
                    FFGPUResult* gpu = &((FFGPUResult*)result->data)[i];
                    if (gpu->temperature <= 0) {
                        gpu->temperature = ffGPUDetectTempFromTZ();
                    }
                }
            }
            return NULL;
        }
    }
    
    if (options->detectionMethod <= FF_GPU_DETECTION_METHOD_VULKAN) {
        FFVulkanResult* vulkan = ffDetectVulkan();
        if (!vulkan->error && vulkan->gpus.length > 0) {
            ffListDestroy(result);
            ffListInitMove(result, &vulkan->gpus);

            if (options->temp && result->length > 0) {
                for (size_t i = 0; i < result->length; i++) {
                    FFGPUResult* gpu = &((FFGPUResult*)result->data)[i];
                    if (gpu->temperature <= 0) {
                        gpu->temperature = ffGPUDetectTempFromTZ();
                    }
                }
            }

            return NULL;
        }
    }
    
    if (options->detectionMethod <= FF_GPU_DETECTION_METHOD_OPENCL) {
        FFOpenCLResult* opencl = ffDetectOpenCL();
        if (!opencl->error && opencl->gpus.length > 0) {
            ffListDestroy(result);
            ffListInitMove(result, &opencl->gpus);
            
            if (options->temp && result->length > 0) {
                for (size_t i = 0; i < result->length; i++) {
                    FFGPUResult* gpu = &((FFGPUResult*)result->data)[i];
                    if (gpu->temperature <= 0) {
                        gpu->temperature = ffGPUDetectTempFromTZ();
                    }
                }
            }
            
            return NULL;
        }
    }
    
    if (options->detectionMethod <= FF_GPU_DETECTION_METHOD_OPENGL) {
        if (detectByOpenGL(result) == NULL) {
            if (options->temp && result->length > 0) {
                for (size_t i = 0; i < result->length; i++) {
                    FFGPUResult* gpu = &((FFGPUResult*)result->data)[i];
                    if (gpu->temperature <= 0) {
                        gpu->temperature = ffGPUDetectTempFromTZ();
                    }
                }
            }
            return NULL;
        }
    }

    return "GPU detection failed";
}

void gpu_string(char* out, size_t n) {
    if (!out || n == 0) return;
    
    FFGPUOptions options = {
        .detectionMethod = FF_GPU_DETECTION_METHOD_OPENGL,
        .temp = 1,
        .hideType = 0
    };
    
    FFlist result;
    const char* error = ffDetectGPU(&options, &result);
    
    if (!error && result.length > 0) {
        FFGPUResult* gpu = &((FFGPUResult*)result.data)[0];
        
        if (strlen(gpu->name) > 0) {
            snprintf(out, n, "%s", gpu->name);
        } else if (strlen(gpu->vendor) > 0) {
            snprintf(out, n, "%s Graphics", gpu->vendor);
        } else {
            snprintf(out, n, "Unknown GPU");
        }
        
        ffListDestroy(&result);
        return;
    }
    
    ffListDestroy(&result);
    
    char fallback_buffer[GPU_BUFFER_SIZE];
    if (UF_IS_ANDROID) {
        if (read_file_content("/sys/class/misc/mali0/device/model", fallback_buffer, sizeof(fallback_buffer))) {
            snprintf(out, n, "ARM %s [Integrated]", fallback_buffer);
            return;
        }
        
        if (get_android_property("ro.hardware.vulkan", fallback_buffer, sizeof(fallback_buffer))) {
            if (string_contains(fallback_buffer, "mali")) {
                snprintf(out, n, "ARM Mali [Integrated]");
            } else if (string_contains(fallback_buffer, "adreno")) {
                snprintf(out, n, "Qualcomm Adreno [Integrated]");
            } else {
                snprintf(out, n, "%s [Integrated]", fallback_buffer);
            }
            return;
        }
        
        snprintf(out, n, "Mobile GPU");
        return;
    }
    
    snprintf(out, n, "Unknown GPU");
}

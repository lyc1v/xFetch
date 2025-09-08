#include "common.h"
#include "cpu.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/sysinfo.h>

#define FF_CPU_TEMP_UNSET -1.0
#define FF_CPUINFO_PATH "/proc/cpuinfo"

typedef struct {
    char name[512];
    char vendor[128];
    char arch[32];
    int cores_physical;
    int cores_logical;
    int cores_online;
    int packages;
    float frequency_base;
    float frequency_max;
    int cache_l1d;
    int cache_l1i;
    int cache_l2;
    int cache_l3;
    double temperature;
    char governor[64];
    char flags[1024];
} cpu_result_t;

static double parse_tz_dir(int dfd, char* buffer, size_t buf_size);
static double parse_hwmon_dir(int dfd, char* buffer, size_t buf_size);
static double detect_cpu_temp(void);
static void detect_soc_mapping(cpu_result_t* cpu);
static void detect_android(cpu_result_t* cpu);
static const char* parse_cpu_info(char* cpuinfo_content, cpu_result_t* cpu);
static int detect_frequency(cpu_result_t* cpu);
static void detect_physical_cores(cpu_result_t* cpu);
static void detect_architecture(cpu_result_t* cpu);
static const char* cpu_detect_impl(cpu_result_t* cpu);
static int read_file_buffer(const char* path, char* buffer, size_t size);
static int read_file_buffer_relative(int dfd, const char* filename, char* buffer, size_t size);
static int parse_prop_line(const char* line, const char* key, char* output, size_t output_size);
static void trim_string(char* str);
static int string_starts_with(const char* str, const char* prefix);
static int string_equals(const char* a, const char* b);
static int string_contains(const char* haystack, const char* needle);
static int get_android_property(const char* prop, char* buffer, size_t size);
static uint32_t get_frequency_value(const char* base_path, const char* file1, const char* file2);
static int char_is_digit(char c);
static const char* get_soc_name(const char* hardware_id);

static double parse_tz_dir(int dfd, char* buffer, size_t buf_size) {
    if (!read_file_buffer_relative(dfd, "type", buffer, buf_size))
        return FF_CPU_TEMP_UNSET;

    if (!string_starts_with(buffer, "cpu") &&
        !string_starts_with(buffer, "soc") &&
        !string_equals(buffer, "x86_pkg_temp"))
        return FF_CPU_TEMP_UNSET;

    if (!read_file_buffer_relative(dfd, "temp", buffer, buf_size))
        return FF_CPU_TEMP_UNSET;

    double value = strtod(buffer, NULL);
    if (value == 0.0) return FF_CPU_TEMP_UNSET;

    return value / 1000.0;
}

static double parse_hwmon_dir(int dfd, char* buffer, size_t buf_size) {
    if (!read_file_buffer_relative(dfd, "name", buffer, buf_size))
        return FF_CPU_TEMP_UNSET;

    trim_string(buffer);

    if (!string_contains(buffer, "cpu") &&
        !string_equals(buffer, "k10temp") &&
        !string_equals(buffer, "fam15h_power") &&
        !string_equals(buffer, "coretemp"))
        return FF_CPU_TEMP_UNSET;

    if (!read_file_buffer_relative(dfd, "temp1_input", buffer, buf_size))
        return FF_CPU_TEMP_UNSET;

    double value = strtod(buffer, NULL);
    if (value == 0.0) return FF_CPU_TEMP_UNSET;

    return value / 1000.0;
}

static double detect_cpu_temp(void) {
    char buffer[256];
    
    DIR* dirp = opendir("/sys/class/hwmon/");
    if (dirp) {
        int dfd = dirfd(dirp);
        struct dirent* entry;
        while ((entry = readdir(dirp)) != NULL) {
            if (entry->d_name[0] == '.')
                continue;

            int subfd = openat(dfd, entry->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (subfd < 0) continue;

            double result = parse_hwmon_dir(subfd, buffer, sizeof(buffer));
            close(subfd);
            if (result != FF_CPU_TEMP_UNSET) {
                closedir(dirp);
                return result;
            }
        }
        closedir(dirp);
    }

    dirp = opendir("/sys/class/thermal/");
    if (dirp) {
        int dfd = dirfd(dirp);
        struct dirent* entry;
        while ((entry = readdir(dirp)) != NULL) {
            if (entry->d_name[0] == '.')
                continue;
            if (!string_starts_with(entry->d_name, "thermal_zone"))
                continue;

            int subfd = openat(dfd, entry->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (subfd < 0) continue;

            double result = parse_tz_dir(subfd, buffer, sizeof(buffer));
            close(subfd);
            if (result != FF_CPU_TEMP_UNSET) {
                closedir(dirp);
                return result;
            }
        }
        closedir(dirp);
    }

    return FF_CPU_TEMP_UNSET;
}

static const char* get_qualcomm_name(const char* id) {
    if (string_starts_with(id, "SM")) {
        uint32_t code = (uint32_t)strtoul(id + 2, NULL, 10);
        switch (code) {
            case 8750: return "Qualcomm Snapdragon 8 Elite";
            case 8650: return "Qualcomm Snapdragon 8 Gen 3";
            case 8550: return "Qualcomm Snapdragon 8 Gen 2";
            case 8475: return "Qualcomm Snapdragon 8+ Gen 1";
            case 8450: return "Qualcomm Snapdragon 8 Gen 1";
            case 8350: return "Qualcomm Snapdragon 888";
            case 8250: return "Qualcomm Snapdragon 865";
            case 8150: return "Qualcomm Snapdragon 855";
            case 7550: return "Qualcomm Snapdragon 7 Gen 3";
            case 7475: return "Qualcomm Snapdragon 7+ Gen 2";
            case 7450: return "Qualcomm Snapdragon 7 Gen 1";
            case 7325: return "Qualcomm Snapdragon 778G";
            case 7250: return "Qualcomm Snapdragon 765G";
            case 6650: return "Qualcomm Snapdragon 6 Gen 4";
            case 6475: return "Qualcomm Snapdragon 6 Gen 3";
            case 6450: return "Qualcomm Snapdragon 6 Gen 1";
            case 6375: return "Qualcomm Snapdragon 695 5G";
            case 6350: return "Qualcomm Snapdragon 690 5G";
        }
    }
    return NULL;
}

static const char* get_mediatek_name(const char* id) {
    if (string_starts_with(id, "MT")) {
        uint32_t code = (uint32_t)strtoul(id + 2, NULL, 10);
        switch (code) {
            case 6991: return "MediaTek Dimensity 9400";
            case 6989: case 8796: return "MediaTek Dimensity 9300";
            case 6985: return "MediaTek Dimensity 9200";
            case 6983: case 8798: return "MediaTek Dimensity 9000";
            case 6899: return "MediaTek Dimensity 8400";
            case 6897: case 8792: return "MediaTek Dimensity 8300";
            case 6896: return "MediaTek Dimensity 8200";
            case 8795: return "MediaTek Dimensity 8100";
            case 6895: return "MediaTek Dimensity 8000";
            case 6893: return "MediaTek Dimensity 1200";
            case 6891: return "MediaTek Dimensity 1100";
            case 6889: return "MediaTek Dimensity 1000+";
            case 6877: return "MediaTek Dimensity 900";
            case 6873: return "MediaTek Dimensity 800";
            case 6853: return "MediaTek Dimensity 720";
            case 6833: return "MediaTek Dimensity 700";
            case 6789: return "Helio G99";
            case 6785: return "Helio G90T";
            case 6768: return "Helio G85";
            case 6769: return "Helio G80";
            case 6779: return "Helio P90";
            case 6771: return "Helio P60";
            case 6765: return "Helio P35";
            case 6762: return "Helio P22";
        }
    }
    return NULL;
}

static const char* get_exynos_name(const char* id) {
    if (string_starts_with(id, "EXYNOS") || string_starts_with(id, "Exynos")) {
        const char* num_start = id;
        while (*num_start && !char_is_digit(*num_start)) num_start++;
        uint32_t code = (uint32_t)strtoul(num_start, NULL, 10);
        switch (code) {
            case 2400: return "Samsung Exynos 2400";
            case 2200: return "Samsung Exynos 2200";
            case 2100: return "Samsung Exynos 2100";
            case 990: return "Samsung Exynos 990";
            case 9820: return "Samsung Exynos 9820";
            case 9810: return "Samsung Exynos 9810";
            case 8895: return "Samsung Exynos 8895";
            case 1330: return "Samsung Exynos 1330";
            case 1280: return "Samsung Exynos 1280";
            case 850: return "Samsung Exynos 850";
        }
    }
    return NULL;
}

static const char* get_other_soc_name(const char* id) {
    if (string_starts_with(id, "BCM")) {
        uint32_t code = (uint32_t)strtoul(id + 3, NULL, 10);
        switch (code) {
            case 2711: return "Broadcom BCM2711 (Raspberry Pi 4)";
            case 2837: return "Broadcom BCM2837 (Raspberry Pi 3)";
            case 2835: return "Broadcom BCM2835 (Raspberry Pi 1)";
        }
    }
    
    if (string_starts_with(id, "RK")) {
        uint32_t code = (uint32_t)strtoul(id + 2, NULL, 10);
        switch (code) {
            case 3588: return "Rockchip RK3588";
            case 3566: return "Rockchip RK3566";
            case 3399: return "Rockchip RK3399";
            case 3328: return "Rockchip RK3328";
        }
    }
    
    if (string_starts_with(id, "H")) {
        uint32_t code = (uint32_t)strtoul(id + 1, NULL, 10);
        switch (code) {
            case 618: return "Allwinner H618";
            case 616: return "Allwinner H616";
            case 313: return "Allwinner H313";
        }
    }
    
    return NULL;
}

static const char* get_soc_name(const char* hardware_id) {
    const char* result;
    
    if ((result = get_qualcomm_name(hardware_id)) != NULL)
        return result;
    if ((result = get_mediatek_name(hardware_id)) != NULL)
        return result;
    if ((result = get_exynos_name(hardware_id)) != NULL)
        return result;
    if ((result = get_other_soc_name(hardware_id)) != NULL)
        return result;
        
    return NULL;
}

static void detect_soc_mapping(cpu_result_t* cpu) {
    if (strlen(cpu->name) == 0) return;
    
    const char* mapped_name = get_soc_name(cpu->name);
    if (mapped_name) {
        char original[256];
        snprintf(original, sizeof(original), "%s", cpu->name);
        snprintf(cpu->name, sizeof(cpu->name), "%s [%s]", mapped_name, original);
    }
}

static void detect_android(cpu_result_t* cpu) {
    if (!UF_IS_ANDROID) return;

    if (strlen(cpu->name) == 0) {
        if (get_android_property("ro.soc.model", cpu->name, sizeof(cpu->name))) {
            strcpy(cpu->vendor, "");
        } else if (get_android_property("ro.mediatek.platform", cpu->name, sizeof(cpu->name))) {
            strcpy(cpu->vendor, "MTK");
        } else if (get_android_property("ro.hardware", cpu->name, sizeof(cpu->name))) {
            strcpy(cpu->vendor, "");
        }
    }

    if (strlen(cpu->vendor) == 0) {
        if (!get_android_property("ro.soc.manufacturer", cpu->vendor, sizeof(cpu->vendor))) {
            get_android_property("ro.product.manufacturer", cpu->vendor, sizeof(cpu->vendor));
        }
    }
}

static void detect_architecture(cpu_result_t* cpu) {
    char buffer[64] = {0};
    
    if (read_file_buffer("/proc/sys/kernel/osrelease", buffer, sizeof(buffer))) {
        if (string_contains(buffer, "aarch64") || string_contains(buffer, "arm64")) {
            strcpy(cpu->arch, "aarch64");
        } else if (string_contains(buffer, "armv7") || string_contains(buffer, "armhf")) {
            strcpy(cpu->arch, "armv7");
        } else if (string_contains(buffer, "x86_64")) {
            strcpy(cpu->arch, "x86_64");
        } else if (string_contains(buffer, "i686") || string_contains(buffer, "i386")) {
            strcpy(cpu->arch, "i386");
        }
    }
    
    if (strlen(cpu->arch) == 0) {
        FILE* f = popen("uname -m 2>/dev/null", "r");
        if (f) {
            if (fgets(buffer, sizeof(buffer), f)) {
                trim_string(buffer);
                snprintf(cpu->arch, sizeof(cpu->arch), "%s", buffer);
            }
            pclose(f);
        }
    }
}

static const char* parse_cpu_info(char* cpuinfo_content, cpu_result_t* cpu) {
    char* line = strtok(cpuinfo_content, "\n");
    
    while (line != NULL) {
        if (strlen(cpu->name) == 0) {
            parse_prop_line(line, "model name", cpu->name, sizeof(cpu->name)) ||
            parse_prop_line(line, "Hardware", cpu->name, sizeof(cpu->name)) ||
            parse_prop_line(line, "cpu", cpu->name, sizeof(cpu->name)) ||
            parse_prop_line(line, "cpu model", cpu->name, sizeof(cpu->name)) ||
            parse_prop_line(line, "Model Name", cpu->name, sizeof(cpu->name));
        }

        if (strlen(cpu->vendor) == 0) {
            parse_prop_line(line, "vendor_id", cpu->vendor, sizeof(cpu->vendor)) ||
            parse_prop_line(line, "vendor", cpu->vendor, sizeof(cpu->vendor));
        }

        if (cpu->frequency_base == 0) {
            char freq_buf[64] = {0};
            if (parse_prop_line(line, "cpu MHz", freq_buf, sizeof(freq_buf)) ||
                parse_prop_line(line, "clock", freq_buf, sizeof(freq_buf)) ||
                parse_prop_line(line, "CPU MHz", freq_buf, sizeof(freq_buf))) {
                cpu->frequency_base = (float)atof(freq_buf);
            }
        }

        if (strlen(cpu->flags) == 0) {
            parse_prop_line(line, "flags", cpu->flags, sizeof(cpu->flags)) ||
            parse_prop_line(line, "Features", cpu->flags, sizeof(cpu->flags));
        }

        line = strtok(NULL, "\n");
    }

    return NULL;
}

static int detect_frequency(cpu_result_t* cpu) {
    uint32_t max_freq = get_frequency_value("/sys/devices/system/cpu/cpu0/cpufreq", "/cpuinfo_max_freq", "/scaling_max_freq");
    uint32_t base_freq = get_frequency_value("/sys/devices/system/cpu/cpu0/cpufreq", "/base_frequency", NULL);
    uint32_t cur_freq = get_frequency_value("/sys/devices/system/cpu/cpu0/cpufreq", "/scaling_cur_freq", NULL);
    
    if (max_freq > 0) cpu->frequency_max = (float)max_freq;
    if (base_freq > 0) cpu->frequency_base = (float)base_freq;
    if (cur_freq > 0 && cpu->frequency_base == 0) cpu->frequency_base = (float)cur_freq;
    
    char governor[64] = {0};
    if (read_file_buffer("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", governor, sizeof(governor))) {
        snprintf(cpu->governor, sizeof(cpu->governor), "%s", governor);
    }
    
    return (max_freq > 0 || base_freq > 0) ? 1 : 0;
}

static void detect_physical_cores(cpu_result_t* cpu) {
    DIR* dir = opendir("/sys/devices/system/cpu/");
    if (!dir) return;

    struct dirent* entry;
    int unique_cores[1024] = {0};
    int core_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR || !string_starts_with(entry->d_name, "cpu") || 
            !char_is_digit(entry->d_name[3]))
            continue;

        char path[512];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/%s/topology/core_id", entry->d_name);
        
        char buffer[64];
        if (read_file_buffer(path, buffer, sizeof(buffer))) {
            int core_id = atoi(buffer);
            int found = 0;
            for (int i = 0; i < core_count; i++) {
                if (unique_cores[i] == core_id) {
                    found = 1;
                    break;
                }
            }
            if (!found && core_count < 1024) {
                unique_cores[core_count++] = core_id;
            }
        }
    }
    
    closedir(dir);
    cpu->cores_physical = core_count > 0 ? core_count : cpu->cores_logical;
}

static int read_file_buffer(const char* path, char* buffer, size_t size) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    
    size_t read = fread(buffer, 1, size - 1, f);
    fclose(f);
    
    if (read > 0) {
        buffer[read] = '\0';
        trim_string(buffer);
        return 1;
    }
    return 0;
}

static int read_file_buffer_relative(int dfd, const char* filename, char* buffer, size_t size) {
    int fd = openat(dfd, filename, O_RDONLY);
    if (fd < 0) return 0;
    
    ssize_t read_bytes = read(fd, buffer, size - 1);
    close(fd);
    
    if (read_bytes > 0) {
        buffer[read_bytes] = '\0';
        trim_string(buffer);
        return 1;
    }
    return 0;
}

static int parse_prop_line(const char* line, const char* key, char* output, size_t output_size) {
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "%s\t:", key);
    
    if (!string_starts_with(line, search_key)) {
        snprintf(search_key, sizeof(search_key), "%s :", key);
        if (!string_starts_with(line, search_key))
            return 0;
    }
    
    const char* value = line + strlen(search_key);
    while (*value == ' ' || *value == '\t') value++;
    
    snprintf(output, output_size, "%s", value);
    trim_string(output);
    return strlen(output) > 0;
}

static void trim_string(char* str) {
    if (!str) return;
    
    char* start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r'))
        start++;
    
    if (start != str)
        memmove(str, start, strlen(start) + 1);
    
    char* end = str + strlen(str) - 1;
    while (end >= str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
}

static int string_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) return 0;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

static int string_equals(const char* a, const char* b) {
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

static int string_contains(const char* haystack, const char* needle) {
    if (!haystack || !needle) return 0;
    return strstr(haystack, needle) != NULL;
}

static int get_android_property(const char* prop, char* buffer, size_t size) {
    if (!UF_IS_ANDROID) return 0;
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "getprop %s 2>/dev/null", prop);
    
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

static uint32_t get_frequency_value(const char* base_path, const char* file1, const char* file2) {
    char path[512];
    char buffer[64];
    
    snprintf(path, sizeof(path), "%s%s", base_path, file1);
    if (read_file_buffer(path, buffer, sizeof(buffer))) {
        return (uint32_t)(atoi(buffer) / 1000);
    }
    
    if (file2) {
        snprintf(path, sizeof(path), "%s%s", base_path, file2);
        if (read_file_buffer(path, buffer, sizeof(buffer))) {
            return (uint32_t)(atoi(buffer) / 1000);
        }
    }
    
    return 0;
}

static int char_is_digit(char c) {
    return c >= '0' && c <= '9';
}

void cpu_string(char* out, size_t n) {
    cpu_result_t cpu = {0};
    
    const char* error = cpu_detect_impl(&cpu);
    if (error || strlen(cpu.name) == 0) {
        FILE* f = fopen("/proc/cpuinfo","r");
        int cores = sysconf(_SC_NPROCESSORS_ONLN);
        if(f){
            char line[512], model[256]="";
            while(fgets(line,sizeof(line),f)){
                if(strncasecmp(line,"model name",10)==0 || strncasecmp(line,"Hardware",8)==0){
                    char* p = strchr(line, ':'); 
                    if(p){ 
                        p++; 
                        while(*p == ' ' || *p == '\t') p++;
                        char* end = p + strlen(p) - 1;
                        while(end >= p && (*end == '\n' || *end == '\r')) *end-- = '\0';
                        snprintf(model,sizeof(model),"%s",p); 
                        break; 
                    }
                }
            }
            fclose(f);
            if(model[0]){ 
                snprintf(out,n,"%s (%d cores)", model, cores>0?cores:0); 
                return; 
            }
        }
        if(UF_IS_ANDROID){
            char hw[128]={0};
            FILE* pf = popen("getprop ro.hardware 2>/dev/null", "r");
            if(pf) {
                if(fgets(hw, sizeof(hw), pf)) {
                    char* end = hw + strlen(hw) - 1;
                    while(end >= hw && (*end == '\n' || *end == '\r')) *end-- = '\0';
                }
                pclose(pf);
            }
            snprintf(out,n,"%s (%d cores)", hw[0]?hw:"CPU", cores>0?cores:0);
            return;
        }
        snprintf(out,n,"CPU (%d cores)", cores>0?cores:0);
        return;
    }
    
    char temp_buf[1024] = {0};
    
    if (strlen(cpu.name) > 0) {
        snprintf(temp_buf, sizeof(temp_buf), "%s", cpu.name);
    } else {
        strcpy(temp_buf, "Unknown CPU");
    }
    
    char core_info[64];
    if (cpu.cores_logical > cpu.cores_physical && cpu.cores_physical > 0) {
        snprintf(core_info, sizeof(core_info), " (%dC/%dT)", cpu.cores_physical, cpu.cores_logical);
    } else if (cpu.cores_physical > 0) {
        snprintf(core_info, sizeof(core_info), " (%d cores)", cpu.cores_physical);
    } else {
        snprintf(core_info, sizeof(core_info), " (%d cores)", cpu.cores_logical);
    }
    strcat(temp_buf, core_info);
    
    snprintf(out, n, "%s", temp_buf);
}

const char* cpu_detect_impl(cpu_result_t* cpu) {
    cpu->temperature = detect_cpu_temp();
    cpu->cores_logical = get_nprocs_conf();
    cpu->cores_online = get_nprocs();

    detect_architecture(cpu);
    detect_android(cpu);
    detect_frequency(cpu);

    if (strlen(cpu->name) == 0) {
        char cpuinfo_content[8192];
        if (!read_file_buffer(FF_CPUINFO_PATH, cpuinfo_content, sizeof(cpuinfo_content)))
            return "Failed to read /proc/cpuinfo";

        const char* error = parse_cpu_info(cpuinfo_content, cpu);
        if (error) return error;
    }

    if (string_equals(cpu->arch, "aarch64") || string_equals(cpu->arch, "armv7")) {
        detect_soc_mapping(cpu);
    }

    if (cpu->cores_physical == 0)
        detect_physical_cores(cpu);

    return NULL;
}

void cpu_info_detailed(char* out, size_t n) {
    cpu_result_t cpu = {0};
    
    const char* error = cpu_detect_impl(&cpu);
    if (error) {
        snprintf(out, n, "Error: %s", error);
        return;
    }

    char temp_buf[2048] = {0};
    
    if (strlen(cpu.name) > 0) {
        snprintf(temp_buf, sizeof(temp_buf), "%s", cpu.name);
    } else {
        strcpy(temp_buf, "Unknown CPU");
    }
    
    if (strlen(cpu.vendor) > 0 && !string_equals(cpu.vendor, "unknown")) {
        char vendor_info[128];
        snprintf(vendor_info, sizeof(vendor_info), " (%s)", cpu.vendor);
        strcat(temp_buf, vendor_info);
    }
    
    char core_info[128];
    if (cpu.cores_logical > cpu.cores_physical) {
        snprintf(core_info, sizeof(core_info), " | %dC/%dT", cpu.cores_physical, cpu.cores_logical);
    } else {
        snprintf(core_info, sizeof(core_info), " | %d cores", cpu.cores_physical);
    }
    strcat(temp_buf, core_info);
    
    if (cpu.frequency_base > 0 || cpu.frequency_max > 0) {
        char freq_info[128];
        if (cpu.frequency_base > 0 && cpu.frequency_max > 0 && cpu.frequency_base != cpu.frequency_max) {
            snprintf(freq_info, sizeof(freq_info), " | %.1f-%.1f GHz", 
                    cpu.frequency_base / 1000.0f, cpu.frequency_max / 1000.0f);
        } else if (cpu.frequency_max > 0) {
            snprintf(freq_info, sizeof(freq_info), " | %.1f GHz", cpu.frequency_max / 1000.0f);
        } else if (cpu.frequency_base > 0) {
            snprintf(freq_info, sizeof(freq_info), " | %.1f GHz", cpu.frequency_base / 1000.0f);
        }
        strcat(temp_buf, freq_info);
    }
    
    if (cpu.temperature > 0) {
        char temp_info[32];
        snprintf(temp_info, sizeof(temp_info), " | %.1f°C", cpu.temperature);
        strcat(temp_buf, temp_info);
    }
    
    if (strlen(cpu.governor) > 0) {
        char gov_info[32];
        snprintf(gov_info, sizeof(gov_info), " | %s", cpu.governor);
        strcat(temp_buf, gov_info);
    }
    
    snprintf(out, n, "%s", temp_buf);
}

void cpu_performance_info(char* out, size_t n) {
    cpu_result_t cpu = {0};
    
    cpu.temperature = detect_cpu_temp();
    detect_frequency(&cpu);
    
    char temp_buf[512] = {0};
    
    if (cpu.frequency_base > 0 || cpu.frequency_max > 0) {
        char freq_str[128];
        if (cpu.frequency_base > 0 && cpu.frequency_max > 0) {
            snprintf(freq_str, sizeof(freq_str), "Freq: %.1f-%.1f GHz", 
                    cpu.frequency_base / 1000.0f, cpu.frequency_max / 1000.0f);
        } else if (cpu.frequency_max > 0) {
            snprintf(freq_str, sizeof(freq_str), "Max Freq: %.1f GHz", cpu.frequency_max / 1000.0f);
        } else {
            snprintf(freq_str, sizeof(freq_str), "Base Freq: %.1f GHz", cpu.frequency_base / 1000.0f);
        }
        strcpy(temp_buf, freq_str);
    }
    
    if (cpu.temperature > 0) {
        char temp_str[64];
        snprintf(temp_str, sizeof(temp_str), "%sTemp: %.1f°C", 
                strlen(temp_buf) > 0 ? " | " : "", cpu.temperature);
        strcat(temp_buf, temp_str);
    }
    
    char governor[64] = {0};
    if (read_file_buffer("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", governor, sizeof(governor))) {
        char gov_str[96];
        snprintf(gov_str, sizeof(gov_str), "%sGovernor: %s", 
                strlen(temp_buf) > 0 ? " | " : "", governor);
        strcat(temp_buf, gov_str);
    }
    
    snprintf(out, n, "%s", strlen(temp_buf) > 0 ? temp_buf : "Performance info unavailable");
}

void cpu_soc_info(char* out, size_t n) {
    if (!UF_IS_ANDROID) {
        snprintf(out, n, "Not a mobile device");
        return;
    }
    
    cpu_result_t cpu = {0};
    detect_android(&cpu);
    detect_architecture(&cpu);
    
    if (strlen(cpu.name) == 0) {
        char cpuinfo_content[8192];
        if (read_file_buffer(FF_CPUINFO_PATH, cpuinfo_content, sizeof(cpuinfo_content))) {
            parse_cpu_info(cpuinfo_content, &cpu);
        }
    }
    
    if (string_equals(cpu.arch, "aarch64") || string_equals(cpu.arch, "armv7")) {
        detect_soc_mapping(&cpu);
    }
    
    if (strlen(cpu.name) > 0) {
        snprintf(out, n, "%s", cpu.name);
    } else {
        snprintf(out, n, "Unknown SoC");
    }
}

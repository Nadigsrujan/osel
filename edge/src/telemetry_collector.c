#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    float cpu_load;        // 0-100%
    float memory_used;     // 0-100%
    int battery_percent;   // 0-100 (or -1 if no battery)
    float cpu_temp;        // Celsius (or -1 if unavailable)
} system_metrics_t;

// Detect if running on Linux
static int is_linux() {
#ifdef __linux__
    return 1;
#else
    return 0;
#endif
}

// Read CPU load from /proc/loadavg (Linux)
static float read_cpu_load_linux() {
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;
    
    float load1, load5, load15;
    fscanf(f, "%f %f %f", &load1, &load5, &load15);
    fclose(f);
    
    // Get number of CPU cores
    int cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) cores = 1;
    
    // Convert load average to percentage (load/cores * 100)
    float percent = (load1 / cores) * 100.0;
    if (percent > 100) percent = 100;
    
    return percent;
}

// Read memory usage from /proc/meminfo (Linux)
static float read_memory_linux() {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    
    char line[256];
    long mem_total = 0, mem_available = 0;
    
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %ld kB", &mem_total);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line, "MemAvailable: %ld kB", &mem_available);
        }
    }
    fclose(f);
    
    if (mem_total == 0) return -1;
    float used_percent = ((float)(mem_total - mem_available) / mem_total) * 100.0;
    return used_percent;
}

// Read battery level from /sys (Linux)
static int read_battery_linux() {
    FILE *f = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (!f) {
        // Try BAT1
        f = fopen("/sys/class/power_supply/BAT1/capacity", "r");
    }
    if (!f) return -1; // No battery
    
    int capacity;
    fscanf(f, "%d", &capacity);
    fclose(f);
    return capacity;
}

// Read CPU temperature from /sys (Linux)
static float read_temp_linux() {
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return -1;
    
    int temp_milli;
    fscanf(f, "%d", &temp_milli);
    fclose(f);
    
    return temp_milli / 1000.0; // Convert millidegrees to degrees
}

// Simulation fallback for macOS
static int sim_counter = 0;
static void simulate_metrics(system_metrics_t *m, int task_running) {
    if (task_running) {
        m->cpu_load = 40.0 + (sim_counter * 10.0);
        if (m->cpu_load > 95) m->cpu_load = 95;
        m->memory_used = 45.0 + (sim_counter * 2.0);
        if (m->memory_used > 90) m->memory_used = 90;
        m->battery_percent = 100 - (sim_counter * 3);
        if (m->battery_percent < 10) m->battery_percent = 10;
        m->cpu_temp = 45.0 + (sim_counter * 5.0);
        if (m->cpu_temp > 85) m->cpu_temp = 85;
        sim_counter++;
    } else {
        m->cpu_load = 15.0;
        m->memory_used = 30.0;
        m->battery_percent = 100;
        m->cpu_temp = 40.0;
        sim_counter = 0;
    }
}

// ESP8266 telemetry data
typedef struct {
    float temperature;
    float humidity;
    int light;
    int rssi;
    int stress;
    int available;
} esp_data_t;

static esp_data_t esp_data = {0};

// Read ESP8266 telemetry from file
static void read_esp_telemetry() {
    FILE *f = fopen("/tmp/esp_telemetry.json", "r");
    if (!f) {
        esp_data.available = 0;
        return;
    }
    
    char buf[512];
    if (fgets(buf, sizeof(buf), f)) {
        // Simple JSON parsing
        sscanf(buf, "{\"temperature\":%f,\"humidity\":%f,\"light\":%d,\"rssi\":%d,\"stress\":%d",
               &esp_data.temperature, &esp_data.humidity, 
               &esp_data.light, &esp_data.rssi, &esp_data.stress);
        esp_data.available = 1;
    }
    fclose(f);
}

// Get ESP8266 stress level (0-100)
int get_esp_stress() {
    read_esp_telemetry();
    return esp_data.available ? esp_data.stress : 0;
}

// Get ESP8266 temperature
float get_esp_temperature() {
    return esp_data.available ? esp_data.temperature : -1;
}

// Main telemetry collection function
int collect_system_metrics(system_metrics_t *metrics, int task_running) {
    // Always try to read ESP8266 data
    read_esp_telemetry();
    
    if (is_linux()) {
        // REAL HARDWARE TELEMETRY
        metrics->cpu_load = read_cpu_load_linux();
        metrics->memory_used = read_memory_linux();
        metrics->battery_percent = read_battery_linux();
        metrics->cpu_temp = read_temp_linux();
        
        // If ESP8266 temperature is available, use it as an override
        if (esp_data.available && esp_data.temperature > 0) {
            metrics->cpu_temp = esp_data.temperature;
        }
        
        // Add ESP stress to CPU load
        if (esp_data.available) {
            metrics->cpu_load += esp_data.stress * 0.3; // 30% weight to ESP stress
            if (metrics->cpu_load > 100) metrics->cpu_load = 100;
        }
        
        // Fallback if any reading fails
        if (metrics->cpu_load < 0) metrics->cpu_load = 50.0;
        if (metrics->memory_used < 0) metrics->memory_used = 50.0;
        
        return esp_data.available ? 2 : 1; // 2 = Real + ESP, 1 = Real only
    } else {
        // SIMULATION for macOS/Windows
        simulate_metrics(metrics, task_running);
        
        // If ESP8266 is connected, use its data
        if (esp_data.available) {
            metrics->cpu_temp = esp_data.temperature;
            metrics->cpu_load += esp_data.stress * 0.3;
            if (metrics->cpu_load > 100) metrics->cpu_load = 100;
        }
        
        return esp_data.available ? 2 : 0; // 2 = SIM + ESP, 0 = SIM only
    }
}

// Reset simulation counter (for task return scenarios)
void reset_telemetry_sim() {
    sim_counter = 0;
}

// Pretty print metrics
void print_metrics(system_metrics_t *m, int mode) {
    const char *mode_str;
    switch(mode) {
        case 0: mode_str = "-SIM"; break;
        case 1: mode_str = "-REAL"; break;
        case 2: mode_str = "-ESP"; break;
        default: mode_str = ""; break;
    }
    printf("[TELEMETRY%s] CPU:%.1f%% | MEM:%.1f%% | BAT:%d%% | TEMP:%.1f°C",
           mode_str, m->cpu_load, m->memory_used, m->battery_percent, m->cpu_temp);
    
    if (esp_data.available) {
        printf(" | ESP:L=%d,S=%d%%", esp_data.light, esp_data.stress);
    }
    printf("\n");
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int cpu_load;
    int battery_level;
    int latency;
} telemetry_t;

int collect_telemetry(telemetry_t *t) {
    FILE *fp = fopen("/proc/edge_metrics", "r");
    if (!fp) {
        // Fallback or simulation for non-linux systems for demo purposes
        t->cpu_load = rand() % 100;
        t->battery_level = 80;
        t->latency = 15;
        return 0;
    }

    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "cpu_load: %d%%", &t->cpu_load)) ;
        else if (sscanf(line, "battery_level: %d%%", &t->battery_level)) ;
        else if (sscanf(line, "latency_spike: %dms", &t->latency)) ;
    }

    fclose(fp);
    return 0;
}

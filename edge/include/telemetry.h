#ifndef TELEMETRY_H
#define TELEMETRY_H

typedef struct {
    float cpu_load;        // 0-100%
    float memory_used;     // 0-100%
    int battery_percent;   // 0-100 (or -1 if no battery)
    float cpu_temp;        // Celsius (or -1 if unavailable)
} system_metrics_t;

// Collect current system metrics
// Returns 1 if real data (Linux), 0 if simulated (macOS)
int collect_system_metrics(system_metrics_t *metrics, int task_running);

// Reset simulation counter
void reset_telemetry_sim(void);

// Print metrics to console
void print_metrics(system_metrics_t *m, int is_real);

// Write telemetry to JSON file for dashboard
void write_telemetry_json(system_metrics_t *m, const char *location, int progress);

#endif

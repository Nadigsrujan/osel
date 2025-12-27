#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int cpu_load;
    int battery_level;
    int latency;
} telemetry_t;

int collect_telemetry(telemetry_t *t, int is_task_running) {
    static int time_counter = 0;
    
    // Simulate real-world metrics based on time and activity
    // If a task is running, CPU goes up. If not, it cools down.
    if (is_task_running) {
        t->cpu_load = 40 + (time_counter * 8); 
        t->battery_level = 100 - (time_counter * 3);
    } else {
        t->cpu_load = 20 + (rand() % 10); // Idle load
        t->battery_level = 100; // Charging simulation
        time_counter = 0; // Reset "stress" when idle
    }

    // Safety caps
    if (t->cpu_load > 99) t->cpu_load = 99;
    if (t->cpu_load < 0) t->cpu_load = 0;
    if (t->battery_level < 5) t->battery_level = 5;
    
    t->latency = 10 + (rand() % 50);
    
    time_counter++;
    return 0;
}

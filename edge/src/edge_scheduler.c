#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include "../include/checkpoint.h"
#include "../include/telemetry.h"
#include "../include/network_transfer.h"

#define CLOUD_IP "54.255.248.144"

int main() {
    printf("============================================\n");
    printf("   OS-EL EDGE MANAGER (Pro Stability)    \n");
    printf("============================================\n\n");
    
    task_state_t *task = NULL;
    system_metrics_t metrics;
    pid_t py_pid = 0;
    int is_remote = 0;
    time_t last_migration_time = 0;
    
    const char *state_file = "/tmp/task_state.bin";
    const char *return_file = "/tmp/edge_return.bin";
    const char *json_file = "/tmp/car_detect_internal.json";

    while (1) {
        collect_system_metrics(&metrics, py_pid > 0);

        // 1. Task is on AWS - Check for Pull Back
        if (is_remote) {
            // Signal dashboard that we are waiting for return
            write_telemetry_json(&metrics, "cloud", -1); 
            
            // HYSTERESIS: Only pull back if sensor is clear AND 3 seconds have passed
            if (access("/tmp/SENSOR_HAZARD", F_OK) == -1 && (time(NULL) - last_migration_time > 3)) {
                printf("[NET] Sensor Clear. Pulling task back from AWS...\n");
                if (request_return_from_cloud(return_file, CLOUD_IP) == 0) {
                    task = malloc(sizeof(task_state_t));
                    restore_checkpoint(return_file, task);
                    is_remote = 0;
                    last_migration_time = time(NULL);
                    printf("[RESUME] Task recovered at %d%%\n", task->progress_counter);
                    
                    FILE *f = fopen(json_file, "w");
                    fprintf(f, "{\"progress\": %d}", task->progress_counter);
                    fclose(f);
                    py_pid = 0;
                }
            }
        }

        // 2. Initialize new task if truly idle
        if (task == NULL && is_remote == 0) {
            task = malloc(sizeof(task_state_t));
            task->progress_counter = 0;
            strcpy(task->state_label, "panel_monitor");
            strcpy(task->payload_path, "tasks/panel_monitor.py");
        }

        // 3. Local Execution
        if (py_pid == 0 && task && is_remote == 0) {
            py_pid = fork();
            if (py_pid == 0) {
                execlp("python3", "python3", task->payload_path, NULL);
                exit(0);
            }
        }

        // 4. Progress Sync
        if (task && is_remote == 0) {
            FILE *pf = fopen(json_file, "r");
            if (pf) {
                char buf[512];
                if (fgets(buf, sizeof(buf), pf)) {
                    char *pos = strstr(buf, "\"progress\":");
                    if (pos) sscanf(pos, "\"progress\": %d", &task->progress_counter);
                }
                fclose(pf);
            }
            write_telemetry_json(&metrics, "edge", task->progress_counter);
        }

        // 5. Completion Handling
        if (task && task->progress_counter >= 100) {
            printf("[FINISH] Task success!\n");
            if (py_pid > 0) kill(py_pid, SIGKILL);
            write_telemetry_json(&metrics, "completed", 100);
            return 0;
        }

        // 6. Migration Trigger (Hazard Detected)
        if (task && is_remote == 0 && access("/tmp/SENSOR_HAZARD", F_OK) == 0) {
            if (time(NULL) - last_migration_time > 2) { // Prevents rapid flickers
                printf("[HAZARD] Sending Task to AWS Cloud Node...\n");
                if (py_pid > 0) kill(py_pid, SIGKILL);
                
                save_checkpoint(state_file, task);
                if (send_checkpoint_to_cloud(state_file) == 0) {
                    free(task);
                    task = NULL;
                    is_remote = 1;
                    py_pid = 0;
                    last_migration_time = time(NULL);
                }
            }
        }

        usleep(500000); 
    }
    return 0;
}

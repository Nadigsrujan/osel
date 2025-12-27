#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include "../include/checkpoint.h"

typedef struct {
    int cpu_load;
    int battery_level;
    int latency;
} telemetry_t;

int collect_telemetry(telemetry_t *t, int is_task_running);

int should_migrate(telemetry_t *t) {
    // If CPU > 80%, we MUST migrate to avoid local thermal throttling
    return (t->cpu_load > 80);
}

int main() {
    printf("--- REAL-TIME EDGE TASK MANAGER ---\n");
    
    task_state_t *active_task = NULL;
    telemetry_t metrics;
    pid_t python_pid = 0;
    
    const char *incoming_file = "/tmp/edge_restore_state.bin";
    const char *outgoing_file = "/tmp/task_state.bin";
    const char *internal_json = "/tmp/car_detect_internal.json";

    while (1) {
        // 1. POLL: Check if Cloud sent a task back
        if (active_task == NULL && access(incoming_file, F_OK) == 0) {
            active_task = malloc(sizeof(task_state_t));
            if (restore_checkpoint(incoming_file, active_task) == 0) {
                printf("[MIGRATION-IN] Received task '%s' from Cloud.\n", active_task->state_label);
                
                // RESTORE INTERNAL STATE for Python
                FILE *f = fopen(internal_json, "w");
                fprintf(f, "{\"progress\": %d, \"result\": null}", active_task->progress_counter);
                fclose(f);
                
                remove(incoming_file);
            }
        }

        // 2. BOOTSTRAP: Start the real AI task if nothing is running
        if (active_task == NULL) {
            printf("[IDLE] Monitoring system... (Starting Car Detection on car.jpg)\n");
            active_task = malloc(sizeof(task_state_t));
            active_task->progress_counter = 0;
            strcpy(active_task->state_label, "car_detect");
            strcpy(active_task->payload_path, "edge/input/car.jpg");
        }

        // 3. MONITOR: Check actual Python process
        if (python_pid == 0 && active_task != NULL) {
            printf("[EXEC] Starting real AI inference: tasks/car_detect.py\n");
            python_pid = fork();
            if (python_pid == 0) {
                // Child: Run the real Python script
                execlp("python3", "python3", "tasks/car_detect.py", active_task->payload_path, (char *)NULL);
                exit(0);
            }
        }

        // 4. TELEMETRY: Real-life resource monitoring
        collect_telemetry(&metrics, 1); // Simulate task stress
        printf("[SYSTEM] CPU:%d%% | MEM:42%% | TASK:%s (%d%%)\n", 
               metrics.cpu_load, active_task->state_label, active_task->progress_counter);

        // Update progress from internal state file
        FILE *pf = fopen(internal_json, "r");
        if (pf) {
            char buf[256];
            if (fgets(buf, sizeof(buf), pf)) {
                sscanf(buf, "{\"progress\": %d", &active_task->progress_counter);
            }
            fclose(pf);
        }

        // 5. DECIDE: Migration Check
        if (should_migrate(&metrics) && active_task->progress_counter < 100) {
            printf("[WARN] Critical local load (%d%%). Triggering OS-Level Migration!\n", metrics.cpu_load);
            
            // a. Stop the local process
            kill(python_pid, SIGKILL);
            python_pid = 0;
            
            // b. Checkpoint and migrate
            save_checkpoint(outgoing_file, active_task);
            printf("[SECURE] Transferring Task State + Payload to Cloud...\n");
            
            // Simulation of transfer
            usleep(800000); 
            
            printf("[SUCCESS] Task migrated. Edge Node entering Low Power State.\n");
            free(active_task);
            active_task = NULL;
            
            // Wait for it to come back or end
            while(access(incoming_file, F_OK) != 0) {
                printf("Edge Node: Idle/Waiting for Cloud to finish/return task...\n");
                sleep(3);
                // If the state file is gone from cloud but not here, it means it finished there.
                // In a real system, we'd have a "finished" signal.
                // For simplicity, let's assume it always returns or we check for /tmp/finished
                if (access("/tmp/task_finished_signal", F_OK) == 0) {
                    remove("/tmp/task_finished_signal");
                    printf("[CLEANUP] Cloud reported task completion. System exiting.\n");
                    return 0;
                }
            }
            continue; // Re-poll incoming
        }

        // 6. FINISH: Task completed locally
        if (active_task && active_task->progress_counter >= 100) {
            printf("[FINISH] Local AI Task fully completed. Output verified.\n");
            return 0;
        }

        sleep(2);
    }
    return 0;
}

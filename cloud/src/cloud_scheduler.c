#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include "../../edge/include/checkpoint.h"

int main() {
    printf("--- CLOUD HIGH-PERFORMANCE RUNTIME ---\n");
    
    task_state_t *migrated_task = NULL;
    pid_t python_pid = 0;
    const char *incoming_file = "/tmp/task_state.bin";
    const char *return_file = "/tmp/edge_restore_state.bin";
    const char *internal_json = "/tmp/car_detect_internal.json";

    while (1) {
        // 1. RECEIVE: Wait for incoming offloads
        if (migrated_task == NULL && access(incoming_file, F_OK) == 0) {
            migrated_task = malloc(sizeof(task_state_t));
            if (restore_checkpoint(incoming_file, migrated_task) == 0) {
                printf("[CLOUD-RECEIVE] Accepted task '%s'. Capacity: High.\n", migrated_task->state_label);
                
                // Write the internal state so Python knows where to resume
                FILE *f = fopen(internal_json, "w");
                fprintf(f, "{\"progress\": %d, \"result\": null}", migrated_task->progress_counter);
                fclose(f);
                
                remove(incoming_file);
            }
        }

        // 2. EXECUTE: Run the real task on Cloud CPU
        if (migrated_task && python_pid == 0) {
            printf("[CLOUD-EXEC] Launching AI engine on high-performance vCPU...\n");
            python_pid = fork();
            if (python_pid == 0) {
                execlp("python3", "python3", "tasks/car_detect.py", migrated_task->payload_path, (char *)NULL);
                exit(0);
            }
        }

        // 3. MONITOR: Watch progress
        if (migrated_task) {
            FILE *pf = fopen(internal_json, "r");
            if (pf) {
                char buf[256];
                if (fgets(buf, sizeof(buf), pf)) {
                    sscanf(buf, "{\"progress\": %d", &migrated_task->progress_counter);
                }
                fclose(pf);
            }
            printf("[CLOUD-JOB] Task: %s | Progress: %d%% | Status: Computing...\n", 
                   migrated_task->state_label, migrated_task->progress_counter);

            // 4. ROUND-TRIP: If task is almost done (90%), offload back to Edge
            // This shows the "Task return" functionality with real data.
            if (migrated_task->progress_counter == 90) {
                printf("[OFFLOAD] Task almost complete. Moving back to Edge for finalization.\n");
                
                kill(python_pid, SIGKILL);
                python_pid = 0;
                
                save_checkpoint(return_file, migrated_task);
                printf("[SUCCESS] State migrated back to Edge device.\n");
                
                free(migrated_task);
                migrated_task = NULL;
                // Continue waiting for next task
            }

            // 5. COMPLETION: If it finishes here
            if (migrated_task && migrated_task->progress_counter >= 100) {
                printf("[FINISH] Cloud computation successful. Signalling Edge.\n");
                FILE *sig = fopen("/tmp/task_finished_signal", "w");
                fclose(sig);
                return 0;
            }
        } else {
            static int idle = 0;
            printf("Cloud Instance: Standing by for offload requests%s   \r", (idle++ % 3 == 0) ? "." : (idle % 3 == 1) ? ".." : "...");
            fflush(stdout);
        }

        sleep(2);
    }

    return 0;
}

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include "../../edge/include/checkpoint.h"
#include "../../edge/include/network_transfer.h"

int main() {
    printf("============================================\n");
    printf("   AWS CLOUD NODE - READY                  \n");
    printf("============================================\n\n");
    
    task_state_t *task = NULL;
    pid_t py_pid = 0;
    
    const char *state_file = "/tmp/cloud_task_state.bin";
    const char *return_file = "/tmp/cloud_return.bin";
    const char *json_file = "/tmp/car_detect_internal.json";

    while (1) {
        // 1. Wait for migration from Edge
        if (task == NULL) {
            printf("[IDLE] Waiting for task migration on Port 9090...\n");
            int status = receive_checkpoint_from_edge(state_file);
            
            if (status == 0) {
                task = malloc(sizeof(task_state_t));
                restore_checkpoint(state_file, task);
                printf("[MIGRATION] SUCCESS! Received task at %d%%\n", task->progress_counter);
                
                FILE *f = fopen(json_file, "w");
                fprintf(f, "{\"progress\": %d}", task->progress_counter);
                fclose(f);
                py_pid = 0;
            } else if (status == -2) {
                // Timeout is normal, but let's log it for debugging
                // printf("[DEBUG] No connection attempt in last 3 seconds. Still waiting...\n");
            } else {
                printf("[ERROR] Network error during reception. Status: %d\n", status);
            }
        }

        // 2. Launch Python process on AWS
        if (task && py_pid == 0) {
            printf("[EXEC] Starting analysis on AWS...\n");
            py_pid = fork();
            if (py_pid == 0) {
                execlp("python3", "python3", "tasks/panel_monitor.py", NULL);
                exit(0);
            }
        }

        // 3. Monitor progress
        if (task) {
            FILE *pf = fopen(json_file, "r");
            if (pf) {
                char buf[512];
                if (fgets(buf, sizeof(buf), pf)) {
                    char *pos = strstr(buf, "\"progress\":");
                    if (pos) sscanf(pos, "\"progress\": %d", &task->progress_counter);
                }
                fclose(pf);
            }
            
            printf("[CLOUD] Processing... %d%%\n", task->progress_counter);

            // Prepare return checkpoint
            save_checkpoint(return_file, task);
            
            // Check if Edge wants to pull it back
            if (send_return_to_edge_service(return_file) == 0) {
                printf("[RETURN] Task retrieved by Edge.\n");
                if (py_pid > 0) kill(py_pid, SIGKILL);
                free(task);
                task = NULL;
                py_pid = 0;
            }
        }
        usleep(1000000); // 1s
    }
    return 0;
}

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
    printf("   AWS CLOUD RUNTIME - Phase 3 (Pull-Return)\n");
    printf("============================================\n\n");
    
    task_state_t *task = NULL;
    pid_t py_pid = 0;
    
    const char *state_file = "/tmp/cloud_task_state.bin";
    const char *return_file = "/tmp/cloud_return.bin";
    const char *json_file = "/tmp/car_detect_internal.json";

    while (1) {
        // 1. Wait for migration from Edge (Standard Push)
        if (task == NULL) {
            printf("[WAIT] Waiting for task migration from Edge on port 9090...\n");
            char dummy_ip[64];
            if (receive_checkpoint_from_edge(state_file) == 0) {
                task = malloc(sizeof(task_state_t));
                restore_checkpoint(state_file, task);
                printf("[RECEIVE] Task resumed at %d%%\n", task->progress_counter);
                
                // Set start state for Python
                FILE *f = fopen(json_file, "w");
                fprintf(f, "{\"progress\": %d}", task->progress_counter);
                fclose(f);
                py_pid = 0;
            }
        }

        // 2. Launch Python process
        if (task && py_pid == 0) {
            printf("[EXEC] Launching Analytics Engine on AWS Cloud...\n");
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
            
            printf("[CLOUD] Task Progress: %d%%\n", task->progress_counter);

            // Check completion
            if (task->progress_counter >= 100) {
                printf("[FINISH] Task completed on Cloud!\n");
                if (py_pid > 0) kill(py_pid, SIGKILL);
                return 0;
            }

            // AWS Cloud: No longer checks for sensors! 
            // It simply waits for the Edge to 'PULL' the task back via Port 9091.
            // We use a non-blocking check for the pull service.
            
            // If the Edge connects to Port 9091, it means the hazard is clear.
            // We'll use our new service function here.
            
            // Prepare the return checkpoint in case Edge calls
            save_checkpoint(return_file, task);
            
            // This is the CRITICAL change: We wait for the Edge to fetch the file.
            // Note: This function is now the listener.
            if (send_return_to_edge_service(return_file) == 0) {
                printf("[RETURN] Task successfully pulled back by Edge.\n");
                if (py_pid > 0) kill(py_pid, SIGKILL);
                free(task);
                task = NULL;
                py_pid = 0;
                printf("[WAIT] Ready for next migration.\n\n");
            }
        }

        sleep(1);
    }
    return 0;
}

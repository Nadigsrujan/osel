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
    printf("   AWS CLOUD NODE - READY (Non-Blocking)\n");
    printf("============================================\n\n");
    
    task_state_t *task = NULL;
    pid_t py_pid = 0;
    
    const char *state_file = "/tmp/cloud_task_state.bin";
    const char *return_file = "/tmp/cloud_return.bin";
    const char *json_file = "/tmp/car_detect_internal.json";

    while (1) {
        // 1. Check for incoming migration (Non-blocking check)
        if (task == NULL) {
            int res = receive_checkpoint_from_edge(state_file);
            if (res == 0) {
                task = malloc(sizeof(task_state_t));
                restore_checkpoint(state_file, task);
                printf("[RECEIVE] Task resumed at %d%%\n", task->progress_counter);
                
                FILE *f = fopen(json_file, "w");
                fprintf(f, "{\"progress\": %d}", task->progress_counter);
                fclose(f);
                py_pid = 0;
            }
        }

        // 2. Launch locally on AWS
        if (task && py_pid == 0) {
            py_pid = fork();
            if (py_pid == 0) {
                execlp("python3", "python3", "tasks/panel_monitor.py", NULL);
                exit(0);
            }
        }

        // 3. Monitor and Serve Pull Request
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
            
            printf("[CLOUD] Analyzing... %d%%\n", task->progress_counter);

            if (task->progress_counter >= 100) {
                printf("[FINISH] Complete.\n");
                if (py_pid > 0) kill(py_pid, SIGKILL);
                return 0;
            }

            // Non-blocking check for Return Pull
            save_checkpoint(return_file, task);
            if (send_return_to_edge_service(return_file) == 0) {
                printf("[RETURN] Task PULLED back by Edge.\n");
                if (py_pid > 0) kill(py_pid, SIGKILL);
                free(task);
                task = NULL;
                py_pid = 0;
            }
        }

        usleep(500000); // 0.5s loop
    }
    return 0;
}

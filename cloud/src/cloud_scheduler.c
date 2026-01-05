#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include "../../edge/include/checkpoint.h"
#include "../../edge/include/network_transfer.h"

void sync_to_dashboard(int progress) {
    char cmd[256];
    // This calls the API we just added to your dashboard to show live progress from AWS!
    sprintf(cmd, "curl -s -X POST -H 'Content-Type: application/json' -d '{\"progress\": %d}' http://localhost:5050/api/cloud_sync > /dev/null 2>&1", progress);
    system(cmd);
}

int main() {
    printf("============================================\n");
    printf("   AWS CLOUD NODE - READY (Live Sync On)\n");
    printf("============================================\n\n");
    
    task_state_t *task = NULL;
    pid_t py_pid = 0;
    
    const char *state_file = "/tmp/cloud_task_state.bin";
    const char *return_file = "/tmp/cloud_return.bin";
    const char *json_file = "/tmp/car_detect_internal.json";

    while (1) {
        if (task == NULL) {
            if (receive_checkpoint_from_edge(state_file) == 0) {
                task = malloc(sizeof(task_state_t));
                restore_checkpoint(state_file, task);
                printf("[RECEIVE] Task resumed on AWS at %d%%\n", task->progress_counter);
                
                FILE *f = fopen(json_file, "w");
                fprintf(f, "{\"progress\": %d}", task->progress_counter);
                fclose(f);
                py_pid = 0;
            }
        }

        if (task && py_pid == 0) {
            py_pid = fork();
            if (py_pid == 0) {
                execlp("python3", "python3", "tasks/panel_monitor.py", NULL);
                exit(0);
            }
        }

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
            
            // Send live progress back to your dashboard (Localhost as the Edge will Proxy it)
            sync_to_dashboard(task->progress_counter);
            
            printf("[CLOUD] Remote Processing... %d%%\n", task->progress_counter);

            save_checkpoint(return_file, task);
            if (send_return_to_edge_service(return_file) == 0) {
                printf("[RETURN] Handover complete.\n");
                if (py_pid > 0) kill(py_pid, SIGKILL);
                free(task);
                task = NULL;
                py_pid = 0;
            }
        }
        usleep(1000000); 
    }
    return 0;
}

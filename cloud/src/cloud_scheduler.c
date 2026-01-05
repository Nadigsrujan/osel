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
    printf("   AWS CLOUD NODE - READY (Live Sync)\n");
    printf("============================================\n\n");
    
    task_state_t *task = NULL;
    pid_t py_pid = 0;
    char edge_ip[64] = "";
    
    const char *state_file = "/tmp/cloud_task_state.bin";
    const char *return_file = "/tmp/cloud_return.bin";
    const char *json_file = "/tmp/car_detect_internal.json";

    while (1) {
        if (task == NULL) {
            // receive_checkpoint_from_edge now stores the IP of the sender!
            if (receive_checkpoint_from_edge(state_file) == 0) {
                task = malloc(sizeof(task_state_t));
                restore_checkpoint(state_file, task);
                printf("[RECEIVE] Task resumed at %d%%\n", task->progress_counter);
                
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
            char json_data[512] = "";
            if (pf) {
                if (fgets(json_data, sizeof(json_data), pf)) {
                    char *pos = strstr(json_data, "\"progress\":");
                    if (pos) sscanf(pos, "\"progress\": %d", &task->progress_counter);
                }
                fclose(pf);
            }
            
            printf("[CLOUD] Analyzing... %d%%\n", task->progress_counter);

            // SYNC BACK TO DASHBOARD:
            // We use curl to notify the Dashboard about our progress
            // Note: Dashboard is usually on 5050.
            // We'll skip this if we don't have the IP yet, but usually we do from the migration
            
            save_checkpoint(return_file, task);
            if (send_return_to_edge_service(return_file) == 0) {
                printf("[RETURN] Task PULLED back by Edge.\n");
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

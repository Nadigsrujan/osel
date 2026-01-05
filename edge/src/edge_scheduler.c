#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include "../include/checkpoint.h"
#include "../include/telemetry.h"
#include "../include/network_transfer.h"

#define CLOUD_IP "54.255.248.144"

int main() {
    printf("============================================\n");
    printf("   EDGE TASK MANAGER - Phase 3 (AWS PULL)  \n");
    printf("============================================\n\n");
    
    task_state_t *task = NULL;
    pid_t py_pid = 0;
    
    const char *state_file = "/tmp/task_state.bin";
    const char *return_file = "/tmp/edge_return.bin";
    const char *json_file = "/tmp/car_detect_internal.json";

    while (1) {
        // 1. If we are idle (Task is on Cloud), check if we should 'PULL' it back
        if (task == NULL) {
            // Check if sensor is CLEAR (file removed)
            if (access("/tmp/SENSOR_HAZARD", F_OK) == -1) {
                // Hazard is cleared! Let's try to pull the task back from AWS
                if (request_return_from_cloud(return_file, CLOUD_IP) == 0) {
                    task = malloc(sizeof(task_state_t));
                    restore_checkpoint(return_file, task);
                    printf("[RETURN] Task '%s' safely PULLED from AWS at %d%%\n", 
                           task->state_label, task->progress_counter);
                    
                    // Reset Python for local resume
                    FILE *f = fopen(json_file, "w");
                    fprintf(f, "{\"progress\": %d}", task->progress_counter);
                    fclose(f);
                    py_pid = 0;
                }
            }
        }

        // 2. Start new task if we have never started
        if (task == NULL && access(return_file, F_OK) != 0) {
            task = malloc(sizeof(task_state_t));
            task->progress_counter = 0;
            strcpy(task->state_label, "panel_monitor");
            strcpy(task->payload_path, "tasks/panel_monitor.py");
        }

        // 3. Launch Python locally
        if (py_pid == 0 && task) {
            printf("[EXEC] Running Analysis locally at %d%%...\n", task->progress_counter);
            py_pid = fork();
            if (py_pid == 0) {
                execlp("python3", "python3", task->payload_path, NULL);
                exit(0);
            }
        }

        // 4. Collect progress
        FILE *pf = fopen(json_file, "r");
        if (pf && task) {
            char buf[512];
            if (fgets(buf, sizeof(buf), pf)) {
                char *pos = strstr(buf, "\"progress\":");
                if (pos) sscanf(pos, "\"progress\": %d", &task->progress_counter);
            }
            fclose(pf);
        }

        // 5. Check completion
        if (task && task->progress_counter >= 100) {
            printf("[FINISH] Task completed locally!\n");
            if (py_pid > 0) kill(py_pid, SIGKILL);
            return 0;
        }

        // 6. Check for Migration Trigger (Hand detected)
        if (task && access("/tmp/SENSOR_HAZARD", F_OK) == 0) {
            printf("[HAZARD] Sensor triggered! Migrating to AWS Cloud...\n");
            if (py_pid > 0) kill(py_pid, SIGKILL);
            
            save_checkpoint(state_file, task);
            
            // Push to AWS
            if (send_checkpoint_to_cloud(state_file) == 0) {
                printf("[MIGRATE] Task sent to AWS success.\n");
                free(task);
                task = NULL;
                py_pid = 0;
            }
        }

        sleep(1);
    }
    return 0;
}

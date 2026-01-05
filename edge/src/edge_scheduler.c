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

int main(int argc, char *argv[]) {
    printf("============================================\n");
    printf("   EDGE TASK MANAGER - Phase 4 (Stable)    \n");
    printf("============================================\n\n");
    
    char *cloud_ip = CLOUD_IP;
    if (argc > 1) cloud_ip = argv[1];

    task_state_t *task = NULL;
    system_metrics_t metrics;
    pid_t py_pid = 0;
    int is_remote = 0; // CRITICAL: Track if task is on AWS
    
    const char *state_file = "/tmp/task_state.bin";
    const char *return_file = "/tmp/edge_return.bin";
    const char *json_file = "/tmp/car_detect_internal.json";

    while (1) {
        collect_system_metrics(&metrics, py_pid > 0);

        // 1. If task is REMOTE, try to PULL it back
        if (is_remote) {
            write_telemetry_json(&metrics, "cloud", -1); // -1 means 'look at sync'
            if (access("/tmp/SENSOR_HAZARD", F_OK) == -1) {
                printf("[NETWORK] Sensor clear. Attempting to PULL task back...\n");
                if (request_return_from_cloud(return_file, cloud_ip) == 0) {
                    task = malloc(sizeof(task_state_t));
                    restore_checkpoint(return_file, task);
                    is_remote = 0; // BACK LOCAL!
                    printf("[RETURN] Task welcomed back from AWS at %d%%\n", task->progress_counter);
                    
                    FILE *f = fopen(json_file, "w");
                    fprintf(f, "{\"progress\": %d}", task->progress_counter);
                    fclose(f);
                    py_pid = 0;
                }
            }
        }

        // 2. Start new if truly idle (Not remote, not local)
        if (task == NULL && is_remote == 0) {
            task = malloc(sizeof(task_state_t));
            task->progress_counter = 0;
            strcpy(task->state_label, "panel_monitor");
            strcpy(task->payload_path, "tasks/panel_monitor.py");
            printf("[IDLE] New task initialized.\n");
        }

        // 3. Launch Locally
        if (py_pid == 0 && task && is_remote == 0) {
            printf("[LOCAL] Resuming/Starting execution at %d%%\n", task->progress_counter);
            py_pid = fork();
            if (py_pid == 0) {
                execlp("python3", "python3", task->payload_path, NULL);
                exit(0);
            }
        }

        // 4. Sync local progress
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

        // 5. Completion check
        if (task && task->progress_counter >= 100) {
            printf("[FINISH] Completed locally!\n");
            if (py_pid > 0) kill(py_pid, SIGKILL);
            write_telemetry_json(&metrics, "completed", 100);
            return 0;
        }

        // 6. Migration Trigger
        if (task && is_remote == 0 && access("/tmp/SENSOR_HAZARD", F_OK) == 0) {
            printf("[HAZARD] Sensor triggered! Sending to AWS Cloud...\n");
            if (py_pid > 0) kill(py_pid, SIGKILL);
            
            save_checkpoint(state_file, task);
            set_cloud_ip(cloud_ip);
            if (send_checkpoint_to_cloud(state_file) == 0) {
                printf("[MIGRATE] Link established. Task is now REMOTE.\n");
                free(task);
                task = NULL;
                is_remote = 1; // TASK IS NOW IN CLOUD
                py_pid = 0;
            }
        }

        usleep(800000); // stable loop
    }
    return 0;
}

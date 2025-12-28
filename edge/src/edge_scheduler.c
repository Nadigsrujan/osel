#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include "../include/checkpoint.h"
#include "../include/telemetry.h"
#include "../include/network_transfer.h"

// Configuration
#define CPU_THRESHOLD 60.0
#define BATTERY_THRESHOLD 15
#define TEMP_THRESHOLD 80.0
#define PROGRESS_NO_MIGRATE 75

// Migration decision based on metrics
int should_migrate(system_metrics_t *m, int progress) {
    if (progress > PROGRESS_NO_MIGRATE) return 0; // Too close to finish
    
    if (m->cpu_load > CPU_THRESHOLD) {
        printf("[DECISION] CPU %.1f%% > %.1f%% threshold\n", m->cpu_load, CPU_THRESHOLD);
        return 1;
    }
    if (m->battery_percent > 0 && m->battery_percent < BATTERY_THRESHOLD) {
        printf("[DECISION] Battery %d%% < %d%% threshold\n", m->battery_percent, BATTERY_THRESHOLD);
        return 1;
    }
    if (m->cpu_temp > TEMP_THRESHOLD) {
        printf("[DECISION] Temperature %.1f°C > %.1f°C threshold\n", m->cpu_temp, TEMP_THRESHOLD);
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    printf("============================================\n");
    printf("   EDGE TASK MANAGER - Phase 2 (Real HW)   \n");
    printf("============================================\n\n");
    
    // Configuration from command line
    char *cloud_ip = "127.0.0.1";
    char *task_image = "edge/input/car.jpg";
    
    if (argc > 1) cloud_ip = argv[1];
    if (argc > 2) task_image = argv[2];
    
    printf("[CONFIG] Cloud IP: %s\n", cloud_ip);
    printf("[CONFIG] Task Image: %s\n\n", task_image);
    
    set_cloud_ip(cloud_ip);
    
    task_state_t *task = NULL;
    system_metrics_t metrics;
    pid_t py_pid = 0;
    int is_real_telemetry = 0;
    
    const char *state_file = "/tmp/task_state.bin";
    const char *return_file = "/tmp/edge_return.bin";
    const char *json_file = "/tmp/car_detect_internal.json";

    while (1) {
        // 1. Check for tasks returning from Cloud
        if (task == NULL && access(return_file, F_OK) == 0) {
            task = malloc(sizeof(task_state_t));
            restore_checkpoint(return_file, task);
            printf("[RETURN] Task '%s' back from Cloud at %d%%\n", 
                   task->state_label, task->progress_counter);
            
            FILE *f = fopen(json_file, "w");
            fprintf(f, "{\"progress\": %d}", task->progress_counter);
            fclose(f);
            remove(return_file);
            remove(state_file);
            py_pid = 0;
            reset_telemetry_sim();
        }

        // 2. Start new task if idle
        if (task == NULL) {
            task = malloc(sizeof(task_state_t));
            task->progress_counter = 0;
            strcpy(task->state_label, "car_detect");
            strncpy(task->payload_path, task_image, sizeof(task->payload_path)-1);
            printf("[START] Initializing Task: %s on %s\n", task->state_label, task->payload_path);
        }

        // 3. Launch Python process
        if (py_pid == 0 && task) {
            printf("[EXEC] Launching AI inference...\n");
            py_pid = fork();
            if (py_pid == 0) {
                execlp("python3", "python3", "tasks/car_detect.py", task->payload_path, NULL);
                exit(0);
            }
        }

        // 4. Collect telemetry
        is_real_telemetry = collect_system_metrics(&metrics, py_pid > 0);
        
        // Sync progress from JSON
        FILE *pf = fopen(json_file, "r");
        if (pf) {
            char buf[128];
            if (fgets(buf, sizeof(buf), pf)) 
                sscanf(buf, "{\"progress\": %d", &task->progress_counter);
            fclose(pf);
        }

        // 5. Display status and write to file for dashboard
        print_metrics(&metrics, is_real_telemetry);
        printf("[TASK] %s: %d%%\n", task->state_label, task->progress_counter);
        
        // Write telemetry JSON for dashboard
        write_telemetry_json(&metrics, "edge", task->progress_counter);

        // 6. Check completion
        if (task->progress_counter >= 100) {
            printf("\n========================================\n");
            printf("[FINISH] Task completed on Edge!\n");
            printf("========================================\n");
            if (py_pid > 0) kill(py_pid, SIGKILL);
            
            // Write final state for dashboard
            write_telemetry_json(&metrics, "completed", 100);
            
            // Cleanup state files
            remove(json_file);
            remove(state_file);
            remove(return_file);
            remove("/tmp/edge_telemetry.json");
            printf("[CLEANUP] State files removed.\n");
            return 0;
        }

        // 7. Migration decision
        if (should_migrate(&metrics, task->progress_counter)) {
            printf("\n[MIGRATE] Triggering migration to Cloud...\n");
            if (py_pid > 0) kill(py_pid, SIGKILL);
            py_pid = 0;
            
            // Write migration state for dashboard
            write_telemetry_json(&metrics, "cloud", task->progress_counter);
            
            save_checkpoint(state_file, task);
            
            // Try network transfer, fallback to file-based
            if (send_checkpoint_to_cloud(state_file) < 0) {
                printf("[NET] Network failed, using file-based transfer\n");
            }
            
            free(task); 
            task = NULL;
            
            // Wait for return - continuously write Edge's CPU status for Cloud to check
            printf("[WAIT] Offloaded to Cloud. Waiting for return...\n");
            
            int wait_count = 0;
            while (access(return_file, F_OK) != 0) {
                // Check if Cloud finished without returning
                if (access("/tmp/cloud_finished", F_OK) == 0) {
                    remove("/tmp/cloud_finished");
                    remove(json_file);
                    remove("/tmp/edge_telemetry.json");
                    remove("/tmp/edge_cpu_status.json");
                    printf("\n[DONE] Cloud completed task.\n");
                    return 0;
                }
                
                // Continuously write Edge's current CPU status for Cloud to read
                system_metrics_t edge_metrics;
                collect_system_metrics(&edge_metrics, 0);
                
                FILE *cpu_file = fopen("/tmp/edge_cpu_status.json", "w");
                if (cpu_file) {
                    fprintf(cpu_file, "{\"cpu\": %.1f, \"recovered\": %d}", 
                            edge_metrics.cpu_load, 
                            edge_metrics.cpu_load < 50.0 ? 1 : 0);
                    fclose(cpu_file);
                }
                
                wait_count++;
                printf("Edge: Waiting (CPU: %.1f%%) [%ds]\r", edge_metrics.cpu_load, wait_count * 2);
                fflush(stdout);
                sleep(2);
                
                // Timeout after 120 seconds
                if (wait_count > 60) {
                    printf("\n[TIMEOUT] Cloud not responding. Exiting.\n");
                    remove("/tmp/edge_cpu_status.json");
                    return 1;
                }
            }
            
            // Cleanup status file
            remove("/tmp/edge_cpu_status.json");
            printf("\n");
            continue;
        }

        printf("-------------------------------------------\n");
        sleep(2);
    }
    return 0;
}

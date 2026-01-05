#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include "../../edge/include/checkpoint.h"

// Network functions (inline for now)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MIGRATION_PORT 9090
#define RETURN_PORT 9091

// Receive checkpoint from Edge via TCP
int wait_for_migration(const char *save_path, char *edge_ip_out) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(MIGRATION_PORT);
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[NET] Bind failed");
        close(server_sock);
        return -1;
    }
    
    listen(server_sock, 1);
    printf("[NET] Listening for migrations on port %d...\n", MIGRATION_PORT);
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
    
    if (client_sock < 0) {
        close(server_sock);
        return -1;
    }
    
    // Store Edge IP for return transfer
    strcpy(edge_ip_out, inet_ntoa(client_addr.sin_addr));
    printf("[NET] Connection from Edge: %s\n", edge_ip_out);
    
    long file_size;
    recv(client_sock, &file_size, sizeof(file_size), 0);
    
    char *buffer = malloc(file_size);
    ssize_t received = 0;
    while (received < file_size) {
        ssize_t r = recv(client_sock, buffer + received, file_size - received, 0);
        if (r <= 0) break;
        received += r;
    }
    
    close(client_sock);
    close(server_sock);
    
    FILE *f = fopen(save_path, "wb");
    fwrite(buffer, 1, file_size, f);
    fclose(f);
    free(buffer);
    
    printf("[NET] Received %ld bytes from Edge.\n", file_size);
    return 0;
}

// Send return state to Edge
int send_return_state(const char *filepath, const char *edge_ip) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buffer = malloc(file_size);
    fread(buffer, 1, file_size, f);
    fclose(f);
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(RETURN_PORT);
    inet_pton(AF_INET, edge_ip, &server_addr.sin_addr);
    
    printf("[NET] Sending return state to Edge %s:%d...\n", edge_ip, RETURN_PORT);
    
    // Retry connection a few times
    int connected = 0;
    for (int i = 0; i < 5; i++) {
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
            connected = 1;
            break;
        }
        sleep(1);
    }
    
    if (!connected) {
        perror("[NET] Connection to Edge failed");
        close(sock);
        free(buffer);
        return -1;
    }
    
    send(sock, &file_size, sizeof(file_size), 0);
    send(sock, buffer, file_size, 0);
    
    close(sock);
    free(buffer);
    printf("[NET] Return state sent successfully.\n");
    return 0;
}

int main() {
    printf("============================================\n");
    printf("   CLOUD RUNTIME - Phase 2 (Network)       \n");
    printf("============================================\n\n");
    
    task_state_t *task = NULL;
    pid_t py_pid = 0;
    char edge_ip[64] = "127.0.0.1";
    
    const char *state_file = "/tmp/cloud_task_state.bin";
    const char *return_file = "/tmp/cloud_return.bin";
    const char *json_file = "/tmp/car_detect_internal.json";

    while (1) {
        // 1. Wait for migration from Edge
        if (task == NULL) {
            printf("[WAIT] Waiting for task migration from Edge...\n\n");
            
            // Check for network-based migration
            if (wait_for_migration(state_file, edge_ip) == 0) {
                task = malloc(sizeof(task_state_t));
                restore_checkpoint(state_file, task);
                printf("[RECEIVE] Task '%s' from Edge at %d%%\n", 
                       task->state_label, task->progress_counter);
                
                // Write internal state for Python
                FILE *f = fopen(json_file, "w");
                fprintf(f, "{\"progress\": %d}", task->progress_counter);
                fclose(f);
                py_pid = 0;
            }
        }

        // 2. Launch Python process
        if (task && py_pid == 0) {
            printf("[EXEC] Launching Panel Monitoring on Cloud...\n");
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
                char buf[512]; // Increased from 128
                if (fgets(buf, sizeof(buf), pf)) {
                    char *pos = strstr(buf, "\"progress\":");
                    if (pos) sscanf(pos, "\"progress\": %d", &task->progress_counter);
                }
                fclose(pf);
            }
            
            printf("[CLOUD] Task %s: %d%%\n", task->state_label, task->progress_counter);

            // Check completion
            if (task->progress_counter >= 100) {
                printf("\n========================================\n");
                printf("[FINISH] Task completed on Cloud!\n");
                printf("========================================\n");
                if (py_pid > 0) kill(py_pid, SIGKILL);
                
                // Signal Edge that Cloud finished
                FILE *sig = fopen("/tmp/cloud_finished", "w");
                if (sig) fclose(sig);
                
                // Cleanup state files
                remove(json_file);
                remove(state_file);
                remove(return_file);
                remove("/tmp/edge_ready");
                printf("[CLEANUP] State files removed.\n");
                return 0;
            }

            int edge_recovered = 0;
            // NEW Method: Check if Hardware Hazard is cleared
            if (access("/tmp/SENSOR_HAZARD", F_OK) != 0) {
                edge_recovered = 1;
                printf("[RECOVERY] Sensor hazard cleared. Returning task to Edge.\n");
            }
            
            // Return task to Edge if recovered and not too close to completion
            if (edge_recovered && task->progress_counter < 90) {
                printf("[RETURN] Returning task to Edge.\n");
                
                if (py_pid > 0) kill(py_pid, SIGKILL);
                py_pid = 0;
                
                save_checkpoint(return_file, task);
                
                // Always use file for reliability
                rename(return_file, "/tmp/edge_return.bin");
                printf("[TRANSFER] Task returned to Edge at %d%%\n", task->progress_counter);
                
                free(task);
                task = NULL;
                printf("[WAIT] Ready for next migration.\n\n");
            }
            
            // If no recovery, Cloud continues processing
        }

        sleep(2);
    }
    return 0;
}

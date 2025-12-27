#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define MIGRATION_PORT 9090
#define RETURN_PORT 9091
#define BUFFER_SIZE 4096

// Configuration
static char cloud_ip[64] = "127.0.0.1"; // Default to localhost

void set_cloud_ip(const char *ip) {
    strncpy(cloud_ip, ip, sizeof(cloud_ip) - 1);
}

// Send checkpoint data to Cloud via TCP
int send_checkpoint_to_cloud(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        perror("[NET] Failed to open checkpoint file");
        return -1;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Read file into buffer
    char *buffer = malloc(file_size);
    fread(buffer, 1, file_size, f);
    fclose(f);
    
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[NET] Socket creation failed");
        free(buffer);
        return -1;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(MIGRATION_PORT);
    inet_pton(AF_INET, cloud_ip, &server_addr.sin_addr);
    
    printf("[NET] Connecting to Cloud at %s:%d...\n", cloud_ip, MIGRATION_PORT);
    
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[NET] Connection to Cloud failed");
        close(sock);
        free(buffer);
        return -1;
    }
    
    // Send file size first
    send(sock, &file_size, sizeof(file_size), 0);
    
    // Send file data
    ssize_t sent = send(sock, buffer, file_size, 0);
    
    close(sock);
    free(buffer);
    
    if (sent == file_size) {
        printf("[NET] Sent %ld bytes to Cloud successfully.\n", file_size);
        return 0;
    } else {
        printf("[NET] Send incomplete: %zd/%ld bytes\n", sent, file_size);
        return -1;
    }
}

// Receive checkpoint data from Edge (Cloud-side listener)
int receive_checkpoint_from_edge(const char *save_path) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("[NET] Socket creation failed");
        return -1;
    }
    
    // Allow port reuse
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
        perror("[NET] Accept failed");
        close(server_sock);
        return -1;
    }
    
    printf("[NET] Connection from %s\n", inet_ntoa(client_addr.sin_addr));
    
    // Receive file size
    long file_size;
    recv(client_sock, &file_size, sizeof(file_size), 0);
    
    // Receive file data
    char *buffer = malloc(file_size);
    ssize_t received = 0;
    while (received < file_size) {
        ssize_t r = recv(client_sock, buffer + received, file_size - received, 0);
        if (r <= 0) break;
        received += r;
    }
    
    close(client_sock);
    close(server_sock);
    
    if (received == file_size) {
        // Save to file
        FILE *f = fopen(save_path, "wb");
        fwrite(buffer, 1, file_size, f);
        fclose(f);
        printf("[NET] Received %ld bytes, saved to %s\n", file_size, save_path);
        free(buffer);
        return 0;
    } else {
        printf("[NET] Receive incomplete: %zd/%ld bytes\n", received, file_size);
        free(buffer);
        return -1;
    }
}

// Send return state back to Edge
int send_return_to_edge(const char *filepath, const char *edge_ip) {
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
    
    printf("[NET] Sending return state to Edge at %s:%d...\n", edge_ip, RETURN_PORT);
    
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
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

// Receive return state from Cloud (Edge-side listener)
int receive_return_from_cloud(const char *save_path) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(RETURN_PORT);
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[NET] Bind failed for return port");
        close(server_sock);
        return -1;
    }
    
    listen(server_sock, 1);
    printf("[NET] Listening for return on port %d...\n", RETURN_PORT);
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
    
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
    
    printf("[NET] Return state received from Cloud.\n");
    return 0;
}

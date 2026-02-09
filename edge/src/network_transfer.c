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
    
    // Set a 3-second timeout for the migration check (Internet is slow)
    fd_set fds;
    struct timeval tv = {3, 0};
    FD_ZERO(&fds);
    FD_SET(server_sock, &fds);
    
    if (select(server_sock + 1, &fds, NULL, NULL, &tv) <= 0) {
        close(server_sock);
        return -2; 
    }

    printf("[NET] Connection attempt detected! Calling accept()...\n");

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

// Cloud-side: Listen for a 'Return Request' from Edge and send the file
int send_return_to_edge_service(const char *filepath) {
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

    // Set a 1.5 second timeout for the return check
    fd_set fds;
    struct timeval tv = {1, 500000};
    FD_ZERO(&fds);
    FD_SET(server_sock, &fds);
    
    if (select(server_sock + 1, &fds, NULL, NULL, &tv) <= 0) {
        close(server_sock);
        return -2; 
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
    
    if (client_sock < 0) return -1;

    // Send the file back through the established tunnel
    FILE *f = fopen(filepath, "rb");
    if (!f) { close(client_sock); close(server_sock); return -1; }
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    send(client_sock, &file_size, sizeof(file_size), 0);
    char *buffer = malloc(file_size);
    fread(buffer, 1, file_size, f);
    send(client_sock, buffer, file_size, 0);
    
    fclose(f);
    free(buffer);
    close(client_sock);
    close(server_sock);
    printf("[NET] Task state successfully PULLED back to Edge.\n");
    return 0;
}

// Edge-side: Connect to AWS and 'Pull' the task back
int request_return_from_cloud(const char *save_path, const char *cloud_ip) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(RETURN_PORT);
    inet_pton(AF_INET, cloud_ip, &server_addr.sin_addr);
    
    printf("[NET] Attempting to PULL task back from AWS... \n");
    
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        // AWS might not be ready yet, that's okay, we'll try again next loop
        close(sock);
        return -1;
    }
    
    long file_size;
    if (recv(sock, &file_size, sizeof(file_size), 0) <= 0) {
        close(sock);
        return -1;
    }
    
    char *buffer = malloc(file_size);
    ssize_t received = 0;
    while (received < file_size) {
        ssize_t r = recv(sock, buffer + received, file_size - received, 0);
        if (r <= 0) break;
        received += r;
    }
    
    if (received == file_size) {
        FILE *f = fopen(save_path, "wb");
        fwrite(buffer, 1, file_size, f);
        fclose(f);
        printf("[NET] Task state successfully retrieved from AWS.\n");
    }
    
    close(sock);
    free(buffer);
    return (received == file_size) ? 0 : -1;
}

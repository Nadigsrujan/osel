/*
 * ESP8266 Telemetry Receiver for Edge Device
 * 
 * Runs an HTTP server on port 8080 to receive sensor data from ESP8266
 * Writes data to /tmp/esp_telemetry.json for the Edge Scheduler to read
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>

#define HTTP_PORT 8080
#define BUFFER_SIZE 4096
#define TELEMETRY_FILE "/tmp/esp_telemetry.json"
#define STATUS_FILE "/tmp/task_status.txt"

volatile int running = 1;

void handle_signal(int sig) {
    running = 0;
}

// Get current task status
const char* get_task_status() {
    FILE *f = fopen(STATUS_FILE, "r");
    if (!f) return "idle";
    
    static char status[32];
    if (fgets(status, sizeof(status), f)) {
        fclose(f);
        // Trim newline
        status[strcspn(status, "\n")] = 0;
        return status;
    }
    fclose(f);
    return "idle";
}

// Handle HTTP request
void handle_request(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes <= 0) {
        close(client_sock);
        return;
    }
    buffer[bytes] = '\0';
    
    // Check if it's a POST to /telemetry
    if (strstr(buffer, "POST /telemetry") != NULL) {
        // Find JSON body (after double newline)
        char *body = strstr(buffer, "\r\n\r\n");
        if (body) {
            body += 4; // Skip the \r\n\r\n
            
            // Save telemetry to file
            FILE *f = fopen(TELEMETRY_FILE, "w");
            if (f) {
                fprintf(f, "%s", body);
                fclose(f);
                printf("[ESP] Received: %s\n", body);
            }
            
            // Send response with task status
            const char *status = get_task_status();
            char response[512];
            snprintf(response, sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "{\"status\":\"%s\",\"received\":true}", status);
            
            send(client_sock, response, strlen(response), 0);
        }
    } else {
        // Return simple status page for browser
        const char *status = get_task_status();
        char html[1024];
        snprintf(html, sizeof(html),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "\r\n"
            "<html><head><meta http-equiv='refresh' content='2'></head>"
            "<body style='font-family:monospace;font-size:24px;padding:20px;'>"
            "<h1>Edge Telemetry Receiver</h1>"
            "<p>Task Status: <b>%s</b></p>"
            "<p>ESP8266 Data: <a href='/telemetry'>View</a></p>"
            "</body></html>", status);
        
        send(client_sock, html, strlen(html), 0);
    }
    
    close(client_sock);
}

int main() {
    printf("===========================================\n");
    printf("   ESP8266 Telemetry Receiver (Port %d)\n", HTTP_PORT);
    printf("===========================================\n\n");
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(HTTP_PORT);
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        return 1;
    }
    
    listen(server_sock, 5);
    printf("[SERVER] Listening on port %d...\n", HTTP_PORT);
    printf("[SERVER] Waiting for ESP8266 connections...\n\n");
    
    // Set socket timeout for graceful shutdown
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock >= 0) {
            handle_request(client_sock);
        }
    }
    
    printf("\n[SERVER] Shutting down...\n");
    close(server_sock);
    return 0;
}

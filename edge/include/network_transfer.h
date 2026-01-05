#ifndef NETWORK_TRANSFER_H
#define NETWORK_TRANSFER_H

#define MIGRATION_PORT 9090
#define RETURN_PORT 9091

// Set Cloud server IP address
void set_cloud_ip(const char *ip);

// Edge → Cloud: Send checkpoint
int send_checkpoint_to_cloud(const char *filepath);

// Cloud: Receive checkpoint from Edge
int receive_checkpoint_from_edge(const char *save_path);

// Cloud: Listen for a 'Return Request' from Edge and send the file
int send_return_to_edge_service(const char *filepath);

// Edge: Connect to AWS and 'Pull' the task back
int request_return_from_cloud(const char *save_path, const char *cloud_ip);

#endif

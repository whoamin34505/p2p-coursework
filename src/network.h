#ifndef NETWORK_H
#define NETWORK_H

#define MAX_PEERS 16
#define MAX_NAME_LENGTH 32
#define MAX_IP_LENGTH 64
#define MAX_PATH_LENGTH 256
#define MAX_FILENAME_LENGTH 256

typedef struct {
    char name[MAX_NAME_LENGTH];
    char ip[MAX_IP_LENGTH];
    int port;
} Peer;

typedef struct {
    char node_name[MAX_NAME_LENGTH];
    int port;
} ServerArgs;

int load_peers(const char *filename, const char *current_node_name, Peer peers[], int max_peers);
void print_peers(Peer peers[], int peer_count);

void *server_thread(void *arg);

int find_file_in_network(const char *filename, Peer peers[], int peer_count, Peer *found_peer);
int download_file_from_network(const char *filename, Peer peers[], int peer_count);

#endif
#ifndef NETWORK_H
#define NETWORK_H

#define MAX_PEERS 16
#define MAX_NAME_LENGTH 32
#define MAX_IP_LENGTH 64
#define MAX_FILENAME_LENGTH 256
#define MAX_PATH_LENGTH 512

#define DISCOVERY_PORT 6000
#define DISCOVERY_TIMEOUT_SECONDS 2

typedef struct {
    char name[MAX_NAME_LENGTH];
    char ip[MAX_IP_LENGTH];
    int port;
} Peer;

typedef struct {
    char node_name[MAX_NAME_LENGTH];
    int port;
} NodeConfig;

typedef struct {
    char node_name[MAX_NAME_LENGTH];
    int port;
} ServerArgs;

typedef struct {
    char node_name[MAX_NAME_LENGTH];
    int tcp_port;
} DiscoveryArgs;

void *server_thread(void *arg);
void *discovery_listener_thread(void *arg);

int discover_peers(const char *current_node_name, int current_port, Peer peers[], int max_peers);
int node_name_exists_in_network(const char *node_name);
void print_peers(Peer peers[], int peer_count);

int find_file_in_network(const char *filename, Peer peers[], int peer_count, Peer *found_peer);
int download_file_from_network(const char *filename, Peer peers[], int peer_count);

#endif
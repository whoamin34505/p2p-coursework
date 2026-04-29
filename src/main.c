#include "network.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void create_directory_if_needed(const char *path) {
    struct stat st;

    if (stat(path, &st) != 0) {
        mkdir(path, 0755);
    }
}

static void print_help(void) {
    printf("\nAvailable commands:\n");
    printf("  help                 show this help\n");
    printf("  peers                show discovered peers\n");
    printf("  find <filename>      search file in P2P network\n");
    printf("  get <filename>       download file from P2P network\n");
    printf("  exit                 stop node\n\n");
}

static void remove_newline(char *text) {
    size_t length;

    length = strlen(text);

    if (length > 0 && text[length - 1] == '\n') {
        text[length - 1] = '\0';
    }
}

static void refresh_peers(const char *node_name, int port, Peer peers[], int *peer_count) {
    Peer discovered[MAX_PEERS];
    int new_count;

    memset(discovered, 0, sizeof(discovered));

    new_count = discover_peers(node_name, port, discovered, MAX_PEERS);

    if (new_count > 0 || *peer_count == 0) {
        memset(peers, 0, sizeof(Peer) * MAX_PEERS);
        memcpy(peers, discovered, sizeof(Peer) * MAX_PEERS);
        *peer_count = new_count;
    }

    printf("Discovered peers: %d\n", *peer_count);
}

int main(int argc, char *argv[]) {
    NodeConfig config;
    ServerArgs server_args;
    DiscoveryArgs discovery_args;

    pthread_t server_tid;
    pthread_t discovery_tid;

    Peer peers[MAX_PEERS];
    int peer_count;

    char command[512];
    char filename[MAX_FILENAME_LENGTH];

    if (argc != 3) {
        printf("Usage: %s <node_name> <port>\n", argv[0]);
        printf("Example: %s node1 5000\n", argv[0]);
        return 1;
    }

    memset(&config, 0, sizeof(config));
    memset(peers, 0, sizeof(peers));

    peer_count = 0;

    snprintf(config.node_name, sizeof(config.node_name), "%s", argv[1]);
    config.port = atoi(argv[2]);

    if (config.port <= 0) {
        printf("Invalid port\n");
        return 1;
    }

    create_directory_if_needed("shared");
    create_directory_if_needed("downloads");

    log_message("INFO", "Starting node: %s", config.node_name);

    memset(&server_args, 0, sizeof(server_args));
    snprintf(server_args.node_name, sizeof(server_args.node_name), "%s", config.node_name);
    server_args.port = config.port;

    if (pthread_create(&server_tid, NULL, server_thread, &server_args) != 0) {
        printf("Cannot start server thread\n");
        log_message("ERROR", "Cannot start server thread");
        return 1;
    }

    pthread_detach(server_tid);

    memset(&discovery_args, 0, sizeof(discovery_args));
    snprintf(discovery_args.node_name, sizeof(discovery_args.node_name), "%s", config.node_name);
    discovery_args.tcp_port = config.port;

    if (pthread_create(&discovery_tid, NULL, discovery_listener_thread, &discovery_args) != 0) {
        printf("Cannot start discovery thread\n");
        log_message("ERROR", "Cannot start discovery thread");
        return 1;
    }

    pthread_detach(discovery_tid);

    sleep(1);

    printf("P2P node started\n");
    printf("Node name: %s\n", config.node_name);
    printf("TCP port: %d\n", config.port);
    printf("Discovery UDP port: %d\n", DISCOVERY_PORT);

    refresh_peers(config.node_name, config.port, peers, &peer_count);

    print_help();

    while (1) {
        printf("> ");

        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }

        remove_newline(command);

        if (strcmp(command, "help") == 0) {
            print_help();
        } else if (strcmp(command, "peers") == 0) {
            refresh_peers(config.node_name, config.port, peers, &peer_count);
            print_peers(peers, peer_count);
        } else if (sscanf(command, "find %255s", filename) == 1) {
            Peer found_peer;

            memset(&found_peer, 0, sizeof(found_peer));

            refresh_peers(config.node_name, config.port, peers, &peer_count);

            if (peer_count == 0) {
                printf("No peers found\n");
                continue;
            }

            if (find_file_in_network(filename, peers, peer_count, &found_peer)) {
                printf("File found on peer: %s %s:%d\n",
                       found_peer.name,
                       found_peer.ip,
                       found_peer.port);
            } else {
                printf("File not found: %s\n", filename);
            }
        } else if (sscanf(command, "get %255s", filename) == 1) {
            refresh_peers(config.node_name, config.port, peers, &peer_count);

            if (peer_count == 0) {
                printf("No peers found\n");
                continue;
            }

            download_file_from_network(filename, peers, peer_count);
        } else if (strcmp(command, "exit") == 0) {
            log_message("INFO", "Stopping node: %s", config.node_name);
            break;
        } else if (strlen(command) == 0) {
            continue;
        } else {
            printf("Unknown command. Type help.\n");
        }
    }

    return 0;
}
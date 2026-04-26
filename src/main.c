#include "network.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

static void create_directory_if_needed(const char *path) {
    struct stat st;

    if (stat(path, &st) != 0) {
        mkdir(path, 0755);
    }
}

static void print_help(void) {
    printf("\nAvailable commands:\n");
    printf("  help                 show this help\n");
    printf("  peers                show known peers\n");
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

int main(int argc, char *argv[]) {
    char node_name[MAX_NAME_LENGTH];
    int port;
    char peers_file[MAX_PATH_LENGTH];

    Peer peers[MAX_PEERS];
    int peer_count;

    ServerArgs server_args;
    pthread_t thread_id;

    char input[512];
    char command[64];
    char filename[MAX_FILENAME_LENGTH];

    Peer found_peer;

    if (argc != 4) {
        printf("Usage: %s <node_name> <port> <peers_file>\n", argv[0]);
        printf("Example: %s node1 5000 peers.conf\n", argv[0]);
        return 1;
    }

    strncpy(node_name, argv[1], MAX_NAME_LENGTH - 1);
    node_name[MAX_NAME_LENGTH - 1] = '\0';

    port = atoi(argv[2]);

    strncpy(peers_file, argv[3], MAX_PATH_LENGTH - 1);
    peers_file[MAX_PATH_LENGTH - 1] = '\0';

    create_directory_if_needed("shared");
    create_directory_if_needed("downloads");

    log_message("INFO", "Starting node: %s", node_name);

    peer_count = load_peers(peers_file, node_name, peers, MAX_PEERS);
    if (peer_count < 0) {
        printf("Cannot load peers from file: %s\n", peers_file);
        return 1;
    }

    strncpy(server_args.node_name, node_name, MAX_NAME_LENGTH - 1);
    server_args.node_name[MAX_NAME_LENGTH - 1] = '\0';
    server_args.port = port;

    if (pthread_create(&thread_id, NULL, server_thread, &server_args) != 0) {
        printf("Cannot start server thread\n");
        log_message("ERROR", "Cannot start server thread");
        return 1;
    }

    pthread_detach(thread_id);

    printf("P2P node started\n");
    printf("Node name: %s\n", node_name);
    printf("Port: %d\n", port);
    printf("Peers loaded: %d\n", peer_count);

    print_help();

    while (1) {
        printf("%s> ", node_name);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        remove_newline(input);

        if (strlen(input) == 0) {
            continue;
        }

        memset(command, 0, sizeof(command));
        memset(filename, 0, sizeof(filename));

        sscanf(input, "%63s %255s", command, filename);

        if (strcmp(command, "help") == 0) {
            print_help();
        } else if (strcmp(command, "peers") == 0) {
            print_peers(peers, peer_count);
        } else if (strcmp(command, "find") == 0) {
            if (strlen(filename) == 0) {
                printf("Usage: find <filename>\n");
                continue;
            }

            if (find_file_in_network(filename, peers, peer_count, &found_peer)) {
                printf("File found on peer: %s %s:%d\n",
                       found_peer.name, found_peer.ip, found_peer.port);
            } else {
                printf("File not found: %s\n", filename);
            }
        } else if (strcmp(command, "get") == 0) {
            if (strlen(filename) == 0) {
                printf("Usage: get <filename>\n");
                continue;
            }

            download_file_from_network(filename, peers, peer_count);
        } else if (strcmp(command, "exit") == 0) {
            log_message("INFO", "Node stopped: %s", node_name);
            printf("Stopping node...\n");
            break;
        } else {
            printf("Unknown command. Type 'help' to see commands.\n");
        }
    }

    return 0;
}
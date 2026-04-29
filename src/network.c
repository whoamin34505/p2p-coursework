#include "network.h"
#include "crypto.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <ifaddrs.h>
#include <net/if.h>

#define BUFFER_SIZE 4096
#define COMMAND_SIZE 1024

static int send_discovery_to_all_interfaces(int socket_fd, const char *message);

static int is_valid_filename(const char *filename) {
    size_t i;
    size_t length;

    if (filename == NULL) {
        return 0;
    }

    length = strlen(filename);

    if (length == 0 || length >= MAX_FILENAME_LENGTH) {
        return 0;
    }

    for (i = 0; i < length; i++) {
        if (!(isalnum((unsigned char)filename[i]) ||
              filename[i] == '.' ||
              filename[i] == '_' ||
              filename[i] == '-')) {
            return 0;
        }
    }

    return 1;
}

static void build_shared_path(char *buffer, size_t size, const char *filename) {
    snprintf(buffer, size, "shared/%s", filename);
}

static void build_download_path(char *buffer, size_t size, const char *filename) {
    snprintf(buffer, size, "downloads/%s", filename);
}

static int send_all(int socket_fd, const void *buffer, size_t size) {
    const char *data;
    size_t total_sent;
    ssize_t sent;

    data = (const char *)buffer;
    total_sent = 0;

    while (total_sent < size) {
        sent = send(socket_fd, data + total_sent, size - total_sent, 0);
        if (sent <= 0) {
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}

static int recv_all(int socket_fd, void *buffer, size_t size) {
    char *data;
    size_t total_received;
    ssize_t received;

    data = (char *)buffer;
    total_received = 0;

    while (total_received < size) {
        received = recv(socket_fd, data + total_received, size - total_received, 0);
        if (received <= 0) {
            return -1;
        }

        total_received += (size_t)received;
    }

    return 0;
}

static int receive_line(int socket_fd, char *buffer, size_t size) {
    size_t index;
    char ch;
    ssize_t received;

    if (size == 0) {
        return -1;
    }

    index = 0;

    while (index < size - 1) {
        received = recv(socket_fd, &ch, 1, 0);
        if (received <= 0) {
            return -1;
        }

        if (ch == '\n') {
            break;
        }

        buffer[index] = ch;
        index++;
    }

    buffer[index] = '\0';

    return 0;
}

static long get_file_size(const char *path) {
    FILE *file;
    long size;

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }

    size = ftell(file);
    fclose(file);

    return size;
}

static int file_exists_in_shared(const char *filename) {
    char path[MAX_PATH_LENGTH];
    FILE *file;

    build_shared_path(path, sizeof(path), filename);

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    fclose(file);
    return 1;
}

static int send_raw_file(int socket_fd, const char *path) {
    FILE *file;
    char buffer[BUFFER_SIZE];
    size_t bytes_read;

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send_all(socket_fd, buffer, bytes_read) != 0) {
            fclose(file);
            return -1;
        }
    }

    fclose(file);
    return 0;
}

static int receive_raw_file(int socket_fd, const char *path, long file_size) {
    FILE *file;
    char buffer[BUFFER_SIZE];
    long remaining;
    size_t chunk_size;

    file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }

    remaining = file_size;

    while (remaining > 0) {
        if (remaining > BUFFER_SIZE) {
            chunk_size = BUFFER_SIZE;
        } else {
            chunk_size = (size_t)remaining;
        }

        if (recv_all(socket_fd, buffer, chunk_size) != 0) {
            fclose(file);
            return -1;
        }

        if (fwrite(buffer, 1, chunk_size, file) != chunk_size) {
            fclose(file);
            return -1;
        }

        remaining -= (long)chunk_size;
    }

    fclose(file);
    return 0;
}

static int create_server_socket(int port) {
    int server_fd;
    int option;
    struct sockaddr_in address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_message("ERROR", "socket failed");
        return -1;
    }

    option = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((unsigned short)port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        log_message("ERROR", "bind failed");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        log_message("ERROR", "listen failed");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

static int connect_to_peer(const char *ip, int port) {
    int socket_fd;
    struct sockaddr_in peer_address;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return -1;
    }

    memset(&peer_address, 0, sizeof(peer_address));
    peer_address.sin_family = AF_INET;
    peer_address.sin_port = htons((unsigned short)port);

    if (inet_pton(AF_INET, ip, &peer_address.sin_addr) <= 0) {
        close(socket_fd);
        return -1;
    }

    if (connect(socket_fd, (struct sockaddr *)&peer_address, sizeof(peer_address)) < 0) {
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

static int peer_already_exists(Peer peers[], int peer_count, const char *name, const char *ip, int port) {
    int i;

    for (i = 0; i < peer_count; i++) {
        if (strcmp(peers[i].name, name) == 0 ||
            (strcmp(peers[i].ip, ip) == 0 && peers[i].port == port)) {
            return 1;
        }
    }

    return 0;
}

void *discovery_listener_thread(void *arg) {
    DiscoveryArgs *discovery_args;
    int socket_fd;
    int option;
    struct sockaddr_in address;
    struct sockaddr_in sender_address;
    socklen_t sender_length;

    char buffer[COMMAND_SIZE];
    char command[64];
    char sender_name[MAX_NAME_LENGTH];
    char response[COMMAND_SIZE];

    int sender_tcp_port;
    ssize_t received;

    discovery_args = (DiscoveryArgs *)arg;

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        log_message("ERROR", "Cannot create UDP discovery socket");
        return NULL;
    }

    option = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(DISCOVERY_PORT);

    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        log_message("ERROR", "Cannot bind UDP discovery socket");
        close(socket_fd);
        return NULL;
    }

    log_message("INFO", "Discovery listener started on UDP port %d", DISCOVERY_PORT);

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        memset(&sender_address, 0, sizeof(sender_address));
        sender_length = sizeof(sender_address);

        received = recvfrom(socket_fd, buffer, sizeof(buffer) - 1, 0,
                            (struct sockaddr *)&sender_address, &sender_length);

        if (received <= 0) {
            continue;
        }

        buffer[received] = '\0';

        memset(command, 0, sizeof(command));
        memset(sender_name, 0, sizeof(sender_name));
        sender_tcp_port = 0;

        if (sscanf(buffer, "%63s %31s %d", command, sender_name, &sender_tcp_port) != 3) {
            continue;
        }

        //
        if (strcmp(command, "CHECK_NAME") == 0) {
    if (strcmp(sender_name, discovery_args->node_name) == 0) {
        snprintf(response, sizeof(response), "NAME_EXISTS %s %d\n",
                 discovery_args->node_name, discovery_args->tcp_port);

        sendto(socket_fd, response, strlen(response), 0,
               (struct sockaddr *)&sender_address, sender_length);

        log_message("INFO", "Node name check response sent: %s",
                    discovery_args->node_name);
    }

    continue;
}

if (strcmp(command, "DISCOVER_P2P") == 0) {
    if (strcmp(sender_name, discovery_args->node_name) == 0 &&
        sender_tcp_port == discovery_args->tcp_port) {
        continue;
    }

    snprintf(response, sizeof(response), "PEER %s %d\n",
             discovery_args->node_name, discovery_args->tcp_port);

    sendto(socket_fd, response, strlen(response), 0,
           (struct sockaddr *)&sender_address, sender_length);

    continue;
}
    }

    close(socket_fd);
    return NULL;
}

int node_name_exists_in_network(const char *node_name) {
    int socket_fd;
    int option;

    struct sockaddr_in sender_address;
    socklen_t sender_length;
    struct timeval timeout;

    char message[COMMAND_SIZE];
    char buffer[COMMAND_SIZE];
    char command[64];
    char found_name[MAX_NAME_LENGTH];

    int found_port;
    ssize_t received;

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        log_message("ERROR", "Cannot create UDP socket for node name check");
        return -1;
    }

    option = 1;

    setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &option, sizeof(option));
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    timeout.tv_sec = DISCOVERY_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    snprintf(message, sizeof(message), "CHECK_NAME %s 0\n", node_name);

    if (send_discovery_to_all_interfaces(socket_fd, message) <= 0) {
        log_message("ERROR", "Cannot send node name check broadcast");
        close(socket_fd);
        return -1;
    }

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        memset(&sender_address, 0, sizeof(sender_address));

        sender_length = sizeof(sender_address);

        received = recvfrom(socket_fd, buffer, sizeof(buffer) - 1, 0,
                            (struct sockaddr *)&sender_address,
                            &sender_length);

        if (received <= 0) {
            break;
        }

        buffer[received] = '\0';

        memset(command, 0, sizeof(command));
        memset(found_name, 0, sizeof(found_name));
        found_port = 0;

        if (sscanf(buffer, "%63s %31s %d", command, found_name, &found_port) != 3) {
            continue;
        }

        if (strcmp(command, "NAME_EXISTS") == 0 &&
            strcmp(found_name, node_name) == 0) {
            close(socket_fd);

            log_message("ERROR", "Node name already exists in network: %s", node_name);
            return 1;
        }
    }

    close(socket_fd);
    return 0;
}

static int send_discovery_to_all_interfaces(int socket_fd, const char *message) {
    struct ifaddrs *interfaces;
    struct ifaddrs *item;
    struct sockaddr_in broadcast_address;
    struct sockaddr_in fallback_address;

    int sent_count;

    sent_count = 0;

    if (getifaddrs(&interfaces) != 0) {
        log_message("ERROR", "Cannot get network interfaces");
        return -1;
    }

    for (item = interfaces; item != NULL; item = item->ifa_next) {
        if (item->ifa_addr == NULL || item->ifa_broadaddr == NULL) {
            continue;
        }

        if (item->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        if (!(item->ifa_flags & IFF_UP)) {
            continue;
        }

        if (!(item->ifa_flags & IFF_BROADCAST)) {
            continue;
        }

        if (item->ifa_flags & IFF_LOOPBACK) {
            continue;
        }

        memset(&broadcast_address, 0, sizeof(broadcast_address));
        memcpy(&broadcast_address, item->ifa_broadaddr, sizeof(broadcast_address));
        broadcast_address.sin_port = htons(DISCOVERY_PORT);

        if (sendto(socket_fd, message, strlen(message), 0,
                   (struct sockaddr *)&broadcast_address,
                   sizeof(broadcast_address)) >= 0) {
            sent_count++;
            log_message("INFO", "Discovery broadcast sent to interface: %s",
                        item->ifa_name);
        }
    }

    freeifaddrs(interfaces);

    if (sent_count == 0) {
        memset(&fallback_address, 0, sizeof(fallback_address));
        fallback_address.sin_family = AF_INET;
        fallback_address.sin_port = htons(DISCOVERY_PORT);
        fallback_address.sin_addr.s_addr = inet_addr("255.255.255.255");

        if (sendto(socket_fd, message, strlen(message), 0,
                   (struct sockaddr *)&fallback_address,
                   sizeof(fallback_address)) >= 0) {
            sent_count++;
            log_message("INFO", "Discovery broadcast sent to fallback address");
        }
    }

    return sent_count;
}

int discover_peers(const char *current_node_name, int current_port, Peer peers[], int max_peers) {
    int socket_fd;
    int option;
    int peer_count;

    struct sockaddr_in sender_address;
    socklen_t sender_length;
    struct timeval timeout;

    char message[COMMAND_SIZE];
    char buffer[COMMAND_SIZE];
    char command[64];
    char peer_name[MAX_NAME_LENGTH];
    char peer_ip[MAX_IP_LENGTH];

    int peer_port;
    ssize_t received;

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        log_message("ERROR", "Cannot create UDP discovery sender socket");
        return 0;
    }

    option = 1;

    setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &option, sizeof(option));
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    timeout.tv_sec = DISCOVERY_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    snprintf(message, sizeof(message), "DISCOVER_P2P %s %d\n",
             current_node_name, current_port);

    if (send_discovery_to_all_interfaces(socket_fd, message) <= 0) {
        log_message("ERROR", "Cannot send discovery broadcast to interfaces");
        close(socket_fd);
        return 0;
    }

    log_message("INFO", "Discovery broadcast sent");

    peer_count = 0;

    while (peer_count < max_peers) {
        memset(buffer, 0, sizeof(buffer));
        memset(&sender_address, 0, sizeof(sender_address));

        sender_length = sizeof(sender_address);

        received = recvfrom(socket_fd, buffer, sizeof(buffer) - 1, 0,
                            (struct sockaddr *)&sender_address,
                            &sender_length);

        if (received <= 0) {
            break;
        }

        buffer[received] = '\0';

        memset(command, 0, sizeof(command));
        memset(peer_name, 0, sizeof(peer_name));

        peer_port = 0;

        if (sscanf(buffer, "%63s %31s %d", command, peer_name, &peer_port) != 3) {
            continue;
        }

        if (strcmp(command, "PEER") != 0) {
            continue;
        }

        if (strcmp(peer_name, current_node_name) == 0) {
            continue;
        }

        snprintf(peer_ip, sizeof(peer_ip), "%s", inet_ntoa(sender_address.sin_addr));

        if (peer_already_exists(peers, peer_count, peer_name, peer_ip, peer_port)) {
            continue;
        }

        snprintf(peers[peer_count].name, sizeof(peers[peer_count].name), "%s", peer_name);
        snprintf(peers[peer_count].ip, sizeof(peers[peer_count].ip), "%s", peer_ip);
        peers[peer_count].port = peer_port;

        log_message("INFO", "Discovered peer: %s %s:%d",
                    peers[peer_count].name,
                    peers[peer_count].ip,
                    peers[peer_count].port);

        peer_count++;
    }

    close(socket_fd);

    return peer_count;
}

static void handle_find_command(int client_socket, const char *filename) {
    char response[COMMAND_SIZE];

    if (!is_valid_filename(filename)) {
        snprintf(response, sizeof(response), "ERROR invalid filename\n");
        send_all(client_socket, response, strlen(response));
        log_message("ERROR", "Invalid filename in FIND command: %s", filename);
        return;
    }

    if (file_exists_in_shared(filename)) {
        snprintf(response, sizeof(response), "FOUND %s\n", filename);
        log_message("INFO", "File found for remote peer: %s", filename);
    } else {
        snprintf(response, sizeof(response), "NOTFOUND %s\n", filename);
        log_message("INFO", "File not found for remote peer: %s", filename);
    }

    send_all(client_socket, response, strlen(response));
}

static void handle_get_command(int client_socket, const char *filename) {
    char source_path[MAX_PATH_LENGTH];
    char encrypted_path[MAX_PATH_LENGTH];
    char header[COMMAND_SIZE];
    char file_hash[SHA256_HEX_LENGTH];

    long encrypted_size;

    if (!is_valid_filename(filename)) {
        snprintf(header, sizeof(header), "ERROR invalid filename\n");
        send_all(client_socket, header, strlen(header));
        log_message("ERROR", "Invalid filename in GET command: %s", filename);
        return;
    }

    if (!file_exists_in_shared(filename)) {
        snprintf(header, sizeof(header), "ERROR file not found\n");
        send_all(client_socket, header, strlen(header));
        log_message("ERROR", "GET requested missing file: %s", filename);
        return;
    }

    build_shared_path(source_path, sizeof(source_path), filename);

    if (calculate_file_sha256(source_path, file_hash, sizeof(file_hash)) != 0) {
        snprintf(header, sizeof(header), "ERROR hash calculation failed\n");
        send_all(client_socket, header, strlen(header));
        log_message("ERROR", "Cannot calculate SHA-256 for file: %s", filename);
        return;
    }

    snprintf(encrypted_path, sizeof(encrypted_path), "/tmp/p2p_%ld_%d_%s.enc",
             (long)time(NULL), getpid(), filename);

    if (encrypt_file(source_path, encrypted_path) != 0) {
        snprintf(header, sizeof(header), "ERROR encryption failed\n");
        send_all(client_socket, header, strlen(header));
        log_message("ERROR", "Encryption failed for file: %s", filename);
        return;
    }

    encrypted_size = get_file_size(encrypted_path);
    if (encrypted_size < 0) {
        snprintf(header, sizeof(header), "ERROR cannot read encrypted file\n");
        send_all(client_socket, header, strlen(header));
        remove(encrypted_path);
        log_message("ERROR", "Cannot get encrypted file size: %s", filename);
        return;
    }

    snprintf(header, sizeof(header), "FILE %s %ld %s\n", filename, encrypted_size, file_hash);

    if (send_all(client_socket, header, strlen(header)) != 0) {
        remove(encrypted_path);
        log_message("ERROR", "Cannot send FILE header");
        return;
    }

    if (send_raw_file(client_socket, encrypted_path) != 0) {
        remove(encrypted_path);
        log_message("ERROR", "Cannot send encrypted file: %s", filename);
        return;
    }

    remove(encrypted_path);
    log_message("INFO", "File sent successfully: %s", filename);
}

static void handle_client(int client_socket) {
    char command_line[COMMAND_SIZE];
    char command_name[64];
    char filename[MAX_FILENAME_LENGTH];
    char response[COMMAND_SIZE];

    if (receive_line(client_socket, command_line, sizeof(command_line)) != 0) {
        log_message("ERROR", "Cannot receive command from client");
        return;
    }

    memset(command_name, 0, sizeof(command_name));
    memset(filename, 0, sizeof(filename));

    sscanf(command_line, "%63s %255s", command_name, filename);

    if (strcmp(command_name, "HELLO") == 0) {
        snprintf(response, sizeof(response), "OK\n");
        send_all(client_socket, response, strlen(response));
    } else if (strcmp(command_name, "FIND") == 0) {
        handle_find_command(client_socket, filename);
    } else if (strcmp(command_name, "GET") == 0) {
        handle_get_command(client_socket, filename);
    } else {
        snprintf(response, sizeof(response), "ERROR unknown command\n");
        send_all(client_socket, response, strlen(response));
        log_message("ERROR", "Unknown command received: %s", command_line);
    }
}

void *server_thread(void *arg) {
    ServerArgs *server_args;
    int server_fd;
    int client_socket;

    struct sockaddr_in client_address;
    socklen_t client_length;

    server_args = (ServerArgs *)arg;

    server_fd = create_server_socket(server_args->port);
    if (server_fd < 0) {
        log_message("ERROR", "Cannot start server socket");
        return NULL;
    }

    log_message("INFO", "TCP server started on port %d", server_args->port);

    while (1) {
        client_length = sizeof(client_address);
        client_socket = accept(server_fd, (struct sockaddr *)&client_address, &client_length);

        if (client_socket < 0) {
            log_message("ERROR", "accept failed");
            continue;
        }

        handle_client(client_socket);
        close(client_socket);
    }

    close(server_fd);
    return NULL;
}

void print_peers(Peer peers[], int peer_count) {
    int i;

    if (peer_count == 0) {
        printf("No discovered peers\n");
        return;
    }

    printf("Discovered peers:\n");

    for (i = 0; i < peer_count; i++) {
        printf("%d. %s %s:%d\n",
               i + 1,
               peers[i].name,
               peers[i].ip,
               peers[i].port);
    }
}

int find_file_in_network(const char *filename, Peer peers[], int peer_count, Peer *found_peer) {
    int i;
    int socket_fd;

    char command[COMMAND_SIZE];
    char response[COMMAND_SIZE];
    char response_name[64];
    char response_filename[MAX_FILENAME_LENGTH];

    if (!is_valid_filename(filename)) {
        printf("Invalid filename\n");
        log_message("ERROR", "Invalid filename in find: %s", filename);
        return 0;
    }

    for (i = 0; i < peer_count; i++) {
        socket_fd = connect_to_peer(peers[i].ip, peers[i].port);
        if (socket_fd < 0) {
            log_message("ERROR", "Cannot connect to peer: %s", peers[i].name);
            continue;
        }

        snprintf(command, sizeof(command), "FIND %s\n", filename);

        if (send_all(socket_fd, command, strlen(command)) != 0) {
            close(socket_fd);
            log_message("ERROR", "Cannot send FIND command to peer: %s", peers[i].name);
            continue;
        }

        if (receive_line(socket_fd, response, sizeof(response)) != 0) {
            close(socket_fd);
            log_message("ERROR", "Cannot receive FIND response from peer: %s", peers[i].name);
            continue;
        }

        close(socket_fd);

        memset(response_name, 0, sizeof(response_name));
        memset(response_filename, 0, sizeof(response_filename));

        if (sscanf(response, "%63s %255s", response_name, response_filename) != 2) {
            continue;
        }

        if (strcmp(response_name, "FOUND") == 0 &&
            strcmp(response_filename, filename) == 0) {
            *found_peer = peers[i];
            log_message("INFO", "File found on peer: %s", peers[i].name);
            return 1;
        }
    }

    log_message("INFO", "File not found in network: %s", filename);
    return 0;
}

int download_file_from_network(const char *filename, Peer peers[], int peer_count) {
    Peer source_peer;
    int socket_fd;

    char command[COMMAND_SIZE];
    char header[COMMAND_SIZE];
    char header_name[64];
    char header_filename[MAX_FILENAME_LENGTH];
    char expected_hash[SHA256_HEX_LENGTH];
    char actual_hash[SHA256_HEX_LENGTH];

    long encrypted_size;

    char encrypted_path[MAX_PATH_LENGTH];
    char output_path[MAX_PATH_LENGTH];

    if (!find_file_in_network(filename, peers, peer_count, &source_peer)) {
        printf("File not found: %s\n", filename);
        return -1;
    }

    socket_fd = connect_to_peer(source_peer.ip, source_peer.port);
    if (socket_fd < 0) {
        printf("Cannot connect to source peer\n");
        log_message("ERROR", "Cannot connect to source peer: %s", source_peer.name);
        return -1;
    }

    snprintf(command, sizeof(command), "GET %s\n", filename);

    if (send_all(socket_fd, command, strlen(command)) != 0) {
        close(socket_fd);
        log_message("ERROR", "Cannot send GET command");
        return -1;
    }

    if (receive_line(socket_fd, header, sizeof(header)) != 0) {
        close(socket_fd);
        log_message("ERROR", "Cannot receive FILE header");
        return -1;
    }

    if (strncmp(header, "ERROR", 5) == 0) {
        close(socket_fd);
        printf("Peer returned error: %s\n", header);
        log_message("ERROR", "Peer returned error: %s", header);
        return -1;
    }

    memset(header_name, 0, sizeof(header_name));
    memset(header_filename, 0, sizeof(header_filename));
    memset(expected_hash, 0, sizeof(expected_hash));
    memset(actual_hash, 0, sizeof(actual_hash));

    encrypted_size = 0;

    if (sscanf(header, "%63s %255s %ld %64s",
               header_name, header_filename, &encrypted_size, expected_hash) != 4) {
        close(socket_fd);
        printf("Bad response from peer: %s\n", header);
        log_message("ERROR", "Bad FILE header: %s", header);
        return -1;
    }

    if (strcmp(header_name, "FILE") != 0 ||
        encrypted_size <= 0 ||
        strcmp(header_filename, filename) != 0) {
        close(socket_fd);
        printf("Bad FILE header: %s\n", header);
        log_message("ERROR", "Bad FILE header: %s", header);
        return -1;
    }

    snprintf(encrypted_path, sizeof(encrypted_path), "downloads/.%s.enc", filename);
    build_download_path(output_path, sizeof(output_path), filename);

    if (receive_raw_file(socket_fd, encrypted_path, encrypted_size) != 0) {
        close(socket_fd);
        remove(encrypted_path);
        log_message("ERROR", "Cannot receive encrypted file: %s", filename);
        return -1;
    }

    close(socket_fd);

    if (decrypt_file(encrypted_path, output_path) != 0) {
        remove(encrypted_path);
        log_message("ERROR", "Cannot decrypt received file: %s", filename);
        return -1;
    }

    if (calculate_file_sha256(output_path, actual_hash, sizeof(actual_hash)) != 0) {
        remove(encrypted_path);
        remove(output_path);
        log_message("ERROR", "Cannot calculate SHA-256 for received file: %s", filename);
        return -1;
    }

    if (strcmp(expected_hash, actual_hash) != 0) {
        remove(encrypted_path);
        remove(output_path);
        printf("File integrity check failed: %s\n", filename);
        log_message("ERROR", "File integrity check failed: %s", filename);
        return -1;
    }

    remove(encrypted_path);

    printf("File downloaded successfully: downloads/%s\n", filename);
    printf("File integrity check passed\n");

    log_message("INFO", "File downloaded successfully: %s from %s", filename, source_peer.name);
    log_message("INFO", "File integrity check passed: %s", filename);

    return 0;
}
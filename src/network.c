#include "network.h"
#include "crypto.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define BUFFER_SIZE 4096
#define COMMAND_SIZE 512

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

    if (strstr(filename, "..") != NULL) {
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
    char character;
    ssize_t received;

    index = 0;

    while (index < size - 1) {
        received = recv(socket_fd, &character, 1, 0);
        if (received <= 0) {
            return -1;
        }

        if (character == '\n') {
            break;
        }

        buffer[index] = character;
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

    fseek(file, 0, SEEK_END);
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
        if (send_all(socket_fd, buffer, bytes_read) < 0) {
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
    long received_total;
    size_t bytes_to_receive;

    file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }

    received_total = 0;

    while (received_total < file_size) {
        bytes_to_receive = BUFFER_SIZE;

        if (file_size - received_total < BUFFER_SIZE) {
            bytes_to_receive = (size_t)(file_size - received_total);
        }

        if (recv_all(socket_fd, buffer, bytes_to_receive) < 0) {
            fclose(file);
            return -1;
        }

        fwrite(buffer, 1, bytes_to_receive, file);
        received_total += (long)bytes_to_receive;
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
        log_message("ERROR", "socket failed: %s", strerror(errno));
        return -1;
    }

    option = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((unsigned short)port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        log_message("ERROR", "bind failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        log_message("ERROR", "listen failed: %s", strerror(errno));
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
        send_all(client_socket, response, strlen(response));
        log_message("INFO", "File found for remote peer: %s", filename);
    } else {
        snprintf(response, sizeof(response), "NOTFOUND %s\n", filename);
        send_all(client_socket, response, strlen(response));
        log_message("INFO", "File not found for remote peer: %s", filename);
    }
}

static void handle_get_command(int client_socket, const char *filename) {
    char source_path[MAX_PATH_LENGTH];
    char encrypted_path[MAX_PATH_LENGTH];
    char header[COMMAND_SIZE];
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

    snprintf(header, sizeof(header), "FILE %s %ld\n", filename, encrypted_size);

    if (send_all(client_socket, header, strlen(header)) < 0) {
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
    char command[COMMAND_SIZE];
    char command_name[64];
    char filename[MAX_FILENAME_LENGTH];

    memset(command, 0, sizeof(command));
    memset(command_name, 0, sizeof(command_name));
    memset(filename, 0, sizeof(filename));

    if (receive_line(client_socket, command, sizeof(command)) != 0) {
        log_message("ERROR", "Cannot receive command from peer");
        return;
    }

    log_message("INFO", "Received command: %s", command);

    if (sscanf(command, "%63s %255s", command_name, filename) < 1) {
        send_all(client_socket, "ERROR bad command\n", strlen("ERROR bad command\n"));
        return;
    }

    if (strcmp(command_name, "HELLO") == 0) {
        send_all(client_socket, "OK\n", strlen("OK\n"));
        log_message("INFO", "HELLO request processed");
    } else if (strcmp(command_name, "FIND") == 0) {
        handle_find_command(client_socket, filename);
    } else if (strcmp(command_name, "GET") == 0) {
        handle_get_command(client_socket, filename);
    } else {
        send_all(client_socket, "ERROR unknown command\n", strlen("ERROR unknown command\n"));
        log_message("ERROR", "Unknown command: %s", command_name);
    }
}

void *server_thread(void *arg) {
    ServerArgs *server_args;
    int server_socket;
    int client_socket;
    struct sockaddr_in client_address;
    socklen_t client_length;

    server_args = (ServerArgs *)arg;

    server_socket = create_server_socket(server_args->port);
    if (server_socket < 0) {
        log_message("ERROR", "Server was not started");
        return NULL;
    }

    log_message("INFO", "Node %s started on port %d", server_args->node_name, server_args->port);
    printf("Server started on port %d\n", server_args->port);

    while (1) {
        client_length = sizeof(client_address);
        client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_length);

        if (client_socket < 0) {
            log_message("ERROR", "accept failed: %s", strerror(errno));
            continue;
        }

        handle_client(client_socket);
        close(client_socket);
    }

    close(server_socket);
    return NULL;
}

int load_peers(const char *filename, const char *current_node_name, Peer peers[], int max_peers) {
    FILE *file;
    char name[MAX_NAME_LENGTH];
    char ip[MAX_IP_LENGTH];
    int port;
    int count;

    file = fopen(filename, "r");
    if (file == NULL) {
        log_message("ERROR", "Cannot open peers file: %s", filename);
        return -1;
    }

    count = 0;

    while (fscanf(file, "%31s %63s %d", name, ip, &port) == 3) {
        if (strcmp(name, current_node_name) == 0) {
            continue;
        }

        if (count >= max_peers) {
            break;
        }

        strncpy(peers[count].name, name, MAX_NAME_LENGTH - 1);
        peers[count].name[MAX_NAME_LENGTH - 1] = '\0';

        strncpy(peers[count].ip, ip, MAX_IP_LENGTH - 1);
        peers[count].ip[MAX_IP_LENGTH - 1] = '\0';

        peers[count].port = port;
        count++;
    }

    fclose(file);

    log_message("INFO", "Loaded %d peers from %s", count, filename);
    return count;
}

void print_peers(Peer peers[], int peer_count) {
    int i;

    if (peer_count == 0) {
        printf("No peers loaded\n");
        return;
    }

    printf("Known peers:\n");

    for (i = 0; i < peer_count; i++) {
        printf("%d. %s %s:%d\n", i + 1, peers[i].name, peers[i].ip, peers[i].port);
    }
}

int find_file_in_network(const char *filename, Peer peers[], int peer_count, Peer *found_peer) {
    int i;
    int socket_fd;
    char command[COMMAND_SIZE];
    char response[COMMAND_SIZE];
    char response_name[64];
    char response_file[MAX_FILENAME_LENGTH];

    if (!is_valid_filename(filename)) {
        printf("Invalid filename\n");
        log_message("ERROR", "Invalid filename for FIND: %s", filename);
        return 0;
    }

    for (i = 0; i < peer_count; i++) {
        socket_fd = connect_to_peer(peers[i].ip, peers[i].port);
        if (socket_fd < 0) {
            log_message("ERROR", "Peer unavailable: %s %s:%d",
                        peers[i].name, peers[i].ip, peers[i].port);
            continue;
        }

        snprintf(command, sizeof(command), "FIND %s\n", filename);

        if (send_all(socket_fd, command, strlen(command)) != 0) {
            close(socket_fd);
            log_message("ERROR", "Cannot send FIND to %s", peers[i].name);
            continue;
        }

        memset(response, 0, sizeof(response));

        if (receive_line(socket_fd, response, sizeof(response)) != 0) {
            close(socket_fd);
            log_message("ERROR", "Cannot receive FIND response from %s", peers[i].name);
            continue;
        }

        close(socket_fd);

        memset(response_name, 0, sizeof(response_name));
        memset(response_file, 0, sizeof(response_file));

        sscanf(response, "%63s %255s", response_name, response_file);

        if (strcmp(response_name, "FOUND") == 0) {
            *found_peer = peers[i];
            log_message("INFO", "File %s found on peer %s", filename, peers[i].name);
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

    memset(header_name, 0, sizeof(header_name));
    memset(header_filename, 0, sizeof(header_filename));
    encrypted_size = 0;

    if (sscanf(header, "%63s %255s %ld", header_name, header_filename, &encrypted_size) != 3) {
        close(socket_fd);
        printf("Bad response from peer: %s\n", header);
        log_message("ERROR", "Bad FILE header: %s", header);
        return -1;
    }

    if (strcmp(header_name, "FILE") != 0 || encrypted_size <= 0) {
        close(socket_fd);
        printf("Peer returned error: %s\n", header);
        log_message("ERROR", "Peer returned error: %s", header);
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

    remove(encrypted_path);

    printf("File downloaded successfully: downloads/%s\n", filename);
    log_message("INFO", "File downloaded successfully: %s from %s", filename, source_peer.name);

    return 0;
}
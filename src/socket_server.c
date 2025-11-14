#include "../include/server_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <stddef.h>

#define PORT 8080

// ========================= COMMAND HANDLERS =========================

/**
 * Xử lý lệnh gửi tin nhắn (/target message)
 * @param sock: Socket của client
 * @param username: Tên người gửi
 * @param buffer: Buffer chứa command
 */
static void handle_send_command(int sock, const char *username, const char *buffer) {
    char target[32] = {0}, msg[BUFFER_SIZE] = {0};
    char *space = strchr(buffer + 1, ' ');
    if (space) {
        strncpy(target, buffer + 1, space - (buffer + 1));
        target[space - (buffer + 1)] = '\0';
        strcpy(msg, space + 1);
    } else {
        strcpy(target, buffer + 1);
    }
    log_event("Parsed command from %s: target=%s, msg='%s'", username, target, msg);

    // Kiểm tra target có phải là reserved command không
    if (strcmp(target, "menu") == 0 || strcmp(target, "users") == 0 ||
        strcmp(target, "groups") == 0 || strcmp(target, "exit") == 0) {
        send_message_safe(sock, "[Server] Invalid command format.\n", "send invalid command message");
        return;
    }

    // Kiểm tra có message không
    if (strlen(msg) == 0) {
        return;
    }

    // Kiểm tra xem target có phải là groupId không
    int isGroup = is_group_id(target);
    if (isGroup) {
        // Kiểm tra xem user có trong group không
        if (is_user_in_group(target, username)) {
            log_event("Sending group message to %s: %s", target, msg);
            send_group_message(username, target, msg);
        } else {
            log_event("User %s not in group %s", username, target);
            send_message_safe(sock, "[Server] You are not a member of this group.\n", "send not in group message");
        }
    } else if (find_client_by_name(target)) {
        log_event("Sending private message to %s: %s", target, msg);
        send_private(username, target, msg);
    } else {
        log_event("Invalid target: %s", target);
        send_message_safe(sock, "[Server] Invalid target.\n", "send invalid target message");
    }
}

/**
 * Xử lý lệnh xem lịch sử chat (|target)
 * @param sock: Socket của client
 * @param username: Tên người yêu cầu
 * @param buffer: Buffer chứa command
 */
static void handle_history_command(int sock, const char *username, const char *buffer) {
    char target[32] = {0};
    strncpy(target, buffer + 1, sizeof(target) - 1);
    target[sizeof(target) - 1] = '\0';
    log_event("Fetching conversation history for %s", target);
    int isGroup = is_group_id(target);
    send_conversation_history(sock, username, target, isGroup);
}

// ========================= XỬ LÝ CLIENT =========================
void *client_handler(void *arg) {
    int *sock_ptr = (int *)arg;
    int sock = *sock_ptr;
    char buffer[BUFFER_SIZE], username[32], password[32];
    
    // Giải phóng bộ nhớ đã cấp phát cho socket pointer ngay sau khi sử dụng
    // Để tránh memory leak nếu có lỗi xảy ra
    free(sock_ptr);
    sock_ptr = NULL;

    // Receive login information
    int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        log_event("[ERROR] Failed to receive login data for socket %d: %s", sock, len == 0 ? "Connection closed" : strerror(errno));
        fprintf(stderr, "[ERROR] Failed to receive login data for socket %d: %s\n", sock, len == 0 ? "Connection closed" : strerror(errno));
        close(sock);
        pthread_exit(NULL);
    }
    buffer[len] = '\0';
    if (sscanf(buffer, "%31[^:]:%31s", username, password) != 2) {
        log_event("[ERROR] Invalid login format from socket %d", sock);
        fprintf(stderr, "[ERROR] Invalid login format from socket %d\n", sock);
        send_message_safe(sock, "Login failed: Invalid format\n", "send invalid format message");
        close(sock);
        pthread_exit(NULL);
    }
    log_event("Login attempt: username=%s", username);

    if (!check_login(username, password)) {
        send_message_safe(sock, "Login failed\n", "send login failed message");
        close(sock);
        pthread_exit(NULL);
    }

    // Check if username is already in use
    if (find_client_by_name(username)) {
        send_message_safe(sock, "Login failed: Username already in use\n", "send duplicate username message");
        close(sock);
        pthread_exit(NULL);
    }

    // Kiểm tra giới hạn số lượng client
    pthread_mutex_lock(&clients_mutex);
    if (clientCount >= MAX_CLIENTS) {
        pthread_mutex_unlock(&clients_mutex);
        log_event("[ERROR] Maximum clients limit reached (%d)", MAX_CLIENTS);
        send_message_safe(sock, "Login failed: Server is full\n", "send server full message");
        close(sock);
        pthread_exit(NULL);
    }
    
    strncpy(clients[clientCount].username, username, sizeof(clients[clientCount].username) - 1);
    clients[clientCount].username[sizeof(clients[clientCount].username) - 1] = '\0';
    clients[clientCount].socket = sock;
    clientCount++;
    pthread_mutex_unlock(&clients_mutex);

    send_message_safe(sock, "Login successful\n", "send login success message");
    log_event("%s logged in", username);
    show_menu(sock);

    // Message processing loop
    while (1) {
        if (sock < 0) {
            log_event("[ERROR] Invalid socket %d for %s", sock, username);
            fprintf(stderr, "[ERROR] Invalid socket %d for %s\n", sock, username);
            break;
        }
        len = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (len < 0) {
            log_event("[ERROR] Receive failed for %s: %s", username, strerror(errno));
            fprintf(stderr, "[ERROR] Receive failed for %s: %s\n", username, strerror(errno));
            break;
        } else if (len == 0) {
            log_event("%s disconnected: Connection closed", username);
            fprintf(stderr, "%s disconnected: Connection closed\n", username);
            break;
        }
        buffer[len] = '\0';
        log_event("Received from %s: %s", username, buffer);

        if (strncmp(buffer, "/exit", 5) == 0) {
            break;
        }
        else if (strncmp(buffer, "/menu", 5) == 0) {
            show_menu(sock);
            continue;
        }
        else if (strncmp(buffer, "/users", 6) == 0) {
            show_users(sock);
            continue;
        }
        else if (strncmp(buffer, "/groups", 7) == 0) {
            show_groups_for_user(sock, username);
            continue;
        }
        else if (buffer[0] == '/') {
            handle_send_command(sock, username, buffer);
        }
        else if (buffer[0] == '|') {
            handle_history_command(sock, username, buffer);
        }
        else {
            log_event("Broadcasting message from %s: %s", username, buffer);
            broadcast(username, buffer);
        }
    }

    remove_client(sock);
    pthread_exit(NULL);
}

// ========================= SERVER INITIALIZATION =========================

static int init_server() {
    // Open log file before calling any function that uses log_event
    logFile = fopen("server.log", "a");
    if (!logFile) {
        fprintf(stderr, "[ERROR] Failed to open server.log: %s\n", strerror(errno));
        return -1;
    }
    log_event("Server initialized, conversation directory will be managed automatically");

    load_users();
    load_groups();
    log_event("Server data loaded: %d users, %d groups", userCount, groupCount);
    return 0;
}

static int setup_server_socket(int port) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        log_event("[ERROR] Socket creation failed: %s", strerror(errno));
        fprintf(stderr, "[ERROR] Socket creation failed: %s\n", strerror(errno));
        return -1;
    }

    // Allow reuse of address
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_event("[ERROR] Setsockopt failed: %s", strerror(errno));
        fprintf(stderr, "[ERROR] Setsockopt failed: %s\n", strerror(errno));
        close(server_sock);
        return -1;
    }

    // Configure server address
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_event("[ERROR] Bind failed on port %d: %s", port, strerror(errno));
        fprintf(stderr, "[ERROR] Bind failed on port %d: %s\n", port, strerror(errno));
        close(server_sock);
        return -1;
    }

    if (listen(server_sock, 5) < 0) {
        log_event("[ERROR] Listen failed: %s", strerror(errno));
        fprintf(stderr, "[ERROR] Listen failed: %s\n", strerror(errno));
        close(server_sock);
        return -1;
    }

    printf("Server started on port %d\n", port);
    log_event("Server listening on port %d", port);
    return server_sock;
}

static void run_server(int server_sock) {
    while (1) {
        int client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) {
            log_event("[ERROR] Accept failed: %s", strerror(errno));
            fprintf(stderr, "[ERROR] Accept failed: %s\n", strerror(errno));
            continue;
        }
        log_event("New client connected: socket %d", client_sock);

        // Tạo thread để xử lý client
        pthread_t tid;
        int *client_sock_ptr = malloc(sizeof(int));
        if (!client_sock_ptr) {
            log_event("[ERROR] Failed to allocate memory for client socket");
            fprintf(stderr, "[ERROR] Failed to allocate memory for client socket\n");
            close(client_sock);
            continue;
        }
        *client_sock_ptr = client_sock;

        if (pthread_create(&tid, NULL, client_handler, client_sock_ptr) != 0) {
            log_event("[ERROR] Failed to create client thread: %s", strerror(errno));
            fprintf(stderr, "[ERROR] Failed to create client thread: %s\n", strerror(errno));
            free(client_sock_ptr);
            close(client_sock);
            continue;
        }
        pthread_detach(tid);
    }
}

// ========================= MAIN =========================
int main() {
    printf("=== IPC CHAT SERVER (SOCKET MODE) ===\n");

    // initialize server
    if (init_server() < 0) {
        fprintf(stderr, "[ERROR] Server initialization failed\n");
        return 1;
    }

    // Create & configure server socket
    int server_sock = setup_server_socket(PORT);
    if (server_sock < 0) {
        fprintf(stderr, "[ERROR] Server socket setup failed\n");
        if (logFile) {
            fclose(logFile);
        }
        return 1;
    }

    // Run server (infinite loop)
    run_server(server_sock);

    // Cleanup 
    log_event("Server shutting down");
    if (logFile) {
        fclose(logFile);
    }
    close(server_sock);
    return 0;
}

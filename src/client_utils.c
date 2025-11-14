#include "../include/client_utils.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

// Biến global để track chế độ chat
char current_chat_target[32] = "";
int in_chat_mode = 0;

void print_menu() {
    printf("\n=== COMMAND MENU ===\n");
    printf("/menu              : Show this menu\n");
    printf("/users             : List online users\n");
    printf("/groups            : List all groups\n");
    printf("|<username>        : Open chat with user\n");
    printf("|<groupId>         : View group chat history\n");
    printf("/<username> <msg>  : Send private message\n");
    printf("/<groupId> <msg>   : Send message to group\n");
    printf("/esc               : Exit chat mode\n");
    printf("/exit              : Logout\n");
    printf("====================\n");
}

void clear_screen() {
    #ifdef _WIN32
        system("cls");
    #else
        system("clear");
    #endif
}

void show_chat_header(const char *target) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  Chat with: %s\n", target);
    printf("  Type message directly to send | Use '/esc' to exit chat\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");
}

// ========================= UTILITY FUNCTIONS =========================
void trim_string(char *str) {
    if (!str || strlen(str) == 0) {
        return;
    }
    
    // Loại bỏ khoảng trắng ở đầu
    char *start = str;
    while (*start == ' ') start++;
    
    // Loại bỏ khoảng trắng ở cuối
    char *end = start + strlen(start) - 1;
    while (end > start && *end == ' ') end--;
    *(end + 1) = '\0';
    
    // Di chuyển chuỗi về đầu nếu cần
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

void parse_command(const char *input, char *target, size_t target_size, char *message, size_t message_size) {
    if (!input || input[0] != '/') {
        if (target) target[0] = '\0';
        if (message) message[0] = '\0';
        return;
    }
    
    char *space = strchr(input + 1, ' ');
    if (space) {
        // Có message
        size_t target_len = space - (input + 1);
        if (target && target_size > 0) {
            strncpy(target, input + 1, target_len < target_size - 1 ? target_len : target_size - 1);
            target[target_len < target_size - 1 ? target_len : target_size - 1] = '\0';
        }
        if (message && message_size > 0) {
            strncpy(message, space + 1, message_size - 1);
            message[message_size - 1] = '\0';
        }
    } else {
        // Không có message, chỉ có target
        if (target && target_size > 0) {
            strncpy(target, input + 1, target_size - 1);
            target[target_size - 1] = '\0';
        }
        if (message) message[0] = '\0';
    }
}

void *recv_thread(void *arg) {
    int sock = *(int *)arg;
    char buffer[BUFFER_SIZE];
    char *full_message = NULL;
    size_t total_len = 0;
    int len;
    int expecting_history = 0;

    while ((len = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[len] = '\0';
        full_message = realloc(full_message, total_len + len + 1);
        memcpy(full_message + total_len, buffer, len);
        total_len += len;
        full_message[total_len] = '\0';

        // Kiểm tra nếu đang nhận lịch sử chat
        if (strstr(full_message, "=== History with") != NULL) {
            expecting_history = 1;
        }

        // Nếu nhận xong lịch sử hoặc tin nhắn ngắn
        if (strstr(full_message, "=== End of History ===\n") != NULL || 
            (!expecting_history && total_len < BUFFER_SIZE - 1)) {
            
            // Luôn hiển thị lịch sử khi nhận được
            if (expecting_history || strstr(full_message, "=== History with") != NULL || 
                strstr(full_message, "=== End of History") != NULL) {
                printf("%s", full_message);
            } 
            // Nếu đang trong chat mode và không phải lịch sử, đây là tin nhắn mới
            else if (in_chat_mode) {
                // Tin nhắn mới trong chat mode
                printf("%s", full_message);
            } 
            // Không trong chat mode, hiển thị bình thường
            else {
                printf("%s", full_message);
            }
            
            if (strstr(full_message, "=== End of History ===\n") != NULL) {
                expecting_history = 0;
            }
            
            free(full_message);
            full_message = NULL;
            total_len = 0;
        }
        fflush(stdout);
    }

    if (full_message) {
        free(full_message);
    }
    printf("\n[Disconnected from server]: %s\n", len == 0 ? "Server closed connection" : strerror(errno));
    close(sock);
    exit(0);
}

void handle_server_message(int sock) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, recv_thread, &sock) != 0) {
        printf("Failed to create receive thread: %s\n", strerror(errno));
        close(sock);
        exit(1);
    }
    pthread_detach(tid);
}

void handle_user_input(int sock, const char *username) {
    char msg[BUFFER_SIZE];
    while (1) {
        // Hiển thị prompt khác nhau tùy vào chế độ
        if (in_chat_mode) {
            printf("%s> ", current_chat_target);
        } else {
            printf("> ");
        }
        fflush(stdout);

        if (!fgets(msg, sizeof(msg), stdin))
            break;
        msg[strcspn(msg, "\n")] = 0;

        // Cắt khoảng trắng đầu và cuối
        trim_string(msg);

        if (strlen(msg) == 0) continue;

        // Xử lý lệnh /exit
        if (strcmp(msg, "/exit") == 0) {
            if (send(sock, "/exit", 5, 0) < 0) {
                printf("Failed to send /exit: %s\n", strerror(errno));
            }
            break;
        }

        // Xử lý lệnh /esc để thoát chat mode
        if (strcmp(msg, "/esc") == 0) {
            if (in_chat_mode) {
                in_chat_mode = 0;
                current_chat_target[0] = '\0';
                clear_screen();
                printf("Exited chat mode. Back to main menu.\n");
                print_menu();
            }
            continue;
        }

        // Xử lý lệnh |username để vào chat mode
        if (msg[0] == '|' && strlen(msg) > 1) {
            char target[32] = {0};
            strncpy(target, msg + 1, sizeof(target) - 1);
            target[sizeof(target) - 1] = '\0';
            
            // Xóa khoảng trắng
            trim_string(target);
            
            if (strlen(target) > 0) {
                // Vào chat mode
                strncpy(current_chat_target, target, sizeof(current_chat_target) - 1);
                current_chat_target[sizeof(current_chat_target) - 1] = '\0';
                in_chat_mode = 1;
                
                // Xóa màn hình và hiển thị header
                clear_screen();
                show_chat_header(current_chat_target);
                
                // Gửi lệnh yêu cầu lịch sử chat
                if (send(sock, msg, strlen(msg), 0) < 0) {
                    printf("Failed to request chat history: %s\n", strerror(errno));
                    in_chat_mode = 0;
                    current_chat_target[0] = '\0';
                }
            }
            continue;
        }

        // Nếu đang trong chat mode, chỉ cho phép gửi tin nhắn hoặc /esc
        if (in_chat_mode) {
            if (msg[0] == '/') {
                printf("Only '/esc' is allowed in chat mode.\n");
                continue;
            }

            size_t target_len = strlen(current_chat_target);
            size_t message_len = strlen(msg);
            if (target_len + 2 + message_len >= BUFFER_SIZE) {
                printf("Message is too long. Please shorten it.\n");
                continue;
            }

            char send_msg[BUFFER_SIZE];
            send_msg[0] = '/';
            memcpy(send_msg + 1, current_chat_target, target_len);
            send_msg[1 + target_len] = ' ';
            memcpy(send_msg + 1 + target_len + 1, msg, message_len);
            send_msg[1 + target_len + 1 + message_len] = '\0';

            if (send(sock, send_msg, strlen(send_msg), 0) < 0) {
                printf("Failed to send message: %s\n", strerror(errno));
            } else {
                printf("[You] %s\n", msg);
            }
            continue;
        }

        // Xử lý các lệnh khác khi không trong chat mode
        if (msg[0] == '/' && strcmp(msg, "/menu") != 0 && 
            strcmp(msg, "/users") != 0 && strcmp(msg, "/groups") != 0) {
            char target[32] = {0}, message[BUFFER_SIZE] = {0};
            parse_command(msg, target, sizeof(target), message, sizeof(message));
            if (strlen(message) > 0) {
                printf("To %s: %s\n", target, message);
            } else {
                printf("Sending to server: %s\n", msg);
            }
        } else {
            printf("Sending to server: %s\n", msg);
        }

        if (send(sock, msg, strlen(msg), 0) < 0) {
            printf("Failed to send to server: %s\n", strerror(errno));
        }
    }
    close(sock);
}
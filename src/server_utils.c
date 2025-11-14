#include "../include/server_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>

User users[100];
Group groups[50];
int userCount = 0;
int groupCount = 0;
FILE *logFile = NULL;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// Client management
Client clients[MAX_CLIENTS];
int clientCount = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_event(const char *fmt, ...) {
    if (!logFile) {
        fprintf(stderr, "[ERROR] logFile is NULL in log_event: %s\n", strerror(errno));
        return;
    }
    va_list args;
    va_start(args, fmt);

    time_t now = time(NULL);
    char *t = ctime(&now);
    t[strcspn(t, "\n")] = 0;
    fprintf(logFile, "[%s] ", t);
    vfprintf(logFile, fmt, args);
    fprintf(logFile, "\n");
    if (fflush(logFile) != 0) {
        fprintf(stderr, "[ERROR] Failed to flush logFile: %s\n", strerror(errno));
    }

    va_end(args);
}

// Hàm helper để tìm file data
static FILE* find_data_file(const char* filename) {
    // Danh sách các đường dẫn có thể thử
    const char* paths[] = {
        "./data/%s",           // Thư mục hiện tại
        "../data/%s",          // Nếu chạy từ build/
        "data/%s",             // Không có ./
        NULL
    };
    
    char fullpath[PATH_MAX];
    FILE* f = NULL;
    
    for (int i = 0; paths[i] != NULL; i++) {
        snprintf(fullpath, sizeof(fullpath), paths[i], filename);
        f = fopen(fullpath, "r");
        if (f) {
            if (logFile) {
                log_event("Found data file at: %s", fullpath);
            } else {
                fprintf(stderr, "[INFO] Found data file at: %s\n", fullpath);
            }
            return f;
        }
    }
    
    return NULL;
}

// Hàm helper để lấy đường dẫn đến thư mục conversation đúng
static const char* get_conversation_dir() {
    static char conv_dir[PATH_MAX] = "";
    static int initialized = 0;
    
    if (initialized) {
        return conv_dir;
    }
    
    // Danh sách các đường dẫn có thể thử (ưu tiên ../conversation trước vì thường chạy từ build/)
    const char* paths[] = {
        "../conversation",     // Nếu chạy từ build/ - ưu tiên cao nhất
        "./conversation",      // Thư mục hiện tại
        "conversation",        // Không có ./
        NULL
    };
    
    struct stat st;
    for (int i = 0; paths[i] != NULL; i++) {
        if (stat(paths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            strncpy(conv_dir, paths[i], sizeof(conv_dir) - 1);
            conv_dir[sizeof(conv_dir) - 1] = '\0';
            initialized = 1;
            if (logFile) {
                log_event("Found conversation directory at: %s", conv_dir);
            } else {
                fprintf(stderr, "[INFO] Found conversation directory at: %s\n", conv_dir);
            }
            return conv_dir;
        }
    }
    
    // Nếu không tìm thấy, thử tạo ở thư mục hiện tại hoặc thư mục cha
    for (int i = 0; paths[i] != NULL; i++) {
        if (mkdir(paths[i], 0777) == 0 || errno == EEXIST) {
            strncpy(conv_dir, paths[i], sizeof(conv_dir) - 1);
            conv_dir[sizeof(conv_dir) - 1] = '\0';
            initialized = 1;
            if (logFile) {
                log_event("Created/found conversation directory at: %s", conv_dir);
            } else {
                fprintf(stderr, "[INFO] Created/found conversation directory at: %s\n", conv_dir);
            }
            return conv_dir;
        }
    }
    
    // Mặc định dùng thư mục hiện tại
    strncpy(conv_dir, "./conversation", sizeof(conv_dir) - 1);
    conv_dir[sizeof(conv_dir) - 1] = '\0';
    initialized = 1;
    return conv_dir;
}

void load_users() {
    FILE *f = find_data_file("user.txt");
    if (!f) {
        fprintf(stderr, "[ERROR] Cannot find data/user.txt in any location.\n");
        fprintf(stderr, "[ERROR] Tried: ./data/user.txt, ../data/user.txt, data/user.txt\n");
        fprintf(stderr, "[ERROR] Current working directory: ");
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            fprintf(stderr, "%s\n", cwd);
        } else {
            fprintf(stderr, "unknown\n");
        }
        exit(1);
    }
    while (fscanf(f, "%31[^:]:%31s\n", users[userCount].username, users[userCount].password) == 2) {
        if (userCount >= 100) {
            fprintf(stderr, "[WARNING] Maximum users limit (100) reached. Ignoring remaining users.\n");
            log_event("[WARNING] Maximum users limit (100) reached");
            break;
        } 
        userCount++;
    }
    fclose(f);
    log_event("Loaded %d users from user.txt", userCount);
}

void load_groups() {
    FILE *f = find_data_file("group.txt");
    if (!f) {
        fprintf(stderr, "[ERROR] Cannot find data/group.txt in any location.\n");
        fprintf(stderr, "[ERROR] Tried: ./data/group.txt, ../data/group.txt, data/group.txt\n");
        fprintf(stderr, "[ERROR] Current working directory: ");
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            fprintf(stderr, "%s\n", cwd);
        } else {
            fprintf(stderr, "unknown\n");
        }
        exit(1);
    }
    while (fscanf(f, "%31[^:]:%63[^:]:%255[^\n]\n",
                  groups[groupCount].groupId,
                  groups[groupCount].groupName,
                  groups[groupCount].members) == 3) {
        if (groupCount >= 50) {
            fprintf(stderr, "[WARNING] Maximum groups limit (50) reached. Ignoring remaining groups.\n");
            log_event("[WARNING] Maximum groups limit (50) reached");
            break;
        }
        groupCount++;
    }
    fclose(f);
    log_event("Loaded %d groups from group.txt", groupCount);
}

int is_user_in_group(const char *groupId, const char *username) {
    for (int i = 0; i < groupCount; i++) {
        if (strcmp(groups[i].groupId, groupId) == 0) {
            char tmp[256];
            strncpy(tmp, groups[i].members, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';
            char *tok = strtok(tmp, ",");
            while (tok) {
                if (strcmp(tok, username) == 0)
                    return 1;
                tok = strtok(NULL, ",");
            }
        }
    }
    return 0;
}

// Kiểm tra xem groupId có tồn tại trong danh sách groups không
int is_group_id(const char *groupId) {
    for (int i = 0; i < groupCount; i++) {
        if (strcmp(groups[i].groupId, groupId) == 0) {
            return 1;
        }
    }
    return 0;
}

void save_conversation(const char *sender, const char *target, const char *msg, int isGroup) {
    pthread_mutex_lock(&file_mutex);
    
    // Đảm bảo thư mục conversation tồn tại
    const char* conv_dir = get_conversation_dir();
    if (mkdir(conv_dir, 0777) == -1 && errno != EEXIST) {
        log_event("[ERROR] Failed to create conversation directory %s: %s", conv_dir, strerror(errno));
        fprintf(stderr, "[ERROR] Failed to create conversation directory %s: %s\n", conv_dir, strerror(errno));
    }

    char filename[PATH_MAX];
    get_conversation_filename(filename, sizeof(filename), sender, target, isGroup);

    FILE *f = fopen(filename, "a");
    if (!f) {
        log_event("[ERROR] Failed to open conversation file %s for writing: %s", filename, strerror(errno));
        fprintf(stderr, "[ERROR] Failed to open conversation file %s for writing: %s\n", filename, strerror(errno));
        pthread_mutex_unlock(&file_mutex);
        return;
    }

    time_t now = time(NULL);
    char *t = ctime(&now);
    t[strcspn(t, "\n")] = 0;
    fprintf(f, "[%s] %s: %s\n", t, sender, msg);
    if (fflush(f) != 0) {
        log_event("[ERROR] Failed to flush conversation file %s: %s", filename, strerror(errno));
        fprintf(stderr, "[ERROR] Failed to flush conversation file %s: %s\n", filename, strerror(errno));
    }
    if (fclose(f) != 0) {
        log_event("[ERROR] Failed to close conversation file %s: %s", filename, strerror(errno));
        fprintf(stderr, "[ERROR] Failed to close conversation file %s: %s\n", filename, strerror(errno));
    }
    log_event("Saved conversation to %s: %s: %s", filename, sender, msg);
    pthread_mutex_unlock(&file_mutex);
}

void send_conversation_history(int sock, const char *sender, const char *target, int isGroup) {
    pthread_mutex_lock(&file_mutex);
    
    char filename[PATH_MAX];
    get_conversation_filename(filename, sizeof(filename), sender, target, isGroup);
    log_event("Attempting to read conversation file: %s", filename);

    FILE *f = fopen(filename, "r");
    if (!f) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[Server] No conversation history with %s.\n", target);
        send_message_safe(sock, msg, "send no history message");
        pthread_mutex_unlock(&file_mutex);
        return;
    }

    // Bỏ header và footer, chỉ gửi nội dung lịch sử
    char line[512];
    int lines_sent = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;
        log_event("Sending history line: %s", line);
        
        // Tạo message với newline
        char formatted_line[512 + 2];
        snprintf(formatted_line, sizeof(formatted_line), "%s\n", line);
        
        if (send_message_safe(sock, formatted_line, "send history line") < 0) {
            break;
        }
        lines_sent++;
    }

    if (lines_sent == 0) {
        send_message_safe(sock, "No messages found.\n", "send no messages message");
    }

    if (fclose(f) != 0) {
        log_event("[ERROR] Failed to close file %s: %s", filename, strerror(errno));
        fprintf(stderr, "[ERROR] Failed to close file %s: %s\n", filename, strerror(errno));
    }
    log_event("Sent conversation history for %s to socket %d (lines sent: %d)", target, sock, lines_sent);
    pthread_mutex_unlock(&file_mutex);
}

// ========================= UTILITY FUNCTIONS =========================

int send_message_safe(int sock, const char *msg, const char *error_context) {
    if (sock < 0 || !msg) {
        return -1;
    }
    
    size_t msg_len = strlen(msg);
    if (msg_len == 0) {
        return 0;
    }
    
    if (send(sock, msg, msg_len, 0) < 0) {
        if (error_context) {
            log_event("[ERROR] Failed to %s on socket %d: %s", error_context, sock, strerror(errno));
            fprintf(stderr, "[ERROR] Failed to %s on socket %d: %s\n", error_context, sock, strerror(errno));
        } else {
            log_event("[ERROR] Failed to send message on socket %d: %s", sock, strerror(errno));
            fprintf(stderr, "[ERROR] Failed to send message on socket %d: %s\n", sock, strerror(errno));
        }
        return -1;
    }
    return 0;
}

/**
 * Tạo tên file conversation từ sender và target
 * @param filename: Buffer để lưu tên file
 * @param size: Kích thước buffer
 * @param sender: Tên người gửi
 * @param target: Tên người nhận hoặc group ID
 * @param isGroup: 1 nếu là group, 0 nếu là private message
 */
void get_conversation_filename(char *filename, size_t size, const char *sender, const char *target, int isGroup) {
    const char* conv_dir = get_conversation_dir();
    
    if (isGroup) {
        snprintf(filename, size, "%s/conversation_%s.txt", conv_dir, target);
    } else {
        // Sắp xếp tên user theo thứ tự alphabet để đảm bảo tên file nhất quán
        const char *user1 = strcmp(sender, target) < 0 ? sender : target;
        const char *user2 = strcmp(sender, target) < 0 ? target : sender;
        snprintf(filename, size, "%s/conversation_%s_%s.txt", conv_dir, user1, user2);
    }
}

// ========================= CLIENT MANAGEMENT FUNCTIONS =========================

Client *find_client_by_name(const char *username) {
    pthread_mutex_lock(&clients_mutex);
    Client *result = NULL;
    for (int i = 0; i < clientCount; i++) {
        if (strcmp(clients[i].username, username) == 0) {
            result = &clients[i];
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return result;
}

void remove_client(int socket) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < clientCount; i++) {
        if (clients[i].socket == socket) {
            log_event("%s disconnected", clients[i].username);
            shutdown(clients[i].socket, SHUT_RDWR);
            close(clients[i].socket);
            for (int j = i; j < clientCount - 1; j++)
                clients[j] = clients[j + 1];
            clientCount--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

int check_login(const char *username, const char *password) {
    for (int i = 0; i < userCount; i++) {
        if (strcmp(users[i].username, username) == 0 &&
            strcmp(users[i].password, password) == 0)
            return 1;
    }
    return 0;
}

// ========================= MESSAGE SENDING FUNCTIONS =========================

void broadcast(const char *sender, const char *msg) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "[%s -> ALL]: %s\n", sender, msg);
    pthread_mutex_lock(&clients_mutex);
    int count = clientCount;
    Client local_clients[MAX_CLIENTS];
    for (int i = 0; i < count; i++) {
        local_clients[i] = clients[i];
    }
    pthread_mutex_unlock(&clients_mutex);
    
    for (int i = 0; i < count; i++) {
        send_message_safe(local_clients[i].socket, buffer, "send broadcast");
    }
    log_event("%s broadcast: %s", sender, msg);
}

void send_private(const char *sender, const char *target, const char *msg) {
    Client *receiver = find_client_by_name(target);
    char buffer[BUFFER_SIZE];
    if (receiver) {
        snprintf(buffer, sizeof(buffer), "[PM %s → %s]: %s\n", sender, target, msg);
        send_message_safe(receiver->socket, buffer, "send private message");
        save_conversation(sender, target, msg, 0);
        log_event("%s → %s: %s", sender, target, msg);
    } else {
        snprintf(buffer, sizeof(buffer), "[Server] User %s not found.\n", target);
        Client *senderClient = find_client_by_name(sender);
        if (senderClient) {
            send_message_safe(senderClient->socket, buffer, "send error message");
        }
    }
}

void send_group_message(const char *sender, const char *groupId, const char *msg) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "[%s@%s]: %s\n", sender, groupId, msg);
    pthread_mutex_lock(&clients_mutex);
    int count = clientCount;
    Client local_clients[MAX_CLIENTS];
    for (int i = 0; i < count; i++) {
        local_clients[i] = clients[i];
    }
    pthread_mutex_unlock(&clients_mutex);
    
    for (int i = 0; i < count; i++) {
        if (is_user_in_group(groupId, local_clients[i].username)) {
            send_message_safe(local_clients[i].socket, buffer, "send group message");
        }
    }
    save_conversation(sender, groupId, msg, 1);
    log_event("%s → GROUP %s: %s", sender, groupId, msg);
}

void show_menu(int sock) {
    const char *menu =
        "\n=== COMMAND MENU ===\n"
        "/menu              : Show this menu\n"
        "/users             : List online users\n"
        "/groups            : List all groups\n"
        "|<username>        : View chat history with user\n"
        "|<groupId>         : View group chat history\n"
        "/<username> <msg>  : Send private message\n"
        "/<groupId> <msg>   : Send message to group\n"
        "/esc               : Exit chat mode\n"
        "/exit              : Logout\n";
    send_message_safe(sock, menu, "send menu");
}

void show_users(int sock) {
    char buffer[BUFFER_SIZE] = "=== Online Users ===\n";
    size_t pos = strlen(buffer);
    
    pthread_mutex_lock(&clients_mutex);
    int count = clientCount;
    for (int i = 0; i < count && pos < sizeof(buffer) - 32; i++) {
        int written = snprintf(buffer + pos, sizeof(buffer) - pos, "%s\n", clients[i].username);
        if (written > 0 && (size_t)written < sizeof(buffer) - pos) {
            pos += written;
        } else {
            break;  // Buffer đầy
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    send_message_safe(sock, buffer, "send user list");
}

void show_groups_for_user(int sock, const char *username) {
    char buffer[BUFFER_SIZE] = "=== Your Groups ===\n";
    size_t pos = strlen(buffer);
    int found = 0;
    
    for (int i = 0; i < groupCount; i++) {
        if (is_user_in_group(groups[i].groupId, username)) {
            int written = snprintf(buffer + pos, sizeof(buffer) - pos, "%s - %s\n", 
                                   groups[i].groupId, groups[i].groupName);
            if (written > 0 && (size_t)written < sizeof(buffer) - pos) {
                pos += written;
                found = 1;
            } else {
                break;  // Buffer đầy
            }
        }
    }
    if (!found && pos < sizeof(buffer) - 30) {
        snprintf(buffer + pos, sizeof(buffer) - pos, "(You are not in any groups)\n");
    }
    send_message_safe(sock, buffer, "send group list");
}
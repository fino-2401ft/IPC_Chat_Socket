#include "../include/server_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>

User users[100];
Group groups[50];
int userCount = 0;
int groupCount = 0;
FILE *logFile = NULL;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

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
        groupCount++;
    }
    fclose(f);
    log_event("Loaded %d groups from group.txt", groupCount);
}

int is_user_in_group(const char *groupId, const char *username) {
    for (int i = 0; i < groupCount; i++) {
        if (strcmp(groups[i].groupId, groupId) == 0) {
            char tmp[256];
            strcpy(tmp, groups[i].members);
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

void save_conversation(const char *sender, const char *target, const char *msg, int isGroup) {
    pthread_mutex_lock(&file_mutex);
    
    // Đảm bảo thư mục conversation tồn tại
    const char* conv_dir = get_conversation_dir();
    if (mkdir(conv_dir, 0777) == -1 && errno != EEXIST) {
        log_event("[ERROR] Failed to create conversation directory %s: %s", conv_dir, strerror(errno));
        fprintf(stderr, "[ERROR] Failed to create conversation directory %s: %s\n", conv_dir, strerror(errno));
    }

    char filename[PATH_MAX];
    if (isGroup)
        snprintf(filename, sizeof(filename), "%s/conversation_%s.txt", conv_dir, target);
    else {
        const char *user1 = strcmp(sender, target) < 0 ? sender : target;
        const char *user2 = strcmp(sender, target) < 0 ? target : sender;
        snprintf(filename, sizeof(filename), "%s/conversation_%s_%s.txt", conv_dir, user1, user2);
    }

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
    
    // Lấy đường dẫn đến thư mục conversation đúng
    const char* conv_dir = get_conversation_dir();
    
    char filename[PATH_MAX];
    if (isGroup)
        snprintf(filename, sizeof(filename), "%s/conversation_%s.txt", conv_dir, target);
    else {
        const char *user1 = strcmp(sender, target) < 0 ? sender : target;
        const char *user2 = strcmp(sender, target) < 0 ? target : sender;
        snprintf(filename, sizeof(filename), "%s/conversation_%s_%s.txt", conv_dir, user1, user2);
    }
    log_event("Attempting to read conversation file: %s", filename);

    FILE *f = fopen(filename, "r");
    if (!f) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[Server] No conversation history with %s.\n", target);
        if (send(sock, msg, strlen(msg), 0) < 0) {
            log_event("[ERROR] Failed to send no history message to socket %d: %s", sock, strerror(errno));
            fprintf(stderr, "[ERROR] Failed to send no history message to socket %d: %s\n", sock, strerror(errno));
        }
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
        if (send(sock, line, strlen(line), 0) < 0 || send(sock, "\n", 1, 0) < 0) {
            log_event("[ERROR] Failed to send history line to socket %d: %s", sock, strerror(errno));
            fprintf(stderr, "[ERROR] Failed to send history line to socket %d: %s\n", sock, strerror(errno));
            break;
        }
        lines_sent++;
    }

    if (lines_sent == 0) {
        if (send(sock, "No messages found.\n", 19, 0) < 0) {
            log_event("[ERROR] Failed to send no messages message to socket %d: %s", sock, strerror(errno));
            fprintf(stderr, "[ERROR] Failed to send no messages message to socket %d: %s\n", sock, strerror(errno));
        }
    }

    if (fclose(f) != 0) {
        log_event("[ERROR] Failed to close file %s: %s", filename, strerror(errno));
        fprintf(stderr, "[ERROR] Failed to close file %s: %s\n", filename, strerror(errno));
    }
    log_event("Sent conversation history for %s to socket %d (lines sent: %d)", target, sock, lines_sent);
    pthread_mutex_unlock(&file_mutex);
}
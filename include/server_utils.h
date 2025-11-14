#ifndef SERVER_UTILS_H
#define SERVER_UTILS_H

#include <stdio.h>
#include <errno.h>
#include <pthread.h>

typedef struct {
    char username[32];
    char password[32];
} User;

typedef struct {
    char groupId[32];
    char groupName[64];
    char members[256];
} Group;

typedef struct {
    int socket;
    char username[32];
} Client;

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

extern User users[100];
extern Group groups[50];
extern int userCount;
extern int groupCount;
extern FILE *logFile;
extern pthread_mutex_t file_mutex;

// Client management
extern Client clients[MAX_CLIENTS];
extern int clientCount;
extern pthread_mutex_t clients_mutex; // Mutex để đồng bộ hóa truy cập vào clients

void load_users();
void load_groups();
void log_event(const char *fmt, ...);
int is_user_in_group(const char *groupId, const char *username);
int is_group_id(const char *groupId);  // Kiểm tra xem groupId có tồn tại không
void save_conversation(const char *sender, const char *target, const char *msg, int isGroup);
void send_conversation_history(int sock, const char *sender, const char *target, int isGroup);

// Client management functions
Client *find_client_by_name(const char *username);
void remove_client(int socket);
int check_login(const char *username, const char *password);

// Message sending functions
void broadcast(const char *sender, const char *msg);
void send_private(const char *sender, const char *target, const char *msg);
void send_group_message(const char *sender, const char *groupId, const char *msg);
void show_menu(int sock);
void show_users(int sock);
void show_groups_for_user(int sock, const char *username);

// Utility functions
int send_message_safe(int sock, const char *msg, const char *error_context);
void get_conversation_filename(char *filename, size_t size, const char *sender, const char *target, int isGroup);

#endif
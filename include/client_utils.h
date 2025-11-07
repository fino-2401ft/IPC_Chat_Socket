#ifndef CLIENT_UTILS_H
#define CLIENT_UTILS_H

#define BUFFER_SIZE 1024

// Biến global để track chế độ chat
extern char current_chat_target[32];
extern int in_chat_mode;

void print_menu();
void clear_screen();
void show_chat_header(const char *target);
void handle_server_message(int sock);
void handle_user_input(int sock, const char *username);

#endif
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <sqlite3.h>
#include <getopt.h>

#include "proto.h"
#include "messages.h"
#include "chat.h"
#define MAX_CLIENTS 100
static int uid = 10;
const char *HELP_STR = "Usage: chat_server [-p PORT] [-r ROOT_PASSWORD]. Default port is 1337. Default root password is 12345.";

// the thread function
void *connection_handler(void *);

void parse_opts(int argc, char **argv, int *port, char **root_password)
{
    struct option opts_list[] = {
        {"port", required_argument, NULL, 'p'},
        {"root-password", required_argument, NULL, 'r'},
        {"help", no_argument, NULL, 'h'},
        {"clean", no_argument, NULL, 'c'},
        {0, 0, 0, 0}};
    char opts_string[] = "p:r:hc";
    int option_index = 0;
    while (1)
    {
        int res = getopt_long(argc, argv, opts_string, opts_list, &option_index);
        if (res == -1)
        {
            return;
        }
        switch (res)
        {
        case 'p':
            sscanf(optarg, "%d", port);
            break;
        case 'r':
            if (strlen(optarg) >= 32 || strlen(optarg) < 2)
            {
                puts("Password length must be between 2 and 31.");
                exit(1);
            }
            *root_password = optarg;
            break;
        case 'h':
            puts(HELP_STR);
            exit(0);
        case 'c':
            chat_clean();
            break;
        default:
            puts("");
            puts(HELP_STR);
            exit(1);
        }
    }
}

int main(int argc, char **argv)
{
    int desc;
    int port = 1337;
    char *root_password = "12345";
    parse_opts(argc, argv, &port, &root_password);
    struct sockaddr_in addr;
    desc = socket(AF_INET, SOCK_STREAM, 0);
    if (desc == -1)
    {
        error(1, 0, "Failed to create socket");
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(desc, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        error(1, errno, "Failed to bind to the port");
    }
    listen(desc, 32);
    puts("Initializing...");
    chat_init(root_password);
    puts("Waiting for clients");
    socklen_t c = sizeof(struct sockaddr_in);
    int client_desc;
    struct sockaddr_in client_addr;
    struct sockaddr_in addr_room;
    int desc_room = socket(AF_INET, SOCK_STREAM, 0);
    if (desc_room == -1)
    {
        error(1, 0, "Failed to create socket");
    }
    addr_room.sin_family = AF_INET;
    addr_room.sin_addr.s_addr = INADDR_ANY;
    addr_room.sin_port = htons(1339);
    if (bind(desc_room, (struct sockaddr *)&addr_room, sizeof(addr_room)) < 0)
    {
        error(1, errno, "Failed to bind to the port");
    }
    listen(desc_room, 32);
    // socklen_t c = sizeof(struct sockaddr_in);
    // int client_desc;
    // struct sockaddr_in client_addr;
    while (1)
    {
        client_desc = accept(desc, (struct sockaddr *)&client_addr, &c);
        if (client_desc < 0)
        {
            puts("Failed to accept a connection");
            sqlite3_sleep(100);
            continue;
        }
        puts("Connection accepted");
        pthread_t handler;
        int *param = malloc(sizeof(int) * 2);
        *param = client_desc;
        *(param + 1) = desc_room;

        if (pthread_create(&handler, 0, connection_handler, param) < 0)
        {
            error(1, errno, "Failed to create thread");
        }
        pthread_detach(handler);
    }
    return 0;
}

// struct thread_room
// {
//     int sock;
//     char room[32];
// };
struct thread_data
{
    int id;
    char *login;
    int sock;
    char room[32];
};
struct thread_data *room_list[200];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void queue_add(struct thread_data *data)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (!room_list[i])
        {
            room_list[i] = data;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}
void queue_remove(int id)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (room_list[i]->id == id)
        {
            room_list[i] = NULL;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

void *message_watcher(void *param)
{
    int sock = ((struct thread_data *)param)->sock;
    char *login = ((struct thread_data *)param)->login;
    char *room = ((struct thread_data *)param)->room;
    char cur_room[128];
    strcpy(cur_room, room);
    long long cur = chat_last_message();
    while (1)
    {
        if (strlen(cur_room) == 0)
        {
            printf("no room\n");
            break;
        }
        pthread_testcancel();
        // sqlite3_sleep(100);
        pthread_testcancel();
        long long t = chat_last_message();
        if (t > cur)
        {
            printf("Watcher %s %s %d\n", login, cur_room, sock);
            chat_send_all(login, cur, t, sock, cur_room);
            cur = t;
        }
    }
    return param;
}

void *connection_handler(void *param)
{
    puts("Client thread created");
    int *sock_list = (int *)param;
    int sock = *sock_list;
    int desc_room = *(sock_list + 1);
    free(param);
    char *buf = malloc(MESSAGE_BUF_SIZE);
    char *login = 0;
    pthread_t watcher = 0;
    signal(SIGPIPE, SIG_IGN);
    while (1)
    {
        int read = proto_recv_packet(buf, sock);

        if (read <= 0)
        {
            shutdown(sock, 2);
            puts("Client thread finished");
            break; // done reading
        }
        struct proto_message *msg = proto_decode(buf, read);
        if (!msg)
        {
            message_send_status(STATUS_INVALID_MESSAGE, sock);
            shutdown(sock, 2);
            puts("Broken message");
            break;
        }
        switch (proto_get_type(msg)) // type từ client
        {
        case 'i': // login
            // if (login)
            // {
            //     if (watcher)
            //     {
            //         pthread_cancel(watcher);
            //         pthread_join(watcher, 0);
            //         watcher = 0;
            //     }
            //     message_log_out(login);
            //     free(login);
            // }
            login = message_login(msg, sock);
            if (login)
            {
                printf("%s logged in\n", login);
            }
            break;

        case 'd':
            message_room_add(login, msg, sock);
            break;
        case 'j':
            message_room_list(login, sock);
            break;
        case 'v':
        {
            // room = message_joined_in(login, msg);
            socklen_t c = sizeof(struct sockaddr_in);
            int sock_room;
            struct sockaddr_in client_addr;
            char room[32];
            int i = 0;
            while (1)
            {

                sock_room = accept(desc_room, (struct sockaddr *)&client_addr, &c);
                if (sock_room < 0)
                {
                    puts("Failed to accept a connection");
                    sqlite3_sleep(100);
                    continue;
                }
                puts("Connection accepted child");
                signal(SIGPIPE, SIG_IGN);
                struct thread_data *data = (struct thread_data *)malloc(sizeof(struct thread_data));
                data->login = login;
                data->sock = sock_room;
                data->id = uid++;
                if (i > 0)
                {
                }
                strcpy(room, message_joined_in(login, msg));
                strcpy(data->room, room);
                queue_add(data);
                printf("You joined as %s", room);
                pthread_create(&watcher, 0, message_watcher, data);
                while (1)
                {
                    int read = proto_recv_packet(buf, sock_room);

                    if (read <= 0)
                    {
                        shutdown(sock_room, 2);
                        puts("Client thread finished");
                        break; // done reading
                    }
                    struct proto_message *msg_child = proto_decode(buf, read);
                    if (!msg_child)
                    {
                        message_send_status(STATUS_INVALID_MESSAGE, sock_room);
                        shutdown(sock_room, 2);
                        puts("Broken message");
                        break;
                    }
                    if (proto_get_type(msg_child) == 'v')
                    {
                        if (read <= 0)
                        {
                            shutdown(sock, 2);
                            puts("Client thread finished");
                            break; // done reading
                        }
                        msg = proto_decode(buf, read);

                        break;
                    }
                    switch (proto_get_type(msg_child)) // type từ client
                    {
                    case 'x':
                        message_room_leader(login, msg_child, sock_room);
                        break;
                    case 'h':
                    {
                        message_history(login, msg_child, sock_room);
                    }
                    break;
                    case 'l':
                        message_list(login, sock_room);
                        break;
                    case 'r':
                        message_receive(login, msg_child, sock_room);
                        break;

                    case 'k':
                        message_kick(login, msg_child, sock_room, room);
                        break;
                    case 'p':
                        message_private_chat(login, msg_child, sock_room, room);
                        break;
                    case 'o': // joined out
                        message_join_out(login, sock_room, room);
                        // free(room);
                        // room = 0;
                        memset(room, 0, strlen(room));
                        break;
                    case 'w':
                        message_invite_user(login, msg_child, sock_room, room);
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        break;

        default:
            message_send_status(STATUS_UNKNOWN_TYPE, sock);
        }
        proto_free(msg);
    }
    // printf("Room out :%s", room);
    if (login)
    {
        if (watcher)
        {
            pthread_cancel(watcher);
            pthread_join(watcher, 0);
            watcher = 0;
        }
        if (*login)
            message_log_out(login);
        free(login);
    }
    free(buf);
    return 0;
}

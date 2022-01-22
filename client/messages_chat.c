#define _GNU_SOURCE

#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include "proto.h"
#include "messages.h"
int sock;
char message_buf[MESSAGE_BUF_SIZE];

static void _send(struct proto_message *p)
{
    char *buf;
    unsigned len = proto_encode(p, &buf);
    proto_free(p);
    send(sock, buf, len, 0);
    printf("Sock send:%d",sock);
    free(buf);
}

static int resolve_host(const char *host)
{
    int s;
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "http", &hints, &result))
        return 0;
    s = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(result);
    return s;
}

char *message_connect(const char *ip, int port) // connect to server
{
    struct sockaddr_in addr;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        return "Cannot create socket";
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (!(addr.sin_addr.s_addr = resolve_host(ip)))
    {
        return "Invalid host";
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr)) < 0)
    {
        shutdown(sock, 2);
        return "Unable to connect";
    }
    return 0;
}

char *message_do_login(const char *login, const char *password)
{
    struct proto_message *p = proto_create('i', 2); // goi tin co type la i(login)
    proto_set_str(p, 0, login);                     // user
    proto_set_str(p, 1, password);                  // password
    _send(p);
    unsigned len = proto_recv_packet(message_buf, sock);
    if (len <= 0)
    {
        return "Connection failed";
    }
    p = proto_decode(message_buf, len);
    if (!p || proto_get_type(p) != 's' || proto_get_line_count(p) < 1 || proto_get_len(p, 0) != 4)
    {
        return "Unknown error";
    }
    int t = proto_get_int(p, 0);
    proto_free(p);
    switch (t)
    {
    case STATUS_OK:
        return 0;
    case STATUS_SIGNUP_ERROR:
        return "Signup error";
    case STATUS_AUTH_ERROR:
        return "Incorrect password";
    default:
        return "Unknown error";
    }
}

void message_do_logout()
{
    struct proto_message *p = proto_create('o', 0);
    _send(p);
    proto_recv_packet(message_buf, sock);
}

void message_send(const char *msg, char *room)
{
    struct proto_message *p = proto_create('r', 2);
    proto_set_str(p, 0, msg);
    proto_set_str(p, 1, room);
    _send(p);
}

int message_receive(struct timeval *time, char **author, char **body)
{
    int len = proto_recv_packet(message_buf, sock);
    if (len <= 0)
        return -1;
    struct proto_message *p = proto_decode(message_buf, len);
    if (!p)
        return -1;
    int tp = proto_get_type(p);
    printf("Message Receive:%d\n", tp);
    if (tp == 'r' || tp == 'h')
    {
        proto_get_timeval(p, 0, time);
        char *s1 = proto_get_str(p, 1);
        char *s2 = proto_get_str(p, 2);
        *author = malloc(strlen(s1) + 1);
        strcpy(*author, s1);
        *body = malloc(strlen(s2) + 1);
        strcpy(*body, s2);
    }
    else if (tp == 'm')
    {
        proto_get_timeval(p, 0, time);
        char *s1 = proto_get_str(p, 1);
        *author = malloc(1);
        **author = 0;
        *body = malloc(strlen(s1) + 1);
        strcpy(*body, s1);
    }
    else if (tp == 'l')
    {
        int cnt = proto_get_line_count(p) / 2;
        int len = 20 + cnt * 50;
        *body = malloc(len);
        strcpy(*body, "");
        for (int i = 0; i < cnt; ++i)
        {
            int uid = proto_get_int(p, i * 2);
            char *login = proto_get_str(p, i * 2 + 1);
            if (strlen(login) > 32)
                continue;
            sprintf(message_buf, "%s%s (%d)", (i ? ", " : ""), login, uid);
            strcat(*body, message_buf);
        }
        *author = malloc(1);
        **author = 0;
        time->tv_sec = 0;
    }
    else if (tp == 's' && proto_get_line_count(p) == 1 && proto_get_int(p, 0) == STATUS_OK)
    {
        *author = 0;
        *body = "Sent successfully";
    }
    else if (tp == 's' && proto_get_line_count(p) == 1 && proto_get_int(p, 0) == STATUS_ACCESS_DENIED)
    {
        *author = 0;
        *body = "Access denied";
    }
    else if (tp == 's' && proto_get_line_count(p) == 1 && proto_get_int(p, 0) == STATUS_NO_SUCH_USER)
    {
        *author = 0;
        *body = "No such user";
    }
    else if (tp == 'k')
    {
        char *reason = proto_get_str(p, 0);
        *author = malloc(1);
        **author = 0;
        *body = malloc(300);
        strcpy(*body, "You have been kicked (reason: ");
        strncat(*body, reason, 256);
        strcat(*body, ")");
        time->tv_sec = 0;
    }
    else if (tp == 'p')
    {
        puts("Private");
        proto_get_timeval(p, 0, time);
        char *s1 = proto_get_str(p, 1);
        char *s2 = proto_get_str(p, 2);
        *author = malloc(strlen(s1) + 1);
        strcpy(*author, s1);
        *body = malloc(300);
        strcpy(*body, s2);
        char from[32];
        char content[128];
        sscanf(*body, "%[^|]|%[^\n]", from, content);
        sprintf(*body, "You received a message from %s (content: %s )", from, content);
        // strcpy(*body, "You have been received content: ");
        // strncat(*body, content, 256);
    }
    else if (tp == 'w')
    {
        char *s2 = proto_get_str(p, 2);
        *body = malloc(300);
        strcpy(*body, s2);
        puts(*body);

        // sprintf(*body, "You received a message from %s (content: %s )", from, content);
        // strcpy(*body, "You have been received content: ");
        // strncat(*body, content, 256);
        return tp;
    }
    else
    {
        tp = 0;
    }
    proto_free(p);
    return tp;
}

void message_request_history(int cnt, char *room)
{
    struct proto_message *p = proto_create('h', 2);
    proto_set_int(p, 0, cnt);
    proto_set_str(p, 1, room);
    _send(p);
}

void message_request_list()
{
    struct proto_message *p = proto_create('l', 0);
    _send(p);
}
char *message_add_room(const char *room_name)
{
    struct proto_message *p = proto_create('d', 1);
    proto_set_str(p, 0, room_name);
    _send(p);
    unsigned len = proto_recv_packet(message_buf, sock);
    if (len <= 0)
    {
        return "Connection failed";
    }
    p = proto_decode(message_buf, len);
    if (!p || proto_get_type(p) != 's' || proto_get_line_count(p) < 1 || proto_get_len(p, 0) != 4)
    {
        return "Unknown error";
    }
    int t = proto_get_int(p, 0);
    proto_free(p);
    switch (t)
    {
    case STATUS_OK:
        return "Added Successfully";
    case STATUS_SIGNUP_ERROR:
        return "Room Add error";
    case STATUS_AUTH_ERROR:
        return "Incorrect password";
    default:
        return "Unknown error";
    }
}
char *message_room_leader(const char *room_name)
{
    struct proto_message *p = proto_create('x', 1);
    proto_set_str(p, 0, room_name);
    _send(p);
    unsigned len = proto_recv_packet(message_buf, sock);
    if (len <= 0)
    {
        return "Connection failed";
    }
    p = proto_decode(message_buf, len);
    // if (!p || proto_get_line_count(p) < 1)
    // {
    //     return "Unknown error";
    // }
    char *creator = proto_get_str(p, 0);
    puts(creator);
    // *leader=malloc(300);
    // strcpy(*leader,creator);
    return creator;
    proto_free(p);
}
char *message_invite_user(int uid)
{
    struct proto_message *p = proto_create('w', 1);
    proto_set_int(p, 0, uid);
    _send(p);
    unsigned len = proto_recv_packet(message_buf, sock);
    if (len <= 0)
    {
        return "Connection failed";
    }
    p = proto_decode(message_buf, len);
    if (!p || proto_get_type(p) != 's' || proto_get_line_count(p) < 1 || proto_get_len(p, 0) != 4)
    {
        return "Unknown error";
    }
    int t = proto_get_int(p, 0);
    proto_free(p);
    switch (t)
    {
    case STATUS_OK:
        return "Invited Successfully";
    case STATUS_SIGNUP_ERROR:
        return "Room Add error";
    case STATUS_AUTH_ERROR:
        return "Incorrect password";
    default:
        return "Unknown error";
    }
}

void message_kick_user(int uid, const char *reason)
{
    struct proto_message *p = proto_create('k', 2);
    proto_set_int(p, 0, uid);
    proto_set_str(p, 1, reason);
    _send(p);
}
void message_private_user(int uid, const char *content)
{
    struct proto_message *p = proto_create('p', 2);
    proto_set_int(p, 0, uid);
    proto_set_str(p, 1, content);
    _send(p);
}

// room
void message_room_list()
{
    struct proto_message *p = proto_create('j', 0);
    _send(p);
}
int message_room_receive(char **name_list)
{
    int len = proto_recv_packet(message_buf, sock);
    if (len <= 0)
        return -1;
    struct proto_message *p = proto_decode(message_buf, len);
    if (!p)
        return -1;
    int tp = proto_get_type(p);
    if (tp == 'j')
    {
        int cnt = proto_get_line_count(p) / 2;
        int len = 20 + cnt * 50;
        *name_list = malloc(len);
        strcpy(*name_list, "");
        for (int i = 0; i < cnt; ++i)
        {
            int uid = proto_get_int(p, i * 2);
            char *name = proto_get_str(p, i * 2 + 1);
            if (strlen(name) > 32)
                continue;
            sprintf(message_buf, "%s|", name);
            strcat(*name_list, message_buf);
        }
    }
    else if (tp == 'w')
    {
        *name_list = malloc(30);
        char *body = proto_get_str(p, 2);
        strcpy(*name_list, body);
        return tp;
    }
    else
    {
        tp = 0;
    }
    proto_free(p);
    return tp;
}
int message_receive_invite(char *content)
{
    int len = proto_recv_packet(message_buf, sock);
    printf("Len Recei:%d\n\n", len);
    if (len <= 0)
        return -1;
    struct proto_message *p = proto_decode(message_buf, len);
    if (!p)
        return -1;
    int tp = proto_get_type(p);
    content = malloc(32);
    printf("Message Receive:%d\n", tp);
    if (tp == 'w')
    {
        char *body = proto_get_str(p, 2);
        strcpy(content, body);
        return tp;
    }
    proto_free(p);
    return 0;
}
void message_joined_in(char *room)
{
    struct proto_message *p = proto_create('v', 1);
    puts(room); // goi tin co type la i(login)
    proto_set_str(p, 0, room);                      // user
    _send(p);
}
void message_disconnect()
{
    shutdown(sock, 2);
}

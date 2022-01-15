#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include "messages.h"
#include "proto.h"
#include "chat.h"

static void _send(struct proto_message *p, int sock)
{
    char *buf;
    unsigned len = proto_encode(p, &buf);
    proto_free(p);
    send(sock, buf, len, 0);
    free(buf);
}

void message_send_status(int status, int sock)
{
    struct proto_message *p = proto_create('s', 1);
    proto_set_int(p, 0, status);
    _send(p, sock);
}

char *message_login(struct proto_message *m, int sock)
{
    if (proto_get_line_count(m) < 2)
    {
        message_send_status(STATUS_INVALID_MESSAGE, sock);
        return 0;
    }
    char *login = proto_get_str(m, 0);
    char *password = proto_get_str(m, 1);
    if (!chat_validate_login(login))
    {
        message_send_status(STATUS_SIGNUP_ERROR, sock);
        return 0;
    }
    char *db_password = chat_get_user_password(login);
    if (!db_password)
    {
        if (strlen(password) < 2 || strlen(password) > 31)
        {
            message_send_status(STATUS_SIGNUP_ERROR, sock);
            return 0;
        }
        chat_create_user(login, password);
        printf("Created user %s\n", login);
    }
    else if (strcmp(password, db_password))
    {
        free(db_password);
        message_send_status(STATUS_AUTH_ERROR, sock);
        return 0;
    }
    free(db_password);
    message_send_status(STATUS_OK, sock);
    chat_create_session(login);
    char *t = malloc(strlen(login) + 1);
    strcpy(t, login);
    return t;
}
char *message_joined_in(char *login, struct proto_message *m)
{
    char *room = proto_get_str(m, 0);
    printf("Server:%s\n", room);
    char buf[64];
    strcpy(buf, login);
    strcat(buf, " joined in");
    chat_new_message("m", "", buf, room);
    char *new_room = malloc(strlen(room) + 1);
    strcpy(new_room, room);
    return room;
}
void message_do_join_out(char *login, char *room)
{
    // chat_delete_session(login);
    printf("%s joined out\n", login);
    char buf[64];
    strcpy(buf, login);
    strcat(buf, " joined out");
    chat_new_message("m", "", buf, room); // nortification message
}

void message_join_out(char *login, int sock, char *room)
{
    if (login)
    {
        message_do_join_out(login, room);
        message_send_status(STATUS_OK, sock);
    }
    else
    {
        message_send_status(STATUS_LOGIN_REQUIRED, sock);
    }
}

void message_receive(char *login, struct proto_message *m, int sock)
{
    if (proto_get_line_count(m) < 2)
    {
        message_send_status(STATUS_INVALID_MESSAGE, sock);
        return;
    }
    if (!login)
    {
        message_send_status(STATUS_LOGIN_REQUIRED, sock);
        return;
    }
    char *line = proto_get_str(m, 0);
    char *room = proto_get_str(m, 1);
    printf("Message from %s: %s in %s\n", login, line, room);
    chat_new_message("r", login, line, room); // r là tin nhắn message
}

void message_send(char kind, struct timeval time, const char *login, const char *body, int sock)
{
    if (kind == 'r' || kind == 'h')
    {
        struct proto_message *p = proto_create(kind, 3);
        proto_set_timeval(p, 0, &time);
        proto_set_str(p, 1, login);
        proto_set_str(p, 2, body);
        _send(p, sock);
    }
    if (kind == 'm')
    {
        struct proto_message *p = proto_create('m', 2);
        proto_set_timeval(p, 0, &time);
        proto_set_str(p, 1, body);
        _send(p, sock);
    }
    if (kind == 'k')
    {
        struct proto_message *p = proto_create('k', 1);
        proto_set_str(p, 0, body);
        _send(p, sock);
        // shutdown(sock, 2);
    }
    if (kind == 'p')
    {
        struct proto_message *p = proto_create('p', 3);
        proto_set_timeval(p, 0, &time);
        proto_set_str(p, 1, login);
        proto_set_str(p, 2, body);
        _send(p, sock);
        // shutdown(sock, 2);
    }
}

void message_history(char *login, struct proto_message *m, int sock)
{
    if (proto_get_len(m, 0) != 4)
    {
        message_send_status(STATUS_INVALID_MESSAGE, sock);
        return;
    }
    if (!login)
    {
        message_send_status(STATUS_LOGIN_REQUIRED, sock);
        return;
    }
    int cnt = proto_get_int(m, 0);
    char *room = proto_get_str(m, 1);
    printf("His%s\n", room);
    chat_send_history(cnt, sock, room);
    printf("Sent %d old messages to %s in  %s\n", cnt, login, room);
}

void message_list(char *login, int sock)
{
    if (!login)
    {
        message_send_status(STATUS_LOGIN_REQUIRED, sock);
        return;
    }
    struct chat_user_list *s, *t;
    int len = chat_get_user_list(&s);
    t = s;
    struct proto_message *p = proto_create('l', 2 * len);
    for (int i = 0; s; ++i, s = s->next)
    {
        proto_set_int(p, 2 * i, (int)s->uid);
        proto_set_str(p, 2 * i + 1, s->login);
    }
    chat_free_user_list(t);
    _send(p, sock);
    printf("Sent user list to %s\n", login);
}
void message_log_out(char *login){
    chat_delete_session(login);
}
void message_kick(char *login, struct proto_message *m, int sock, char *room)
{
    if (proto_get_line_count(m) < 2 || proto_get_len(m, 0) != 4)
    {
        message_send_status(STATUS_INVALID_MESSAGE, sock);
        return;
    }
    if (!login)
    {
        message_send_status(STATUS_LOGIN_REQUIRED, sock);
        return;
    }
    // if (strcmp(login, "root"))
    // {
    //     printf("%s tried to kick\n", login);
    //     message_send_status(STATUS_ACCESS_DENIED, sock);
    //     return;
    // }
    int uid = proto_get_int(m, 0);
    char *reason = proto_get_str(m, 1);
    if (chat_kick_user(uid, reason, room))
    {
        printf("Kicked user %d: %s\n", uid, reason);
    }
    else
    {
        message_send_status(STATUS_NO_SUCH_USER, sock);
        printf("Tried to kick %d: %s\n", uid, reason);
    }
}
void message_private_chat(char *login, struct proto_message *m, int sock,char *room)
{
    if (proto_get_line_count(m) < 2 || proto_get_len(m, 0) != 4)
    {
        message_send_status(STATUS_INVALID_MESSAGE, sock);
        return;
    }
    if (!login)
    {
        message_send_status(STATUS_LOGIN_REQUIRED, sock);
        return;
    }
    // if(strcmp(login, "root"))
    // {
    //     printf("%s tried to kick\n", login);
    //     message_send_status(STATUS_ACCESS_DENIED, sock);
    //     return;
    // }
    int uid = proto_get_int(m, 0);
    char *body = proto_get_str(m, 1);
    if (chat_private_user(login, uid, body,room))
    {
        printf("Sent to user %d: %s in %s\n", uid, body,room);
        message_send_status(STATUS_OK, sock);
    }
    else
    {
        message_send_status(STATUS_NO_SUCH_USER, sock);
        printf("Tried to send %d: %s\n", uid, body);
    }
}
void message_room_add(char *login, struct proto_message *m, int sock)
{
    printf("ROOM_ADD:%s %s", login, m->lines[0].data);
    if (proto_get_line_count(m) < 1)
    {
        message_send_status(STATUS_INVALID_MESSAGE, sock);
        return;
    }
    if (!login)
    {
        message_send_status(STATUS_LOGIN_REQUIRED, sock);
        return;
    }
    char *room_name = proto_get_str(m, 0);
    if (create_room(login, room_name))
    {
        printf(" %s added room %s\n", login, room_name);
        message_send_status(STATUS_OK, sock);
    }
    else
    {
        message_send_status(STATUS_SIGNUP_ERROR, sock);
        printf("\nAdd Error\n");
    }
}
void message_room_list(char *login, int sock)
{
    if (!login)
    {
        message_send_status(STATUS_LOGIN_REQUIRED, sock);
        return;
    }
    struct room_list *s, *t;
    int len = chat_get_room_list(&s);
    if (len == 0)
    {
        message_send_status(STATUS_ADD_ROOM_ERROR, sock);
    }
    t = s;
    struct proto_message *p = proto_create('j', 2 * len);
    for (int i = 0; s; ++i, s = s->next)
    {
        proto_set_int(p, 2 * i, (int)s->uid);
        proto_set_str(p, 2 * i + 1, s->name);
    }
    chat_free_room_list(t);
    _send(p, sock);
    printf("Sent user list to %s\n", login);
}
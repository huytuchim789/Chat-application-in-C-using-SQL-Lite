#ifndef _MESSGES_H
#define _MESSAGES_H

#include "proto.h"

void message_send_status(int status, int sock);
char *message_login(struct proto_message *m, int sock);
void message_do_join_out(char *login,char *room);
void message_join_out(char *login, int sock,char *room);
void message_receive(char *login, struct proto_message *m, int sock);
void message_send(char kind, struct timeval time, const char *login, const char *body, int sock);
void message_history(char *login, struct proto_message *m, int sock);
void message_list(char *login, int sock);
void message_kick(char *login, struct proto_message *m, int sock,char *room);
void message_private_chat(char *login, struct proto_message *m, int sock);
void message_room_add(char *login, struct proto_message *m, int sock);
void message_room_list(char *login, int sock);
char *message_joined_in(char *login,struct proto_message *m);
#endif

#include <sys/time.h>
char *message_connect(const char *ip, int port);
char *message_do_login(const char *login, const char *password);
void message_do_logout();
void message_send(const char *msg, char *room);
int message_receive(struct timeval *time, char **author, char **body);
void message_request_history(int cnt, char *room);
void message_request_list();
void message_kick_user(int uid, const char *reason);
void message_private_user(int uid, const char *reason);
char *message_invite_user(int uid);
int message_receive_invite(char *content);
void message_disconnect();
char *message_add_room(const char *room_name);
int message_room_receive(char **name);
void message_room_list();
void message_joined_in();
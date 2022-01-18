#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <sqlite3.h>
#include <pthread.h>
#include "chat.h"
#include "messages.h"

#if 0
#define DEBUG_LOCK printf("%s: lock\n", __func__);
#define DEBUG_UNLOCK printf("%s: unlock\n", __func__);
#else
#define DEBUG_LOCK
#define DEBUG_UNLOCK
#endif

static sqlite3 *db;
long long last_msg_id;
static pthread_mutex_t msg_mutex = PTHREAD_MUTEX_INITIALIZER;

static void _init_last_msg_id()
{
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT MAX(ROWID) FROM messages", -1, &stmt, 0);
    sqlite3_step(stmt);
    last_msg_id = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
}

void chat_init(char *root_password)
{
    if (sqlite3_open("chat.sqlite", &db))
    {
        puts("Cannot open chat.sqlite");
        exit(1);
    }
    char *errmsg;
    if (sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS users(login varchar(32) NOT NULL PRIMARY KEY, password varchar(32) NOT NULL)", 0, 0, &errmsg))
    {
        printf("Database error: %s\n", errmsg);
        sqlite3_close(db);
        exit(1);
    }
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS messages(kind char(1), time int64 NOT NULL, login varchar(32), body text, room varchar(32))", 0, 0, &errmsg);
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS rooms (name varchar(32) NOT NULL PRIMARY KEY,creator varchar(32),foreign key(creator) references users(login))", 0, 0, &errmsg);
    sqlite3_exec(db, "DROP TABLE IF EXISTS sessions", 0, 0, &errmsg);
    sqlite3_exec(db, "CREATE TABLE sessions(login varchar(32) NOT NULL)", 0, 0, &errmsg);
    sqlite3_exec(db, "DELETE FROM users WHERE login='root'", 0, 0, &errmsg);
    _init_last_msg_id();
    chat_create_user("root", root_password);
}

char *chat_get_user_password(char *login)
{
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT password FROM users WHERE login=?", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, login, strlen(login), SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&msg_mutex);
        DEBUG_UNLOCK
        return 0;
    }
    const char *password = (const char *)sqlite3_column_text(stmt, 0);
    char *t = malloc(strlen(password) + 1);
    strcpy(t, password);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&msg_mutex);
    DEBUG_UNLOCK
    return t;
}

int chat_validate_login(char *login)
{
    if (strlen(login) > 31 || strlen(login) < 2)
    {
        return 0;
    }
    for (char *i = login; *i; ++i)
    {
        if (*i < ' ')
        {
            return 0;
        }
    }
    return 1;
}

int chat_create_user(char *login, char *password)
{
    sqlite3_stmt *stmt;
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    sqlite3_prepare_v2(db, "INSERT INTO users VALUES (?, ?)", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, login, strlen(login), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, strlen(password), SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&msg_mutex);
        DEBUG_UNLOCK
        return 0;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&msg_mutex);
    DEBUG_UNLOCK
    return 1;
}

void chat_create_session(char *login)
{
    sqlite3_stmt *stmt;
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    sqlite3_prepare_v2(db, "INSERT INTO sessions(login) VALUES (?)", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, login, strlen(login), SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        puts("Failed to create session");
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&msg_mutex);
    DEBUG_UNLOCK
}

int create_room(char *login, char *room_name)
{
    sqlite3_stmt *stmt;
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    sqlite3_prepare_v2(db, "INSERT INTO rooms(name,creator) VALUES (?,?)", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, room_name, strlen(room_name), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, login, strlen(login), SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        puts("Failed to create session");
        return 0;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&msg_mutex);
    return 1;
    DEBUG_UNLOCK
}
char *room_get_leader(char *login, char *room_name)
{
    sqlite3_stmt *stmt;
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    sqlite3_prepare_v2(db, "SELECT creator from rooms where name=?", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, room_name, strlen(room_name), SQLITE_STATIC);
    int rc;
    char *creator = NULL;
    if ((rc = sqlite3_step(stmt)) != SQLITE_DONE)
    {
        creator = (char *)sqlite3_column_text(stmt, 0);
    }
    char *t = malloc(strlen(creator) + 1);
    strcpy(t, creator);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&msg_mutex);
    DEBUG_UNLOCK
    return t;
}
void chat_delete_session(const char *login)
{
    sqlite3_stmt *stmt;
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    sqlite3_prepare_v2(db, "DELETE FROM sessions WHERE ROWID=(SELECT MIN(ROWID) FROM sessions WHERE login=?)", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, login, strlen(login), SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        puts("Failed to delete session");
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&msg_mutex);
    DEBUG_UNLOCK
}

void chat_new_message(char *kind, const char *login, const char *msg, char *room)
{
    struct timeval tv;
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    gettimeofday(&tv, 0);
    long long time_encoded = ((long long)tv.tv_sec << 32) + tv.tv_usec;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "INSERT INTO messages(kind, time, login, body,room) VALUES (?, ?, ?, ?,?)", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, kind, 1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, time_encoded);
    sqlite3_bind_text(stmt, 3, login, strlen(login), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, msg, strlen(msg), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, room, strlen(room), SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        puts(sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    last_msg_id = sqlite3_last_insert_rowid(db);
    pthread_mutex_unlock(&msg_mutex);
    DEBUG_UNLOCK
}

long long chat_last_message()
{
    long long t = last_msg_id;
    return t;
}

void chat_send_all(char *my_login, long long from, long long to, int sock, char *room)
{
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT * FROM messages WHERE ROWID>? AND ROWID<=? and room =?", -1, &stmt, 0);
    sqlite3_bind_int64(stmt, 1, from);
    sqlite3_bind_int64(stmt, 2, to);
    sqlite3_bind_text(stmt, 3, room, strlen(room), SQLITE_STATIC);

    int rc;
    while ((rc = sqlite3_step(stmt)) != SQLITE_DONE)
    {
        const char *kind = (const char *)sqlite3_column_text(stmt, 0);
        long long t = sqlite3_column_int64(stmt, 1);
        const char *login = (const char *)sqlite3_column_text(stmt, 2);
        const char *body = (const char *)sqlite3_column_text(stmt, 3);
        const char *room = (const char *)sqlite3_column_text(stmt, 4);
        struct timeval tv;
        tv.tv_usec = t << 32 >> 32;
        tv.tv_sec = t >> 32;
        if (kind[0] == 'k')
        {
            if (strcmp(login, my_login))
                continue;
            else
            {
                // my_login[0] = 0;
                // chat_delete_session(login);
            }
        }
        if (kind[0] == 'w')
        {
            if (strcmp(login, my_login))
                continue;
            else
            {
                char body_room[256];
                sprintf(body_room, "%s|%s", body, room);
                puts(body_room);
                message_send(kind[0], tv, login, body_room, sock);
                continue;
            }
        }
        if (kind[0] == 'p')
        {
            if (strcmp(login, my_login))
                continue;
        }
        message_send(kind[0], tv, login, body, sock);
    }
    sqlite3_finalize(stmt);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
}

void chat_send_history(int cnt, int sock, char *room)
{
    sqlite3_stmt *stmt;
    if (cnt > 500)
        cnt = 500;
    sqlite3_prepare_v2(db, "SELECT * FROM (SELECT *, ROWID FROM messages WHERE kind='r'and room =? ORDER BY ROWID DESC LIMIT ?) ORDER BY ROWID ASC", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, room, strlen(room), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, cnt);
    int rc;
    while ((rc = sqlite3_step(stmt)) != SQLITE_DONE)
    {
        long long t = sqlite3_column_int64(stmt, 1);
        const char *login = (const char *)sqlite3_column_text(stmt, 2);
        const char *body = (const char *)sqlite3_column_text(stmt, 3);
        struct timeval tv;
        tv.tv_usec = t << 32 >> 32;
        tv.tv_sec = t >> 32;
        printf("%s %s %s\n", login, body, room);
        message_send('h', tv, login, body, sock);
    }
    sqlite3_finalize(stmt);
}

int chat_get_user_list(struct chat_user_list **st)
{
    struct chat_user_list *s = 0;
    sqlite3_stmt *stmt;
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    sqlite3_prepare_v2(db, "SELECT users.ROWID, users.login FROM users INNER JOIN sessions ON users.login=sessions.login", -1, &stmt, 0);
    int len = 0;
    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ++len;
        struct chat_user_list *t = malloc(sizeof(struct chat_user_list));
        t->uid = sqlite3_column_int64(stmt, 0);
        strcpy(t->login, (const char *)sqlite3_column_text(stmt, 1));
        t->next = s;
        s = t;
    }
    sqlite3_finalize(stmt);
    *st = s;
    pthread_mutex_unlock(&msg_mutex);
    DEBUG_UNLOCK
    return len;
}

void chat_free_user_list(struct chat_user_list *s)
{
    while (s)
    {
        struct chat_user_list *t = s->next;
        free(s);
        s = t;
    }
}

int chat_kick_user(long long uid, const char *reason, char *room)
{
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT login FROM users WHERE ROWID=? AND login IN (SELECT login FROM sessions)", -1, &stmt, 0);
    sqlite3_bind_int64(stmt, 1, uid);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&msg_mutex);
        DEBUG_UNLOCK
        return 0;
    }
    const char *login = (const char *)sqlite3_column_text(stmt, 0);
    pthread_mutex_unlock(&msg_mutex);
    DEBUG_UNLOCK
    chat_new_message("k", login, reason, room);
    char buf[512];
    strcpy(buf, login);
    strcat(buf, " kicked");
    if (*reason)
    {
        strcat(buf, " (reason: ");
        strncat(buf, reason, 256);
        strcat(buf, ")");
    }
    sqlite3_finalize(stmt);
    chat_new_message("m", "", buf, room);
    return 1;
}
int chat_private_user(char *cur_login, long long uid, const char *content, char *room)
{
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    sqlite3_stmt *stmt, *stmt1;
    sqlite3_prepare_v2(db, "SELECT login FROM users WHERE ROWID=? AND login IN (SELECT login FROM sessions)", -1, &stmt, 0);
    sqlite3_bind_int64(stmt, 1, uid);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&msg_mutex);
        DEBUG_UNLOCK
        return 0;
    }
    const char *login = (const char *)sqlite3_column_text(stmt, 0);
    pthread_mutex_unlock(&msg_mutex);
    DEBUG_UNLOCK
    char new_content[128];
    sprintf(new_content, "%s|%s", cur_login, content);
    puts(new_content);
    chat_new_message("p", login, new_content, room);
    char buf[512];
    strcpy(buf, login);
    strcat(buf, " received a message");
    if (*content)
    {
        strcat(buf, " (content: ");
        strncat(buf, content, 256);
        strcat(buf, ")");
    }
    puts(buf);
    sqlite3_finalize(stmt);
    // chat_new_message("m", "", buf);
    return 1;
}
int chat_invite_user(char *cur_login, long long uid, char *room)
{
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    sqlite3_stmt *stmt, *stmt1;
    sqlite3_prepare_v2(db, "SELECT login FROM users WHERE ROWID=? AND login IN (SELECT login FROM sessions)", -1, &stmt, 0);
    sqlite3_bind_int64(stmt, 1, uid);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&msg_mutex);
        DEBUG_UNLOCK
        return 0;
    }
    const char *login = (const char *)sqlite3_column_text(stmt, 0);
    pthread_mutex_unlock(&msg_mutex);
    DEBUG_UNLOCK
    char new_content[128];
    sprintf(new_content, "%s", cur_login);
    puts(new_content);
    chat_new_message("w", login, new_content, room);
    char buf[512];
    strcpy(buf, login);
    strcat(buf, "received an invitation");
    puts(buf);
    sqlite3_finalize(stmt);
    // chat_new_message("m", "", buf);
    return 1;
}
int chat_get_room_list(struct room_list **st)
{
    struct room_list *s = 0;
    sqlite3_stmt *stmt;
    DEBUG_LOCK
    pthread_mutex_lock(&msg_mutex);
    sqlite3_prepare_v2(db, "SELECT rooms.ROWID, rooms.name FROM rooms", -1, &stmt, 0);
    int len = 0;
    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        ++len;
        struct room_list *t = malloc(sizeof(struct room_list));
        t->uid = sqlite3_column_int64(stmt, 0);
        strcpy(t->name, (const char *)sqlite3_column_text(stmt, 1));
        t->next = s;
        s = t;
    }
    sqlite3_finalize(stmt);
    *st = s;
    pthread_mutex_unlock(&msg_mutex);
    DEBUG_UNLOCK
    return len;
}
void chat_free_room_list(struct room_list *s)
{
    while (s)
    {
        struct room_list *t = s->next;
        free(s);
        s = t;
    }
}
// clean
void chat_clean()
{
    unlink("chat.sqlite");
}

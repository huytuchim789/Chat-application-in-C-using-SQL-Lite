#define _GNU_SOURCE

#include "chat.h"
#include "messages_chat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

GtkWidget *chatWindow;
GtkWidget *sendEntry, *sendButton;
GtkWidget *statusLabel;
GtkWidget *welcome;
GtkWidget *messagesTreeView;
GtkWidget *listUserTreeView;
GtkWidget *roomLeader;
GtkAdjustment *vAdjust;
GtkScrolledWindow *scrolledWindow;
GtkListStore *messagesListStore;
GtkListStore *userListStore;
GtkWidget *inviteEntry;
GtkWidget *stack1;
// dialog
GtkWidget *dialog;
GtkWidget *yesButton;
GtkWidget *noButton;
int show_dialog;
char a[20];
#define REQUEST_HISTORY 10

const char *HELP_STR = "\\l: list of active users, \\k ID REASON: kick (root only), \\p ID content:private chat by ID";
void sleep_ms(int milliseconds)
{
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000; // convert to seconds
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL); // suspend thread
}

void add_list_entry(const char *t, const char *a, const char *m, int sleep)
{
    GtkTreeIter iter;                                                                   // pointer tree
    gtk_list_store_append(GTK_LIST_STORE(messagesListStore), &iter);                    // point to a new node
    gtk_list_store_set(GTK_LIST_STORE(messagesListStore), &iter, 0, t, 1, a, 2, m, -1); // set value to new node
    if (sleep)
        sleep_ms(100);
    gtk_adjustment_set_value(vAdjust, gtk_adjustment_get_upper(vAdjust) - gtk_adjustment_get_page_size(vAdjust)); // get scrolled
}
void add_list_user_online(char *body)
{
    gtk_list_store_clear(userListStore);
    char *token = strtok(body, ",");
    // loop through the string to extract all other tokens
    while (token != NULL)
    {
        // printf("IN:%s\n", token);
        GtkTreeIter iter;                                                       // pointer tree
        gtk_list_store_append(GTK_LIST_STORE(userListStore), &iter);            // point to a new node                                                    // printing each token
        gtk_list_store_set(GTK_LIST_STORE(userListStore), &iter, 0, token, -1); // set value to new node
        token = strtok(NULL, ",");
    }
    // gtk_adjustment_set_value(vAdjust, gtk_adjustment_get_upper(vAdjust) - gtk_adjustment_get_page_size(vAdjust)); // get scrolled
}
void do_send(GtkWidget *widget, gpointer *data)
{
    printf("do_send");
    if (!gtk_widget_get_sensitive(sendButton)) // when havent pressed buttton
        return;
    gtk_label_set_text(GTK_LABEL(statusLabel), ""); // default value
    const gchar *message;
    message = gtk_entry_get_text(GTK_ENTRY(sendEntry));
    if (!message || !*message)
        return;
    // default value
    if (message[0] == '\\' && message[1])
    {
        if (message[1] == 'h' && (!message[2] || message[2] == ' ')) // get help
        {
            gtk_label_set_text(GTK_LABEL(statusLabel), HELP_STR);
            gtk_entry_set_text(GTK_ENTRY(sendEntry), "");
            return;
        }
        if (message[1] == 'l' && (!message[2] || message[2] == ' '))
        {
            message_request_list();
            gtk_entry_set_text(GTK_ENTRY(sendEntry), "");
            return;
        }
        if (message[1] == 'k' && message[2] == ' ')
        {
            int uid;
            char num[16];
            int i = 3, j = 0;
            while (message[i] == ' ') // pass \k
                ++i;
            while (j < 15 && message[i] != ' ' && message[i]) // pass \k ID
                num[j++] = message[i++];                      // get the id with max char is 16
            if (j == 15)
            {
                gtk_label_set_text(GTK_LABEL(statusLabel), "Usage: \\k ID REASON");
                gtk_entry_set_text(GTK_ENTRY(sendEntry), "");
                return;
            }
            num[j] = 0;
            printf("%s", num);
            sscanf(num, "%d", &uid); // conver num to int
            while (message[i] == ' ')
                ++i;
            message_kick_user(uid, &message[i]);
            gtk_entry_set_text(GTK_ENTRY(sendEntry), "");
            return;
        }
        if (message[1] == 'p' && message[2] == ' ')
        {
            struct timeval tv;
            struct tm *nowtm;
            char timebuf[64];
            gettimeofday(&tv, 0);
            nowtm = localtime(&tv.tv_sec);
            strftime(timebuf, 64, "[%d.%m.%Y %H:%M:%S]", nowtm);
            int uid;
            char num[40];
            int i = 3, j = 0;
            while (message[i] == ' ') // pass \k
                ++i;
            while (j < 39 && message[i] != ' ' && message[i]) // pass \k ID
                num[j++] = message[i++];                      // get the id with max char is 16
            if (j == 39)
            {
                gtk_label_set_text(GTK_LABEL(statusLabel), "Usage: \\p ID CONTENT");
                gtk_entry_set_text(GTK_ENTRY(sendEntry), "");
                return;
            }
            num[j] = 0;
            printf("%s", num);
            sscanf(num, "%d", &uid); // conver num to int
            while (message[i] == ' ')
                ++i;
            message_private_user(uid, &message[i]);
            char content[64];
            sprintf(content, "Send to %d content: (%s)", uid, &message[i]);
            add_list_entry(timebuf, "", content, 0);
            gtk_entry_set_text(GTK_ENTRY(sendEntry), "");
            return;
        }
        gtk_label_set_text(GTK_LABEL(statusLabel), "Unknown command, type \\h for help");
        gtk_entry_set_text(GTK_ENTRY(sendEntry), "");
        return;
    }
    char *m = malloc(strlen(message) + 1);
    strcpy(m, message);
    gtk_entry_set_text(GTK_ENTRY(sendEntry), "");
    message_send(m, (char *)data);
    free(m);
}
void do_invite()
{
    const gchar *invite_id;
    invite_id = gtk_entry_get_text(GTK_ENTRY(inviteEntry));
    if (!invite_id || !*invite_id)
    {
        gtk_label_set_text(GTK_LABEL(statusLabel), "ID cannot be empty");
        return;
    }
    int uid = atoi(invite_id);
    printf("ID invite:%d\n\n", uid);
    char status[32];
    strcpy(status, message_invite_user(uid));
    gtk_entry_set_text(GTK_ENTRY(inviteEntry), "");
    gtk_label_set_text(GTK_LABEL(statusLabel), status);
}
void *watcher_thread(void *param)
{
    struct timeval tv;
    struct tm *nowtm;
    char *author, *body;
    char timebuf[64];
    printf("His%s\n", (char *)param);
    char *res = message_connect("127.0.0.1", 1339);
    if (res)
    {
        gtk_label_set_text(GTK_LABEL(statusLabel), res);
        message_disconnect();
    }
    char *creator = message_room_leader((char *)param);
    char creatorLabel[256];
    sprintf(creatorLabel, "Room Creator: %s", creator);
    gtk_label_set_text(GTK_LABEL(roomLeader), creatorLabel);
    message_request_history(REQUEST_HISTORY, (char *)param);
    while (1)
    {
        int k = message_receive(&tv, &author, &body);
        if ((char)k == 'l')
        {
            add_list_user_online(body);
            continue;
        }
        if ((char)k == 'k')
        {
            gtk_label_set_text(GTK_LABEL(statusLabel), "You have been blocked and cant send a message");
            gtk_widget_set_sensitive(sendButton, 0);
            gtk_editable_set_editable(GTK_EDITABLE(sendEntry), FALSE);
            gtk_widget_set_can_focus(GTK_WIDGET(sendEntry), FALSE);
            gtk_editable_set_editable(GTK_EDITABLE(inviteEntry), FALSE);
            gtk_widget_set_can_focus(GTK_WIDGET(inviteEntry), FALSE);
            break;
        }
        if ((char)k == 'w')
        {
            gtk_label_set_text(GTK_LABEL(statusLabel), "You received an invitation");
            // gtk_widget_show(dialog);
            char from[32];
            char room[128];
            sscanf(body, "%[^|]|%[^\n]", from, room);
            char content[200];
            sprintf(content, "%s invite you to join %s", from, room);
            show_dialog = 1;
            gtk_window_set_title(GTK_WINDOW(dialog), content);
            continue;
        }
        if (k < 0)
        {
            gtk_label_set_text(GTK_LABEL(statusLabel), "Disconnected, please restart the client");
            gtk_widget_set_sensitive(sendButton, 0);
            break;
        }
        if (k == 0)
            continue;
        if (!author)
        {
            gtk_label_set_text(GTK_LABEL(statusLabel), body);
            continue;
        }
        if (tv.tv_sec)
        {
            nowtm = localtime(&tv.tv_sec);
            strftime(timebuf, 64, "[%d.%m.%Y %H:%M:%S]", nowtm);
        }
        else
        {
            *timebuf = 0;
        }
        add_list_entry(timebuf, author, body, k != 'h');

        free(author);
        free(body);
        // free(param);
    }
    return param;
}
void out_room()
{
    message_do_logout();
}
void yes()
{
    gtk_label_set_text(GTK_LABEL(statusLabel), "You accepted an invitation");
    gtk_widget_hide(dialog);
}
void no()
{
    puts("no");
    gtk_label_set_text(GTK_LABEL(statusLabel), "You denied an invitation");
    gtk_widget_hide(dialog);
}
gboolean check_dialog(void *param)
{

    // if (content)
    // {
    //     free(content);
    // }
    // if (hide_dialog)
    // {
    //     gtk_widget_show(dialog);
    //     hide_dialog = 0;
    // }
    (void)param;
    if (show_dialog)
    {
        // gtk_widget_hide(roomWindow);
        gtk_widget_show(dialog);
        show_dialog = 0;
    }
    return G_SOURCE_CONTINUE;
}
void init_chat_window(char *login, char *room)
{
    GtkBuilder *builder = gtk_builder_new_from_file("./client/chat.glade");
    pthread_t watcher;
    chatWindow = GTK_WIDGET(gtk_builder_get_object(builder, "chatWindow"));
    char buf[100] = "Group chat client: ";
    char wel[30];
    strcat(buf, room);
    sprintf(wel, "Welcome %s!!", login);
    puts(wel);
    gtk_window_set_title(GTK_WINDOW(chatWindow), buf);
    g_signal_connect(chatWindow, "destroy", G_CALLBACK(out_room), NULL);
    sendEntry = GTK_WIDGET(gtk_builder_get_object(builder, "sendEntry"));
    inviteEntry = GTK_WIDGET(gtk_builder_get_object(builder, "inviteEntry"));
    sendButton = GTK_WIDGET(gtk_builder_get_object(builder, "sendButton"));
    stack1 = GTK_WIDGET(gtk_builder_get_object(builder, "stack1"));
    g_signal_connect(G_OBJECT(sendEntry), "activate", G_CALLBACK(do_send), (gpointer *)room);
    g_signal_connect(G_OBJECT(inviteEntry), "activate", G_CALLBACK(do_invite), (gpointer *)room);
    g_signal_connect(G_OBJECT(sendButton), "clicked", G_CALLBACK(do_send), (gpointer *)room);
    statusLabel = GTK_WIDGET(gtk_builder_get_object(builder, "statusLabel"));
    roomLeader = GTK_WIDGET(gtk_builder_get_object(builder, "roomLeader"));
    welcome = GTK_WIDGET(gtk_builder_get_object(builder, "welcome"));
    gtk_label_set_text(GTK_LABEL(welcome), wel);
    messagesTreeView = GTK_WIDGET(gtk_builder_get_object(builder, "messagesTreeView"));
    messagesListStore = GTK_LIST_STORE(gtk_builder_get_object(builder, "messagesListStore"));
    listUserTreeView = GTK_WIDGET(gtk_builder_get_object(builder, "listUserTreeView"));
    userListStore = GTK_LIST_STORE(gtk_builder_get_object(builder, "userListStore"));
    scrolledWindow = GTK_SCROLLED_WINDOW(gtk_builder_get_object(builder, "scrolledWindow"));
    // g_signal_connect(G_OBJECT(listUserTreeView), "row-activated", G_CALLBACK(view_onRowActivated),NULL);

    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "dialog"));
    yesButton = GTK_WIDGET(gtk_builder_get_object(builder, "yesButton"));
    g_signal_connect(G_OBJECT(yesButton), "clicked", G_CALLBACK(yes), NULL);
    noButton = GTK_WIDGET(gtk_builder_get_object(builder, "noButton"));
    g_signal_connect(G_OBJECT(noButton), "clicked", G_CALLBACK(no), NULL);
    vAdjust = gtk_scrolled_window_get_vadjustment(scrolledWindow);
    pthread_create(&watcher, NULL, watcher_thread, (void *)room);

    //stack
    // gtk_stack_set_visible_child_name((GtkStack *)stack1,(void *)room);
    show_dialog = 0;
    g_timeout_add(50, check_dialog, 0);
}

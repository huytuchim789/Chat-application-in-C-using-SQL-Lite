#include "room.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "messages.h"
#include "chat.h"
GtkWidget *roomWindow;
GtkWidget *roomWelcome;
GtkWidget *pickButton;
GtkWidget *roomBox;
GtkWidget *status_label;
GtkWidget *registerEntry;
GtkWidget *roomNameEntry;
GtkListStore *listRoom;
pthread_t roomer;
int room_in;
struct roomer_info
{
    const char *room;
    const char *login;
};
pthread_t room_watcher;
void *room_thread(void *param)
{
    puts(((struct roomer_info *)param)->room);
    message_joined_in(((struct roomer_info *)param)->room);
    // init_chat_window(((struct roomer_info *)param)->login);
    init_chat_window(((struct roomer_info *)param)->login, ((struct roomer_info *)param)->room);
    // gtk_widget_show(chatWindow);
    room_in = 1;
    free(param);
    return param;
}
void add_list_room(char *body)
{
    gtk_list_store_clear(listRoom);
    char *token = strtok(body, "|");
    // loop through the string to extract all other tokens
    while (token != NULL)
    {
        // printf("IN:%s\n", token);
        GtkTreeIter iter; // pointer tree
        gtk_list_store_append(GTK_LIST_STORE(listRoom), &iter);
        printf("IN:%s\n", token);
        // point to a new node
        if (strcmp(token, ""))                                                 // printing each token
            gtk_list_store_set(GTK_LIST_STORE(listRoom), &iter, 0, token, -1); // set value to new node
        token = strtok(NULL, "|");
    }
}
void *room_watcher_thread(void *param)
{
    (void)param;
    message_room_list();
    char *name_list;

    int k = message_room_receive(&name_list);
    if ((char)k == 'j')
    {
        add_list_room(name_list);
    }
    else
    {
        puts("Error in init\n");
    }
    return param;
}
void do_pick(GtkWidget *widget, gpointer *data)
{
    const gchar *new_room;
    new_room = gtk_entry_get_text(GTK_ENTRY(roomNameEntry));
    if (!new_room || !*new_room)
    {
        gtk_label_set_text(GTK_LABEL(status_label), "Room is invalid");
        return;
    }
    struct roomer_info *li = malloc(sizeof(struct roomer_info));
    li->room = (char *)new_room;
    li->login = (char *)data;
    pthread_create(&roomer, 0, room_thread, (void *)li);
}
void do_register()
{
    const gchar *new_room;
    new_room = gtk_entry_get_text(GTK_ENTRY(registerEntry));
    char room[32];
    strcpy(room,(char *)new_room);
    if (!new_room || !*new_room)
    {
        gtk_label_set_text(GTK_LABEL(status_label), "New Room must not be empty");
        return;
    }
    char status[32];
    strcpy(status, message_add_room(new_room));
    gtk_entry_set_text(GTK_ENTRY(registerEntry), "");
    gtk_label_set_text(GTK_LABEL(status_label), status);
    if (strcmp(status, "Added Successfully") == 0)
    {
        GtkTreeIter iter; // pointer tree
        gtk_list_store_append(GTK_LIST_STORE(listRoom), &iter);
        // point to a new node
        gtk_list_store_set(GTK_LIST_STORE(listRoom), &iter, 0, room, -1); // set value to new node
    }
}
gboolean check_room(void *param)
{
    (void)param;
    if (room_in)
    {
        // gtk_widget_hide(roomWindow);
        gtk_widget_show(chatWindow);
        room_in = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
void init_room_window(char *login)
{
    GtkBuilder *builder = gtk_builder_new_from_file("./client/room.glade");
    roomWindow = GTK_WIDGET(gtk_builder_get_object(builder, "roomWindow"));
    char buf[100];
    char wel[30];
    strcpy(buf, login);
    sprintf(wel, "Welcome %s!!", login);
    g_signal_connect(roomWindow, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    pickButton = GTK_WIDGET(gtk_builder_get_object(builder, "pickButton"));
    g_signal_connect(G_OBJECT(pickButton), "clicked", G_CALLBACK(do_pick), (gpointer *)login);
    registerEntry = GTK_WIDGET(gtk_builder_get_object(builder, "registerEntry"));
    g_signal_connect(G_OBJECT(registerEntry), "activate", G_CALLBACK(do_register), NULL);
    roomBox = GTK_WIDGET(gtk_builder_get_object(builder, "roomBox"));
    roomWelcome = GTK_WIDGET(gtk_builder_get_object(builder, "roomWelcome"));
    status_label = GTK_WIDGET(gtk_builder_get_object(builder, "status_label"));
    gtk_label_set_text(GTK_LABEL(roomWelcome), wel);
    listRoom = GTK_LIST_STORE(gtk_builder_get_object(builder, "listRoom"));
    roomNameEntry = GTK_WIDGET(gtk_builder_get_object(builder, "roomNameEntry"));
    // g_signal_connect(G_OBJECT(roomNameEntry), "changed", G_CALLBACK(onchange), NULL);
    pthread_create(&room_watcher, 0, room_watcher_thread, 0);
    room_in = 0;
    g_timeout_add(50, check_room, 0);
}

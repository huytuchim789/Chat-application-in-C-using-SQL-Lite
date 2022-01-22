COMMON_FILES=../proto/proto.c ../proto/aes.c -lmcrypt
CLIENT_FILES= login.c chat.c messages.c room.c client_main.c $(COMMON_FILES)
SERVER_FILES=messages.c chat.c server_main.c $(COMMON_FILES)
GCC_FLAGS= -std=c99 -Wall -Wextra -pedantic -O2 -pthread -I ../proto

all: chatserver chatclient

chatserver:
	cd server; gcc $(GCC_FLAGS)  $(SERVER_FILES) -lsqlite3 -o ../chat_server
chatclient:
	cd client; gcc $(GCC_FLAGS) $(CLIENT_FILES) `pkg-config --cflags --libs gtk+-3.0 gthread-2.0` -o ../chat_client
clean:
	rm chat_client chat_server 

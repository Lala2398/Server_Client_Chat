/*
 * client.c  -  Chat client for the multi-client server.
 *
 * Two things happen at the same time:
 *   - a background thread keeps reading messages from the server and prints them
 *   - the main thread reads your keyboard input and sends it to the server
 *
 * Usage:
 *   ./client            connect to 127.0.0.1 (same machine)
 *   ./client 192.168.0.5  connect to a server on another machine
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h"

static int sock = -1;
static volatile sig_atomic_t running = 1;

/* Background thread: print whatever the server sends us. */
static void *receive_handler(void *arg)
{
    (void)arg;
    char buffer[BUFFER_SIZE];

    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = recv(sock, buffer, BUFFER_SIZE - 1, 0);

        if (n > 0) {
            buffer[n] = '\0';
            /* "\r\033[K" returns to the start of the line and clears it,
               so an incoming message doesn't get tangled up with the
               "> " prompt. Then we reprint the prompt underneath. */
            printf("\r\033[K%s> ", buffer);
            fflush(stdout);
        } else if (n == 0) {          /* server closed the connection      */
            if (running) {            /* stay quiet if WE are shutting down */
                printf("\r\033[KServer closed the connection.\n");
                fflush(stdout);
            }
            running = 0;
            break;
        } else {                      /* read error                        */
            if (running)
                perror("recv");
            running = 0;
            break;
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    /* Optional first argument = server IP address. */
    const char *server_ip = (argc > 1) ? argv[1] : "127.0.0.1";

    struct sockaddr_in serv_addr;
    pthread_t tid;
    char name[NAME_LEN];
    char message[BUFFER_SIZE];

    /* Ask for a username before we connect. */
    printf("Enter your name: ");
    fflush(stdout);
    if (fgets(name, NAME_LEN, stdin) == NULL)
        return 0;
    name[strcspn(name, "\r\n")] = '\0';
    if (name[0] == '\0')
        strcpy(name, "anonymous");

    /* 1. Create the socket. */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(PORT);

    /* 2. Convert the text IP ("127.0.0.1") into binary form. */
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", server_ip);
        exit(EXIT_FAILURE);
    }

    /* 3. Connect to the server. */
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    /* 4. The server expects our username as the first message. */
    send(sock, name, strlen(name), 0);

    printf("Connected to %s:%d. Type a message and press Enter.\n",
           server_ip, PORT);
    printf("Type 'exit' to quit.\n");

    /* 5. Start the receiver thread, then loop on keyboard input. */
    pthread_create(&tid, NULL, receive_handler, NULL);

    while (running) {
        printf("> ");
        fflush(stdout);

        if (fgets(message, BUFFER_SIZE, stdin) == NULL)
            break;
        if (!running)                 /* server vanished while we typed    */
            break;
        message[strcspn(message, "\r\n")] = '\0';

        if (strcmp(message, "exit") == 0)
            break;
        if (message[0] == '\0')
            continue;

        if (send(sock, message, strlen(message), 0) < 0) {
            perror("send");
            break;
        }
    }

    /* 6. Clean shutdown.
     *
     * Subtle but important: close() alone does NOT wake a recv() that the
     * receiver thread is blocked in, so pthread_join() would hang forever.
     * shutdown(SHUT_RDWR) tears down the connection itself, which makes that
     * blocked recv() return 0 (the thread then exits) and sends a FIN so the
     * server notices we left. Only after joining do we close() the fd.
     */
    running = 0;
    shutdown(sock, SHUT_RDWR);
    pthread_join(tid, NULL);
    close(sock);
    printf("Disconnected. Bye!\n");
    return 0;
}

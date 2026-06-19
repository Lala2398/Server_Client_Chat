/*
 * server.c  -  Multi-client TCP chat hub.
 *
 * The server acts as a relay ("hub"): every message a client sends is
 * broadcast to all the OTHER connected clients. Each client is handled
 * in its own thread, and a shared, mutex-protected list keeps track of
 * who is currently online.
 *
 * Build:  see Makefile  (gcc ... -pthread)
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

/* One connected client. */
typedef struct {
    int  socket;
    char name[NAME_LEN];
} client_t;

/* Shared state: the list of online clients, guarded by a mutex so that
   multiple client threads can touch it safely. */
static client_t *clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static volatile sig_atomic_t server_running = 1;
static int listen_fd = -1;

/* Put a client into the first free slot. Returns 0 on success, -1 if full. */
static int add_client(client_t *cl)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == NULL) {
            clients[i] = cl;
            pthread_mutex_unlock(&clients_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return -1;
}

/* Remove a client (identified by its socket fd) from the list. */
static void remove_client(int socket)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != NULL && clients[i]->socket == socket) {
            clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/*
 * Send a message to every connected client except `except_fd`.
 * Pass except_fd = -1 to send to everyone.
 *
 * Note (teaching): we hold the lock while sending. That keeps the code
 * simple and correct, but send() could block if a client is slow, which
 * would stall the others. A production server would use non-blocking
 * sockets or a per-client outgoing queue. For a small chat, this is fine.
 */
static void broadcast(const char *message, int except_fd)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != NULL && clients[i]->socket != except_fd) {
            if (send(clients[i]->socket, message, strlen(message), 0) < 0)
                perror("send");
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* The per-client worker thread. */
static void *handle_client(void *arg)
{
    int sock = *(int *)arg;
    free(arg);                       /* the fd was malloc'd by main()     */

    char buffer[BUFFER_SIZE];
    char name[NAME_LEN];

    /* The very first message a client sends is its chosen username. */
    int n = recv(sock, name, NAME_LEN - 1, 0);
    if (n <= 0) {
        close(sock);
        return NULL;
    }
    name[n] = '\0';
    name[strcspn(name, "\r\n")] = '\0';      /* trim trailing newline     */
    if (name[0] == '\0')
        strcpy(name, "anonymous");

    client_t *cl = malloc(sizeof(client_t));
    if (cl == NULL) {
        close(sock);
        return NULL;
    }
    cl->socket = sock;
    strncpy(cl->name, name, NAME_LEN - 1);
    cl->name[NAME_LEN - 1] = '\0';

    if (add_client(cl) != 0) {        /* server full                       */
        const char *full = "Server is full. Please try again later.\n";
        send(sock, full, strlen(full), 0);
        free(cl);
        close(sock);
        return NULL;
    }

    printf("[+] %s connected (fd=%d)\n", cl->name, sock);
    fflush(stdout);

    char notice[BUFFER_SIZE];
    snprintf(notice, sizeof(notice), "*** %s joined the chat ***\n", cl->name);
    broadcast(notice, sock);          /* tell everyone else                */

    snprintf(notice, sizeof(notice),
             "Welcome, %s! You are now connected.\n", cl->name);
    send(sock, notice, strlen(notice), 0);   /* private hello to the joiner */

    /* Main receive loop: read messages and relay them. */
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        n = recv(sock, buffer, BUFFER_SIZE - 1, 0);

        if (n <= 0)                   /* 0 = clean disconnect, <0 = error  */
            break;

        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (buffer[0] == '\0')        /* ignore empty lines                */
            continue;

        char out[BUFFER_SIZE + NAME_LEN + 8];
        snprintf(out, sizeof(out), "%s: %s\n", cl->name, buffer);

        printf("%s", out);            /* log the message on the server     */
        fflush(stdout);
        broadcast(out, sock);         /* relay to the other clients        */
    }

    /* Cleanup once this client leaves. */
    printf("[-] %s disconnected\n", cl->name);
    fflush(stdout);
    remove_client(sock);              /* remove BEFORE freeing cl          */
    snprintf(notice, sizeof(notice), "*** %s left the chat ***\n", cl->name);
    broadcast(notice, -1);
    close(sock);
    free(cl);
    return NULL;
}

/*
 * Console thread: lets the person running the server type from the server
 * terminal too. A plain line is broadcast to every client as "server: ...".
 * Lines starting with '/' are treated as commands.
 */
static void *console_handler(void *arg)
{
    (void)arg;
    char line[BUFFER_SIZE];

    while (server_running) {
        if (fgets(line, sizeof(line), stdin) == NULL)   /* Ctrl+D = EOF    */
            break;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0')
            continue;

        if (strcmp(line, "/list") == 0) {
            pthread_mutex_lock(&clients_mutex);
            int count = 0;
            printf("---- online users ----\n");
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] != NULL) {
                    printf("  - %s (fd=%d)\n",
                           clients[i]->name, clients[i]->socket);
                    count++;
                }
            }
            printf("---- %d online ----\n", count);
            pthread_mutex_unlock(&clients_mutex);
            fflush(stdout);
        }
        else if (strcmp(line, "/help") == 0) {
            printf("Commands:\n");
            printf("  /list   show who is connected\n");
            printf("  /help   show this help\n");
            printf("  /quit   stop the server\n");
            printf("Any other text is sent to all clients as 'server: ...'\n");
            fflush(stdout);
        }
        else if (strcmp(line, "/quit") == 0) {
            printf("Shutting down...\n");
            fflush(stdout);
            server_running = 0;
            if (listen_fd != -1)
                close(listen_fd);        /* unblock accept() in main()     */
            break;
        }
        else {
            /* Not a command: broadcast it as a message from the server. */
            char out[BUFFER_SIZE + 16];
            snprintf(out, sizeof(out), "server: %s\n", line);
            printf("%s", out);           /* echo on the server console      */
            fflush(stdout);
            broadcast(out, -1);          /* -1 = send to everyone           */
        }
    }
    return NULL;
}

/* Ctrl+C handler: stop the accept loop and unblock accept(). */
static void handle_sigint(int sig)
{
    (void)sig;
    server_running = 0;
    if (listen_fd != -1)
        close(listen_fd);
}

int main(void)
{
    /* Without this, send()-ing to a client that already left would raise
       SIGPIPE and kill the whole server. Ignoring it lets send() simply
       return -1 instead. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_sigint);

    struct sockaddr_in address;
    int opt = 1;

    /* 1. Create the listening socket. (socket() returns -1 on error.) */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* 2. Allow quick restarts without "Address already in use". */
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;       /* accept on any interface */
    address.sin_port        = htons(PORT);

    /* 3. Bind to the port, then start listening. */
    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Chat server listening on port %d (max %d clients).\n",
           PORT, MAX_CLIENTS);
    printf("Type a message to send it to everyone, or use /list, /help, /quit.\n");
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    /* Start the console thread so the operator can also type from the
       server terminal (broadcast messages and run commands like /list). */
    pthread_t console_tid;
    if (pthread_create(&console_tid, NULL, console_handler, NULL) == 0)
        pthread_detach(console_tid);

    /* 4. Accept connections forever; one thread per client. */
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);

        int client_fd = accept(listen_fd,
                               (struct sockaddr *)&client_addr, &addrlen);
        if (client_fd < 0) {
            if (!server_running)      /* interrupted by Ctrl+C             */
                break;
            perror("accept");
            continue;
        }

        /* Hand the fd to the thread on the heap so each thread owns its
           own copy (avoids a data race on a shared stack variable). */
        int *pfd = malloc(sizeof(int));
        if (pfd == NULL) {
            close(client_fd);
            continue;
        }
        *pfd = client_fd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, pfd) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(pfd);
            continue;
        }
        pthread_detach(tid);          /* resources auto-reclaimed on exit  */
    }

    /* 5. Shutting down: close any sockets still open. */
    printf("\nServer shutting down...\n");
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i] != NULL)
            close(clients[i]->socket);
    pthread_mutex_unlock(&clients_mutex);

    return 0;
}

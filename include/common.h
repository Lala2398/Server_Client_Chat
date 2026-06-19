#ifndef COMMON_H
#define COMMON_H

/* Shared settings used by both the server and the client. */

#define PORT        8080   /* TCP port the server listens on            */
#define BUFFER_SIZE 2048   /* Max bytes we read/write in one go          */
#define MAX_CLIENTS 10     /* How many users can be connected at once    */
#define NAME_LEN    32     /* Max length of a username (incl. null byte) */

#endif /* COMMON_H */

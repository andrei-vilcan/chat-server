#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "chatsvr.h"


char *memnewline(char *p, int size) {
        /* finds \r _or_ \n */
        /* This is like min(memchr(p, '\r'), memchr(p, '\n')) */
        /* It is named after memchr().  There's no memcspn(). */
    for (; size > 0; p++, size--)
        if (*p == '\r' || *p == '\n')
            return(p);
    return(NULL);
}

int isalldigits(char *s) {
    for (; *s; s++)
        if (!isdigit(*s))
            return(0);
    return(1);
}

struct user {
    char *handle;
    struct user *next;
};

struct server {
    char *host;
    int port;
    int lines_pending;
    int serverfd;
    struct user *users;
};

int add_user(struct server *s, char *handle) {
    if ((strcmp(handle, "bridge") == 0) || (strcmp(handle, "chatsvr") == 0))
        return(0);
    struct user *u = s->users;
    while (u != NULL) {
        if (strcmp(u->handle, handle) == 0)
            return(-1);
        u = u->next;
    }
    u = malloc(sizeof(struct user));
    if (!u) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    u->handle = strdup(handle);
    u->next = s->users;
    s->users = u;
    // fprintf(stderr, "added user %s\n", handle);
    return(0);
}

int check_for_user(struct server *s, char *handle) {
    // return codes:
    // 0: user found
    // -1: user not found
    struct user *u = s->users;
    while (u != NULL) {
        if (strcmp(u->handle, handle) == 0)
            return(0);
        u = u->next;
    }
    return(-1);
}

char *read_line_from_server(struct server *s) {
    /* This function is guaranteed to do at most one read(). */
    static char buf[MAXTRANSMISSION + 3];
    static char *nextbuf = NULL;
    static int bytes_in_buf = 0;
    int len;
    char *p;

    /*
     * If we returned a line last time, that's at the beginning of buf --
     * move the rest of the string over it.  The bytes_in_buf value has
     * already been adjusted.
     */
    if (nextbuf) {
        memmove(buf, nextbuf, bytes_in_buf);
        nextbuf = NULL;
    }

    /* Do a read(), unless we already have a whole line. */
    if (!memnewline(buf, bytes_in_buf)) {
        if ((len = read(s->serverfd, buf + bytes_in_buf,
                    sizeof buf - bytes_in_buf - 1)) < 0) {
            perror("read");
            exit(1);
        }
        if (len == 0) {
            printf("Server shut down.\n");
            exit(0);
        }
        bytes_in_buf += len;
    }

    /* Now do we have a whole line? */
    if ((p = memnewline(buf, bytes_in_buf))) {
        nextbuf = p + 1;  /* the next line if the newline is one byte */
        /* but if the newline is \r\n... */
        if (nextbuf < buf + bytes_in_buf && *p == '\r' && *(p+1) == '\n')
            nextbuf++;  /* then skip the \n too */
        /*
         * adjust bytes_in_buf for next time.  Data moved down at the
         * beginning of the next read_line_from_server() call.
         */
        bytes_in_buf -= nextbuf - buf;
        *p = '\0';  /* we return a nice string */

        /* Is there a subsequent line already waiting? */
        s->lines_pending = !!memnewline(nextbuf, bytes_in_buf);

        return(buf);
    }

    /*
     * Is the buffer full even though we don't yet have a whole line?
     * This shouldn't happen if the server is following the protocol, but
     * still we don't want to infinite-loop over this.
     */
    if (bytes_in_buf == sizeof buf - 1) {
        buf[sizeof buf - 1] = '\0';
        bytes_in_buf = 0;
        s->lines_pending = 0;
        return(buf);  /* needn't set nextbuf because there's nothing to move */
    }

    /* No line yet.  Please try again later. */
    return(NULL);
}

void connect_to_server(struct server *s, int i) {
    struct hostent *hp;
    struct sockaddr_in r;
    char *p;

    if ((hp = gethostbyname(s->host)) == NULL) {
        fprintf(stderr, "%s: no such host\n", s->host);
        exit(1);
    }
    if (hp->h_addr_list[0] == NULL || hp->h_addrtype != AF_INET) {
        fprintf(stderr, "%s: not an internet protocol host name\n", s->host);
        exit(1);
    }

    if ((s->serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    r.sin_family = AF_INET;
    memcpy(&r.sin_addr, hp->h_addr_list[0], hp->h_length);
    r.sin_port = htons(s->port);

    if (connect(s->serverfd, (struct sockaddr *)&r, sizeof r) < 0) {
        perror("connect");
        exit(1);
    }

    while ((p = read_line_from_server(s)) == NULL) {
        ; /* i.e. loop until we get an entire line */
    }
    if (strcmp(p, CHATSVR_ID_STRING)) {
        fprintf(stderr, "That is not a 'chatsvr'\n");
        exit(1);
    }
}

int main(int argc, char **argv) {
    int i;
    char *host;
    int port;

    struct server *serverlist = malloc(argc * sizeof(struct server));
    /* There can't be more than argc servers, so this is enough space: */
    if (serverlist == NULL) {
        fprintf(stderr, "out of memory!\n");
        exit(1);
    }

    int nservers = 0;
    for (i = 1; i < argc; i++) {
        if (isalldigits(argv[i])) {
            if ((port = atoi(argv[i])) <= 0) {
                fprintf(stderr, "%s: port argument must be a positive integer\n", argv[i]);
                return(1);
            }
            if (!host) {
                fprintf(stderr, "%s: must specify a host before a port number\n", argv[i]);
                return(1);
            }
            struct server *s = NULL;
            for (s = serverlist; s < serverlist + nservers; s++)
                if (s->host == host && s->port == port) {
                    break;
                }
            s->host = host;
            s->port = port;
            s->serverfd = -1;
            s->lines_pending = 0;
            s->users = NULL;
            
            connect_to_server(s, nservers++); /* doesn't return if error */

            if (s->serverfd == -1) {
                fprintf(stderr, "Could not connect to %s:%d\n", host, port);
                return(1);
            }
            
            // store the server in the list
            serverlist[nservers] = *s;
            host = NULL;
            port = 0;
        } else {
            host = argv[i];
        }
    }
    
    if (nservers == 0) {
        fprintf(stderr, "usage: %s {host port ...} ...\n", argv[0]);
        return(1);
    }

    /* and the rest of your program goes here, obviously */

    int bridged[nservers];
    for (i = 0; i < nservers; i++) {
        bridged[i] = 0;
    }
    // give your name as bridge to each server
    for (i = 0; i < nservers; i++) {
        if (bridged[i] == 0) {
            char bridge[MAXMESSAGE + 3] = "bridge\0";
            char *p;
            if ((p = strchr(bridge, '\n'))) {
                *p = '\0';
            }
            strcat(bridge, "\r\n");
            if (write(serverlist[i].serverfd, bridge, 9) != 9) {
                perror("write");
                exit(1);
            }
            bridged[i] = 1;
        }
    }

    // create fd set for select() and find max fd
    fd_set fds;
    int maxfd = 0;
    for (i = 0; i < nservers; i++) {
        if (bridged[i] == 1) {
            FD_SET(serverlist[i].serverfd, &fds);
            if (serverlist[i].serverfd > maxfd) {
                maxfd = serverlist[i].serverfd;
            }
        }
    }

    struct timeval tv;
    tv.tv_sec = 1;
    // tv.tv_sec = 0;
    // tv.tv_usec = 500000;
    tv.tv_usec = 0;

    while (1) {
        // fprintf(stderr, "selecting\n");
        FD_ZERO(&fds);
        for (int j = 0; j < nservers; j++) {
            FD_SET(serverlist[j].serverfd, &fds);
        }
        if (select(maxfd + 1, &fds, NULL, NULL, tv) < 0) {
            perror("select");
            exit(1);
        } else {
            for (int k = 0; k < nservers; k++) {
                // fprintf(stderr, "server: %d\n", serverlist[k].serverfd);
                
                if (bridged[k] == 1 && FD_ISSET(serverlist[k].serverfd, &fds)) {
                    char *p;
                    if ((p = read_line_from_server(&serverlist[k]))) {
                        
                        // parse the message for a user on a different server
                        char *message = strcpy(malloc(strlen(p) + 1), p);
    
                        char *colon = strchr(message, ':');
                        char *user = NULL;
                        if (colon) {
                            *colon = '\0';
                            user = message;
                            message = colon + 2;
                        }
    
                        char *comma = strchr(message, ',');
                        char *to = NULL;
                        if (comma) {
                            *comma = '\0';
                            to = message;
                            message = comma + 1;
                        }
    
                        // fprintf(stderr, "user: %s\n", user);
                        // fprintf(stderr, "to: %s\n", to);
                        // fprintf(stderr, "message: %s\n", message);
    
                        if ((check_for_user(&serverlist[k], user) == -1)) {
                            add_user(&serverlist[k], user);
                        }
    
                        if (strcmp(user, "chatsvr") == 0) {
                            ;
                        } else if (strcmp(user, "bridge") == 0) {
                            ;
                        } else {
                            if (to) {
                                for (int j = 0; j < nservers; j++) {
                                    struct server *other_server = serverlist + j;
                                    if (serverlist[k].serverfd == other_server->serverfd) {
                                        ;
                                    } else {
                                        // fprintf(stderr, "checking for user %s in %s -%d\n", user, other_server->host, other_server->port);
                                        if (check_for_user(other_server, to) == 0) {
                                            // fprintf(stderr, "user found\n");
                                            char *message_for_server = malloc(strlen(user) + strlen(message) + 11);
                                            strcat(message_for_server, user);
                                            strcat(message_for_server, " says:");
                                            strcat(message_for_server, message);
                                            strcat(message_for_server, "\r\n");
                                            // fprintf(stderr, "message_for_server: %s", message_for_server);
                                            if (write(other_server->serverfd, message_for_server, strlen(message_for_server)) != strlen(message_for_server)) {
                                                perror("write");
                                                exit(1);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        break;
                    }
                }
            }
        }
    }
}
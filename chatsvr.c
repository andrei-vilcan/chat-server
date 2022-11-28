/*
 * demonstration "chat server".
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include "chatsvr.h"

int port = 1234;

static int listenfd;

struct client {
    int fd;
    char name[MAXHANDLE + 1];
    int namecomplete;
    struct client *next;
} *top = NULL;

static void addclient(int fd);
static void removeclient(struct client *p);
static void broadcast(char *s, int size);


int main(int argc, char **argv) {
    int c, status = 0;
    struct client *p;
    extern void setup(), newconnection(), whatsup(struct client *p);

    while ((c = getopt(argc, argv, "p:")) != EOF) {
        if (c == 'p') {
            if ((port = atoi(optarg)) <= 0) {
                fprintf(stderr, "%s: port argument must be a positive integer\n", argv[0]);
                return(1);
            }
        } else {
            status = 1;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        return(1);
    }

    setup();  /* aborts on error */

    /* the only way the server exits is by being killed */
    for (;;) {
        fd_set fdlist;
        int maxfd = listenfd;
        FD_ZERO(&fdlist);
        FD_SET(listenfd, &fdlist);
        for (p = top; p; p = p->next) {
            FD_SET(p->fd, &fdlist);
            if (p->fd > maxfd)
                maxfd = p->fd;
        }
        if (select(maxfd + 1, &fdlist, NULL, NULL, NULL) < 0) {
            perror("select");
        } else {
            for (p = top; p; p = p->next)
                if (FD_ISSET(p->fd, &fdlist))
                    break;
                /*
                 * it's not very likely that more than one client will drop at
                 * once, so it's not a big loss that we process only one each
                 * select(); we'll get it later...
                 */
            if (p)
                whatsup(p); /* might remove p from list, so can't be in the loop */
            if (FD_ISSET(listenfd, &fdlist))
                newconnection();
        }
    }

    return(0);
}


void setup()  /* bind and listen, abort on error */ {
    struct sockaddr_in r;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&r, sizeof r)) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}


void newconnection()  /* accept connection, update linked list */
{
    int fd;
    struct sockaddr_in r;
    socklen_t len = sizeof r;

    if ((fd = accept(listenfd, (struct sockaddr *)&r, &len)) < 0) {
        perror("accept");
    } else {
        static char greeting[] = CHATSVR_ID_STRING "\r\n";
        printf("new connection from %s, fd %d\n", inet_ntoa(r.sin_addr), fd);
        fflush(stdout);
        addclient(fd);
        write(fd, greeting, sizeof greeting - 1);
    }
}


void whatsup(struct client *p)  /* select() said activity; check it out */ {
    char buf[MAXMESSAGE + 3], buf2[MAXTRANSMISSION + 3];
    int len;
    extern void cleanupstr(char *s), killnetworknewline(char *s);

    /*
     * This really should read() into a buffer and only process it when we
     * get a newline.  Writing that loop might be part of a future a4!
     * You can see such a loop in chatclient.c.
     */
    len = read(p->fd, buf, sizeof buf - 1);
    if (len <= 0) {
        if (len < 0)
            perror("read()");
        close(p->fd);
        if (p->namecomplete) {
            printf("Disconnecting fd %d, name %s\n", p->fd, p->name);
            fflush(stdout);
            snprintf(buf, sizeof buf, "chatsvr: Goodbye, %s\r\n", p->name);
            removeclient(p);
            broadcast(buf, strlen(buf));
        } else {
            printf("Disconnecting fd %d, no name\n", p->fd);
            fflush(stdout);
            removeclient(p);
        }
    } else if (p->namecomplete) {
        buf[len] = '\0';
        killnetworknewline(buf);
        if (buf[0]) /* i.e. ignore blank lines */ {
            snprintf(buf2, sizeof buf2, "%s: %s\r\n", p->name, buf);
            broadcast(buf2, strlen(buf2));
        }
    } else {
        buf[len] = '\0';
        cleanupstr(buf);
        buf[MAXHANDLE] = '\0';
        if (buf[0]) {
            strcpy(p->name, buf);
            p->namecomplete = 1;
            printf("fd %d registers handle '%s'\n", p->fd, p->name);
            snprintf(buf2, sizeof buf2, "chatsvr: Welcome to our new participant, %s\r\n", p->name);
            broadcast(buf2, strlen(buf2));
        } else {
            static char msg[] = "chatsvr: protocol botch\r\n";
            write(p->fd, msg, sizeof msg - 1);
            printf("Disconnecting fd %d because of protocol botch in handle registration\n", p->fd);
            fflush(stdout);
            close(p->fd);
            removeclient(p);
        }
    }
}


static void addclient(int fd) {
    struct client *p = malloc(sizeof(struct client));
    if (!p) {
        fprintf(stderr, "out of memory!\n");  /* highly unlikely to happen */
        exit(1);
    }
    p->fd = fd;
    p->name[0] = '\0';
    p->namecomplete = 0;
    p->next = top;
    top = p;
}


static void removeclient(struct client *p)  /* doesn't close fd! */ {
    struct client **pp;
    for (pp = &top; *pp && *pp != p; pp = &(*pp)->next)
        ;
    if (*pp) {
        struct client *t = (*pp)->next;
        free(*pp);
        *pp = t;
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n", p->fd);
        fflush(stderr);
    }
}


static void broadcast(char *s, int size) {
    struct client *p;
    for (p = top; p; p = p->next)
        if (p->namecomplete)
            write(p->fd, s, size);
            /* should probably check write() return value and perhaps remove client */
}


void killnetworknewline(char *s) {
    char *p;
    if ((p = strchr(s, '\r')))
        *p = '\0';
    if ((p = strchr(s, '\n')))
        *p = '\0';
}


void cleanupstr(char *s) {
    /* ajr, 16 May 1988. */
    /* colon-handling added for chatsvr, Nov 2003 */
    char *t = s;
    enum {
        IGNORESPACE,  /* initial state - spaces should be ignored. */
        COPYSPACE,    /* just saw a letter - next space should be copied. */
        COPIEDSPACE   /* just copied a space - spaces should be ignored,
                       * and if this is the last space it should be removed. */
    } state = IGNORESPACE;

    while (*t) {
        if (isspace(*t)) {
            if (state == COPYSPACE) {
                /* It's a space, but we should copy it. */
                *s++ = *t++;

                /*
                 * However, we shouldn't copy another one, and if this space is
                 * last it should be removed later.
                 */
                state = COPIEDSPACE;

            } else {
                /* An extra space; skip it. */
                t++;
            }
        } else if ((*t & 127) < ' ') {
            /* control character or similar.  Skip it. */
            t++;
        } else if (*t == ':' || *t == ',') {
            /*
             * transform colons and commas to avoid parsing errors in
             * chatsvr messages
             */
            *s++ = ';';
            t++;
        } else {

            /* Not a space.  So copy it. */
            *s++ = *t++;

            /* If the next character is a space, it should be copied. */
            state = COPYSPACE;
        }
    }

    if (state == COPIEDSPACE) {
        /* Go back one, as the last character copied was a space. */
        s--;
    }

    /* Chop off the string at wherever we've copied to. */
    *s = '\0';
}

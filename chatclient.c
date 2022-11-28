#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "chatsvr.h"


static int serverfd;
static int lines_pending = 0;

static char *memnewline(char *p, int size);


int main(int argc, char **argv) {
    int port = 1234;  /* default port number */
    int len;
    char buf[MAXMESSAGE + 3];
    char *p;
    fd_set fdlist;
    extern void connect_to_server(char *host, int port);
    extern char *read_line_from_server();

    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: %s hostname [port]\n", argv[0]);
        return(1);
    }
    if (argc == 3 && (port = atoi(argv[2])) <= 0) {
        fprintf(stderr, "%s: port argument must be a positive integer\n", argv[0]);
        return(1);
    }

    connect_to_server(argv[optind], port);
    printf("Enter your 'handle': ");
    if (fgets(buf, sizeof buf, stdin) == NULL)
        return(0);
    if ((p = strchr(buf, '\n')))
        *p = '\0';
    if (!buf[0])
        return(0);
    buf[MAXHANDLE] = '\0';  /* if longer than that, truncate it */
    strcat(buf, "\r\n");
    len = strlen(buf);
    if (write(serverfd, buf, len) != len) {
        perror("write");
        exit(1);
    }

    while (1) {
        while (lines_pending)
            if ((p = read_line_from_server()))
                printf("%s\n", p);

        FD_ZERO(&fdlist);
        FD_SET(serverfd, &fdlist);
        FD_SET(0, &fdlist);
        if (select(serverfd + 1, &fdlist, NULL, NULL, NULL) < 0) {
            perror("select");
            exit(1);
        }
        if (FD_ISSET(serverfd, &fdlist)) {
            if ((p = read_line_from_server()))
                printf("%s\n", p);
        } else {
            /*
             * select() said someone's ready to read from -- if it's not the
             * server, it must be stdin
             * (that is, this is a simple case because there are only two fds)
             */
            if (fgets(buf, sizeof buf - 2, stdin) == NULL)
                exit(0);
            if ((p = strchr(buf, '\n')))
                *p = '\0';
            strcat(buf, "\r\n");
            len = strlen(buf);
            if (write(serverfd, buf, len) != len)
                perror("write");
        }
    }
}


void connect_to_server(char *host, int port)
{
    struct hostent *hp;
    struct sockaddr_in r;
    char *p;
    extern char *read_line_from_server();

    if ((hp = gethostbyname(host)) == NULL) {
        fprintf(stderr, "%s: no such host\n", host);
        exit(1);
    }
    if (hp->h_addr_list[0] == NULL || hp->h_addrtype != AF_INET) {
        fprintf(stderr, "%s: not an internet protocol host name\n", host);
        exit(1);
    }

    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    r.sin_family = AF_INET;
    memcpy(&r.sin_addr, hp->h_addr_list[0], hp->h_length);
    r.sin_port = htons(port);

    if (connect(serverfd, (struct sockaddr *)&r, sizeof r) < 0) {
        perror("connect");
        exit(1);
    }

    while ((p = read_line_from_server()) == NULL)
        ; /* i.e. loop until we get an entire line */
    if (strcmp(p, CHATSVR_ID_STRING)) {
        fprintf(stderr, "That is not a 'chatsvr'\n");
        exit(1);
    }
}


/* This function is guaranteed to do at most one read(). */
char *read_line_from_server() {
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
        if ((len = read(serverfd, buf + bytes_in_buf,
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
        lines_pending = !!memnewline(nextbuf, bytes_in_buf);

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
        lines_pending = 0;
        return(buf);  /* needn't set nextbuf because there's nothing to move */
    }

    /* No line yet.  Please try again later. */
    return(NULL);
}


char *memnewline(char *p, int size)  /* finds \r _or_ \n */
        /* This is like min(memchr(p, '\r'), memchr(p, '\n')) */
        /* It is named after memchr().  There's no memcspn(). */
{
    for (; size > 0; p++, size--)
        if (*p == '\r' || *p == '\n')
            return(p);
    return(NULL);
}

/*
 * This number was randomly generated, but is now a fixed part of the
 * protocol.  The server should provide this salutation string and the client
 * should verify the exact match.
 */

#define CHATSVR_ID_STRING "chatsvr 324705858"

/* maximum 'handle' length: */
#define MAXHANDLE 80

/* maximum message size from client to server, excluding the newline: */
#define MAXMESSAGE 300

/* maximum transmission from server to client, excluding the newline: */
#define MAXTRANSMISSION 385

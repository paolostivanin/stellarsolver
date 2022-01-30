/*
 # This file is part of the Astrometry.net suite.
 # Licensed under a 3-clause BSD style license - see LICENSE
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include "log.h" //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging

#ifdef _WIN32 //# Modified by Robert Lancaster for the StellarSolver Internal Library
#include <winsock2.h>
#include <string.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include <assert.h>

#include "solvedclient.h"
#include "bl.h"

static int serveraddr_initialized = 0;
static struct sockaddr_in serveraddr;
static FILE* fserver = NULL;

int solvedclient_set_server(char* addr) {
    char buf[256];
    char* ind;
    struct hostent* he;
    int len;
    int port;
    if (fserver) {
        if (fflush(fserver) ||
            fclose(fserver)) {
            debug("Failed to close previous connection to server.\n"); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        }
        fserver = NULL;
    }
    if (!addr)
        return -1;
#ifdef _WIN32 //# Modified by Robert Lancaster for the StellarSolver Internal Library
    ind = strstr(addr, ":");
#else
    ind = index(addr, ':');
#endif
    if (!ind) {
        debug("Invalid IP:port address: %s\n", addr); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        return -1;
    }
    len = ind - addr;
    memcpy(buf, addr, len);
    buf[len] = '\0';
    he = gethostbyname(buf);
    if (!he) {
#ifdef _WIN32 //# Modified by Robert Lancaster for the StellarSolver Internal Library
        debug("Solved server \"%s\" not found.\n", buf); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
#else
        debug("Solved server \"%s\" not found: %s.\n", buf, hstrerror(h_errno)); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
#endif
        return -1;
    }
    if (!serveraddr_initialized) {
        memset(&serveraddr, 0, sizeof(serveraddr));
        serveraddr_initialized = 1;
    }
    memcpy(&(serveraddr.sin_addr), he->h_addr, he->h_length);
    port = atoi(ind+1);
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(port);

    return 0;
}

static int connect_to_server() {
    int sock;
    if (fserver)
        return 0;
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        debug("Couldn't create socket: %s\n", strerror(errno)); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        return -1;
    }
    fserver = fdopen(sock, "r+b");
    if (!fserver) {
        debug("Failed to fdopen socket: %s\n", strerror(errno)); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        return -1;
    }
    assert(serveraddr_initialized);
    // gcc with strict-aliasing warns about this cast but it should be okay.
    if (connect(sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr))) {
        debug("Couldn't connect to server: %s\n", strerror(errno)); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        if (fclose(fserver))
            debug("Failed to close socket: %s\n", strerror(errno)); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        fserver = NULL;
        return -1;
    }
    return 0;
}

int solvedclient_get(int filenum, int fieldnum) {
    char buf[256];
    const char* solvedstr = "solved";
    int nchars;
    int solved;

    if (connect_to_server())
        return -1;
    nchars = sprintf(buf, "get %i %i\n", filenum, fieldnum);
    if ((fwrite(buf, 1, nchars, fserver) != nchars) ||
        fflush(fserver)) {
        debug("Failed to write request to server: %s\n", strerror(errno)); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        fclose(fserver);
        fserver = NULL;
        return -1;
    }
    if (!fgets(buf, 256, fserver)) {
        debug("Couldn't read response: %s\n", strerror(errno)); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        fclose(fserver);
        fserver = NULL;
        return -1;
    }
    solved = (strncmp(buf, solvedstr, strlen(solvedstr)) == 0);
    return solved;
}

void solvedclient_set(int filenum, int fieldnum) {
    char buf[256];
    int nchars;
    if (connect_to_server())
        return;
    nchars = sprintf(buf, "set %i %i\n", filenum, fieldnum);
    if ((fwrite(buf, 1, nchars, fserver) != nchars) ||
        fflush(fserver)) {
        debug("Failed to send command (%s) to solvedserver: %s\n", buf, strerror(errno)); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        return;
    }
    // wait for response.
    if (!fgets(buf, 256, fserver)) {
        debug("Couldn't read response: %s\n", strerror(errno)); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        fclose(fserver);
        fserver = NULL;
        return;
    }
}

il* solvedclient_get_fields(int filenum, int firstfield, int lastfield,
                            int maxnfields) {
    char* buf;
    int bufsize;
    il* list;
    char* cptr;
    int fld;
    int nchars;

    if (connect_to_server())
        return NULL;
    bufsize = 100 + 10 * (maxnfields ? maxnfields : (1 + lastfield - firstfield));
    buf = malloc(bufsize);
    nchars = sprintf(buf, "getall %i %i %i %i\n", filenum, firstfield,
                     lastfield, maxnfields);
    if ((fwrite(buf, 1, nchars, fserver) != nchars) ||
        fflush(fserver)) {
        debug("Failed to send command (%s) to solvedserver: %s\n", buf, strerror(errno)); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        return NULL;
    }
    // wait for response.
    if (!fgets(buf, bufsize, fserver)) {
        debug("Couldn't read response: %s\n", strerror(errno)); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        fclose(fserver);
        fserver = NULL;
        free(buf);
        return NULL;
    }
    if (sscanf(buf, "unsolved %i%n", &fld, &nchars) != 1) {
        debug("Couldn't parse response: %s\n", buf); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        free(buf);
        return NULL;
    }
    if (fld != filenum) {
        debug("Expected file number %i, not %i.\n", filenum, fld); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
        free(buf);
        return NULL;
    }
    cptr = buf + nchars;
    list = il_new(256);
    while (*cptr && *cptr != '\n') {
        if (sscanf(cptr, " %i%n", &fld, &nchars) != 1) {
            debug("Couldn't parse response: %s\n", buf); //# Modified by Robert Lancaster for the StellarSolver Internal Library for logging
            il_free(list);
            free(buf);
            return NULL;
        }
        cptr += nchars;
        il_append(list, fld);
    }
    free(buf);
    return list;
}

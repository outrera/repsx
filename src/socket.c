#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include "common.h"
#include "socket.h"

static int server_socket = 0;
static int client_socket = 0;

static char tbuf[513];
static int ptr = 0;

#define PORT_NUMBER 12345

int StartServer() {
    struct in_addr localhostaddr;
    struct sockaddr_in localsocketaddr;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);


    if (server_socket == -1)
        return -1;

    SetsNonblock();

    memset((void *)&localhostaddr, 0, sizeof(localhostaddr));
    memset(&localsocketaddr, 0, sizeof(struct sockaddr_in));

    localhostaddr.s_addr = htonl(INADDR_ANY);
    localsocketaddr.sin_family = AF_INET;
    localsocketaddr.sin_addr = localhostaddr;
    localsocketaddr.sin_port = htons(PORT_NUMBER);

    if (bind(server_socket, (struct sockaddr *) &localsocketaddr, sizeof(localsocketaddr)) < 0)
        return -1;

    if (listen(server_socket, 1) != 0)
        return -1;

    return 0;
}

void StopServer() {
    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);
}

void GetClient() {
    int new_socket, flags;
    char hello[256];

    new_socket = accept(server_socket, 0, 0);

    if (new_socket == -1)
        return;

    if (client_socket)
        CloseClient();
    client_socket = new_socket;

    flags = fcntl(client_socket, F_GETFL, 0);
    fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);

    sprintf(hello, "000 PCSX Version %s - Debug console\r\n", PACKAGE_VERSION);
    WriteSocket(hello, strlen(hello));
    ptr = 0;
}

void CloseClient() {
    if (client_socket) {
        shutdown(client_socket, SHUT_RDWR);
        close(client_socket);
        client_socket = 0;
    }
}

int HasClient() {
    return client_socket ? 1 : 0;
}

int ReadSocket(char * buffer, int len) {
    int r;
    char * endl;

    if (!client_socket)
        return -1;

    r = recv(client_socket, tbuf + ptr, 512 - ptr, 0);

    if (r == 0) {
        client_socket = 0;
        if (!ptr)
            return 0;
    }

    if (r == -1) {
        if (ptr == 0)
            return -1;
        r = 0;
    }
    ptr += r;
    tbuf[ptr] = 0;

    endl = strstr(tbuf, "\r\n");

    if (endl) {
        r = endl - tbuf;
        strncpy(buffer, tbuf, r);

        r += 2;
        memmove(tbuf, tbuf + r, 512 - r);
        ptr -= r;
        memset(tbuf + r, 0, 512 - r);
        r -= 2;

    } else {
        r = 0;
    }

    buffer[r] = 0;

    return r;
}

int RawReadSocket(char * buffer, int len) {
    int r;
    int mlen = len < ptr ? len : ptr;

    if (!client_socket)
        return -1;

    if (ptr) {
        memcpy(buffer, tbuf, mlen);
        ptr -= mlen;
        memmove(tbuf, tbuf + mlen, 512 - mlen);
    }

    if (len - mlen)
        r = recv(client_socket, buffer + mlen, len - mlen, 0);

    if (r == 0) {
        client_socket = 0;
        if (!ptr)
            return 0;
    }

    if (r == -1) {
        if (ptr == 0)
            return -1;
        r = 0;
    }

    r += mlen;

    return r;
}

void WriteSocket(char * buffer, int len) {
    if (!client_socket) return;
    send(client_socket, buffer, len, 0);
}

void SetsBlock() {
    int flags = fcntl(server_socket, F_GETFL, 0);
    fcntl(server_socket, F_SETFL, flags & ~O_NONBLOCK);
}

void SetsNonblock() {
    int flags = fcntl(server_socket, F_GETFL, 0);
    fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);
}

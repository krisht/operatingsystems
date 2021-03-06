#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define BUFF_SIZE 2048 //Default size for sending through TCP socket

int err(const char *format, ...) {
    va_list arg;
    va_start (arg, format);
    vfprintf(stderr, format, arg);
    va_end(arg);
    return EXIT_FAILURE;
}

void usage() {
    err("Usage: ./tcp_send hostName port <input_stream\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {

    if (argc != 3)
        usage();

    struct hostent *hostEntry;
    struct sockaddr_in sockIn;
    struct linger sockLinger;
    struct timeval start, end;
    char buff[BUFF_SIZE], *writeBuff;

    double startTime, endTime, rate;
    int sock, rBytes, wBytes, numBytes = 0;

    const char *hostName = argv[1];
    unsigned short port = (unsigned short) atoi(argv[2]);

    sockIn.sin_family = AF_INET;
    sockIn.sin_port = htons(port);

    if ((sockIn.sin_addr.s_addr = inet_addr(hostName)) == -1) {
        if (!(hostEntry = gethostbyname(hostName)))
            return err("Error with given hostname (unknown) with code %d: %s\n", errno, strerror(errno));
        memcpy(&sockIn.sin_addr.s_addr, hostEntry->h_addr_list[0], sizeof(sockIn.sin_addr.s_addr));
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return err("Error with opening socket with code %d: %s", errno, strerror(errno));

    sockLinger.l_onoff = 1;
    sockLinger.l_linger = 30; //Timeout period in seconds

    if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &sockLinger, sizeof(sockLinger)) < 0)
        return err("Error on using setsockopt to set SO_LINGER with code %d: %s\n", errno, strerror(errno));

    if (connect(sock, (struct sockaddr *) &sockIn, sizeof(sockIn)) < 0)
        return err("Error connecting to socket with code %d: %s\n", errno, strerror(errno));

    err("Connected...\n");

    gettimeofday(&start, NULL);

    while ((rBytes = (int) read(STDIN_FILENO, buff, BUFF_SIZE)) != 0) {
        if (rBytes < 0)
            return err("Error on reading from input with code %d: %s\n", errno, strerror(errno));

        writeBuff = buff;

        for (wBytes = 0; wBytes < rBytes;) {
            if ((wBytes = (int) write(sock, writeBuff, (unsigned int) rBytes)) <= 0)
                return err("Error writing to socket with code %d: %s\n", errno, strerror(errno));
            rBytes -= wBytes;
            writeBuff += wBytes;
            numBytes += wBytes;
        }
    }

    if (close(sock) < 0)
        return err("Error on closing socket with code %d: %s\n", errno, strerror(errno));

    gettimeofday(&end, NULL);

    endTime = end.tv_sec + (double) end.tv_usec / 1000000;
    startTime = start.tv_sec + (double) start.tv_usec / 1000000;
    rate = numBytes / ((endTime - startTime) * 1024 * 1024);

    err("Sent %d bytes\nTransfer Rate: %.6f MB/s\n", numBytes, rate);

    return EXIT_SUCCESS;
}
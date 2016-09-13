#include <stdio.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int bValue = 1024;

void exitProgram(const char *str, int retval) {
    perror(str);
    exit(retval);
}

void sysCallFiles(const char *inputFile, int ofd) {
    int len, ifd = (!strcmp(inputFile, "-")) ? STDIN_FILENO : open(inputFile, O_RDONLY);
    char data[bValue];
    if (ifd < 0)
        exitProgram(inputFile, -1);
    while ((len = read(ifd, data, bValue)) > 0 && len != -1)
        write(ofd, data, len);

    if(len == -1)
        exitProgram(inputFile, -1);
    if(close(ifd) == -1)
        exitProgram(inputFile, -1);

}

void processFiles(const char *oValue, int inputStart, int argc, char **argv) {
    int kk, ofd = (oValue == NULL) ? STDOUT_FILENO : open(oValue, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    if (ofd < 0)
        exitProgram(oValue, -1);

    if (argc == inputStart)
        sysCallFiles("-", ofd);
    else
        for (kk = inputStart; kk < argc; kk++)
            sysCallFiles(argv[kk], ofd);

    if (ofd != STDOUT_FILENO && close(ofd) == -1)
        exitProgram(oValue, -1);
     /*   if(close(ofd) == -1)
            exitProgram(oValue, -1);*/
}

int main(int argc, char **argv) {
    char *oValue = NULL;
    int ch;
    while ((ch = getopt(argc, argv, "b:o:")) != -1)
        switch (ch) {
            case 'b':
                bValue = atoi(optarg);
                break; //Check whether optarg is a number
            case 'o':
                oValue = optarg;
                break;
            case '?':
                exit(-1);
            default :
                abort();
        }

    processFiles(oValue, optind, argc, argv);

    if (close(STDOUT_FILENO))
        err(1, "stdout");
    exit(0);
}
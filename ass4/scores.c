#include <game.h>
#include <util.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include "shared.h"

/* Message to send to server to indicate scores client */
#define INITIAL_SEND_MESSAGE "scores"

/* Message sent back by server to verify */
#define VERIFY_MESSAGE "yes"

/*
 * Defines exit codes for program
 */
typedef enum {
    NORMAL_EXIT = 0,
    WRONG_ARGS = 1, // Wrong argument count provided
    CONNECTION_ERROR = 3, // Couldn't connect to localhost on port
    INVALID_SERVER = 4 // If connected to server, and it is invalid
} ExitCode;

/*
 * Exits program with given exit code, and prints an info message to stderr
 */
void exit_program(ExitCode code) {
    switch (code) {
        case WRONG_ARGS:
            fprintf(stderr, "Usage: gopher port\n");
            break;
        case CONNECTION_ERROR:
            fprintf(stderr, "Failed to connect\n");
            break;
        case INVALID_SERVER:
            fprintf(stderr, "Invalid server\n");
            break;
        default:
            break;
    }

    exit(code);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        exit_program(WRONG_ARGS);
    }

    // Connect to socket at localhost (NULL), at port given as arg
    int sockfd = connect_to(NULL, argv[1]);
    if (sockfd == -1) {
        exit_program(CONNECTION_ERROR);
    }

    // Open the socket to read and write.
    FILE* input = fdopen(sockfd, "r+");

    // Send initial message to indicate this is scores client
    fprintf(input, "%s\n", INITIAL_SEND_MESSAGE);

    // Get first message from server
    char* buffer;
    if (read_line(input, &buffer, 0) <= 0) {
        free(buffer);
        fclose(input);
        exit_program(INVALID_SERVER);
    }

    ExitCode code = NORMAL_EXIT;
    if (strcmp(buffer, VERIFY_MESSAGE) == 0) {
        // While input available, read and send to stdout
        char c;
        while((c = fgetc(input)) != EOF) {
            putchar(c);
        }
    } else {
        code = INVALID_SERVER;
    }

    free(buffer);
    fclose(input);

    exit_program(code);
}

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "shared.h"
#include <util.h>

/*
 * Connects to given host at specified port.
 * If successful, returns a socket file descriptor to send/receive from.
 * Otherwise, if unsuccessful, returns -1.
 */
int connect_to(const char* host, const char* port) {
    struct addrinfo hints;
    struct addrinfo* res0;
    int sock = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res0) != 0) {
        return -1;
    }

    for (struct addrinfo* res = res0; res != NULL; res = res->ai_next) {
        sock = socket(res->ai_family, res->ai_socktype,
                res->ai_protocol);
        if (sock == -1) {
            continue;
        }

        if (connect(sock, res->ai_addr, res->ai_addrlen) == -1) {
            close(sock);
            sock = -1;
            continue;
        }

        break;  /* okay we got one */
    }

    freeaddrinfo(res0);

    return sock;
}

/*
 * Checks whether a file ends in a newline ('\n') character.
 * Returns true if the last character (before EOF) in the file is a \n,
 * otherwise returns false.
 * Restores file's position indicator to original position before returning.
 */
bool does_file_end_newline(FILE* file) {
    // Get original position indicator
    long oldPos = ftell(file);

    // Go to last char of file
    fseek(file, 0, SEEK_END);
    fseek(file, (ftell(file) - sizeof(char)), SEEK_SET);

    // get last char of file
    char last = fgetc(file);

    // Return to original position before function ran
    fseek(file, oldPos, SEEK_SET);

    return last == '\n';
}

/*
 * Reads a keyfile at given filename. If valid (i.e. at least one character
 * and no newlines), it will return true and store the read key in dest.
 * If invalid, it won't change value at dest and will return false.
 */
bool get_keyfile(const char* filename, char** dest) {
    FILE* keyfile = fopen(filename, "r");

    if (keyfile == NULL) {
        return false;
    }

    if (read_line(keyfile, dest, 0) <= 0) {
        fclose(keyfile);
        return false;
    }

    // Keyfile is valid if it now ends, and the last char wasn't a \n
    bool isValid = !does_file_end_newline(keyfile)
            && (fgetc(keyfile) == EOF);

    fclose(keyfile);

    return isValid;
}

/*
 * Converts a string to integer.
 * Returns false if all of input string wasn't part of integer, or if
 * input value couldn't fit into an integer. Doesn't change dest value.
 * Otherwise returns true on success and stores converted value at dest.
 */
bool str_to_int(const char* str, int* dest) {
    char* strpart;
    long value;

    // convert string to long, base 10
    value = strtol(str, &strpart, 10);

    // Check if any of string is not part of int, or number is too big for int
    if (str[0] == ' ' || *strpart != '\0' || value > INT_MAX) {
        return false;
    }

    *dest = (int) value;

    return true;
}

/*
 * Counts tokens in tokenPool, which has numPiles elements.
 * Returns total count of values in tokenPool.
 */
int count_tokens(int* tokenPool, int numPiles) {
    int count = 0;
    for (int i = 0; i < numPiles; i++) {
        count += tokenPool[i];
    }

    return count;
}

/*
 * Checks if a given string is a valid name. A valid name contains no
 * newline characters or commas.
 * Returns true if name is valid, false otherwise.
 */
bool is_valid_game_name(const char* name) {
    for (int i = 0; i < strlen(name); i++) {
        if (name[i] == ',' || name[i] == '\n') {
            return false;
        }
    }

    return true;
}

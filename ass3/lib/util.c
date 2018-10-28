#include "util.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

/*
 * Checks if an array of integers is all positive (or 0).
 *
 * @param length The length of array
 * @param array The array to check, of size length
 * @return true if all items are positive or 0, else false.
 */
bool is_array_all_positive(int length, int array[length]) {
    for (int i = 0; i < length; i++) {
        if (array[i] < 0) {
            return false;
        }
    }

    return true;
}

/*
 * Checks if a string has any whitespace
 *
 * @param input String to check
 * @return true if any whitespace, false otherwise
 */
bool has_any_whitespace(char* input) {
    for (int i = 0; i < strlen(input); i++) {
        if (isspace(input[i])) {
            return true;
        }
    }

    return false;
}

/*
 * Checks if a string starts with prefix string.
 *
 * @param input String to check if it starts with
 * @param prefix String/prefix to check is at start of input
 */
bool starts_with(char* input, char* prefix) {
    return strncmp(prefix, input, strlen(prefix)) == 0;
}

/*
 * Gets input from specified filestream until EOF, newline, or max buffer.
 *
 * @param inputBuffer Destination buffer to store input received
 * @param bufferSize Size of inputBuffer
 * @param fileStream Stream to read from
 */
bool get_input(char* inputBuffer, int bufferSize, FILE* fileStream) {
    if (fgets(inputBuffer, bufferSize, fileStream) == NULL) {
        // EOF reached
        return false;
    }

    // Remove newline at end if needed
    if (inputBuffer[strlen(inputBuffer) - 1] == '\n') {
        inputBuffer[strlen(inputBuffer) - 1] = '\0';
    }

    return true;
}

/*
 * Converts player integer (from 0-25) to respective letter (A to Z)
 *
 * @param playerId Player id integer
 * @return player's letter
 */
char player_int_to_char(int playerId) {
    return (char) (playerId + 'A');
}

/*
 * Converts player letter (from A-Z) to respective integer (0-25)
 *
 * @param playerLetter Player id letter
 * @return player's integer id
 */
int player_char_to_int(char playerLetter) {
    return (int) (playerLetter - 'A');
}

/*
 * Checks if a given player letter is valid, and within valid range
 * considering how many players in game.
 *
 * @param playerLetter Player letter to check
 * @param numPlayers Number of players (max valid letter offset)
 * @return true if valid player char, otherwise false
 */
bool is_valid_player_char(char playerLetter, int numPlayers) {
    return (playerLetter >= 'A' && playerLetter < ('A' + numPlayers));
}

/*
 * Converts a string to integer.
 * Returns false if all of input string wasn't part of integer, or if
 * input value couldn't fit into an integer.
 *
 * @param str String to convert
 * @param dest Destination pointer to store converted int
 * @return true if str was a valid int, false otherwise.
 */
bool str_to_int(char* str, int* dest) {
    char* strpart;
    long value;

    // convert string to long, base 10
    value = strtol(str, &strpart, 10);

    *dest = (int) value;

    // Check if any of string is not part of int, or number is too big for int
    if (*strpart != '\0' || value > INT_MAX) {
        return false;
    }

    return true;
}

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef UTIL_H
#define UTIL_H

#define BUFFER_SIZE 100

bool has_any_whitespace(char* input);

bool is_array_all_positive(int length, int array[length]);

bool starts_with(char* input, char* prefix);

bool str_to_int(char* str, int* dest);

char player_int_to_char(int playerId);

int player_char_to_int(char playerChar);

bool is_valid_player_char(char playerLetter, int numPlayers);

bool get_input(char* inputBuffer, int bufferSize, FILE* fileStream);

#endif

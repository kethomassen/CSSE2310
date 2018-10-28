#include <stdio.h>
#include <stdbool.h>

#ifndef SHARED_H
#define SHARED_H

int connect_to(const char* host, const char* port);

bool does_file_end_newline(FILE* file);

bool get_keyfile(const char* filename, char** dest);

bool str_to_int(const char* str, int* dest);

int count_tokens(int* tokenPool, int numPiles);

bool is_valid_game_name(const char* name);

#endif

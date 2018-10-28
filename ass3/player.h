#include "lib/game.h"

#ifndef PLAYER_H
#define PLAYER_H

/*
 * Shared interface methods between base player and specific implementations
 */

/* Implemented by base player */

// Tells hub to take a wild token
void tell_hub_take_wild(void);

// Tells hub to take tokens
void tell_hub_take_tokens(int tokens[NUM_COLOURS]);

// Tells hub to purchase a card from board
void tell_hub_purchase_card(int cardId, int tokens[5]);

/* Implemented by specific player implementation*/

// Gets player implementation name
const char* get_player_name(void);

// Called when a player implementation needs to choose a move
void choose_move(Game* game);

#endif

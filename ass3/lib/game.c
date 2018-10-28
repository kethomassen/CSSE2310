#include "game.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>


/*
 * Gets a CardColour from given input character
 *
 * @param input Input character
 * @return Associated CardColour or INVALID_COLOUR
 */
CardColour get_card_colour(char input) {
    switch (input) {
        case 'B':
            return BROWN;
            break;
        case 'Y':
            return YELLOW;
            break;
        case 'P':
            return PURPLE;
            break;
        case 'R':
            return RED;
            break;
        default:
            return INVALID_COLOUR;
    }
}

/*
 * Gets character for given CardColour
 *
 * @param colour The card colour to get char for
 * @return Associated character or '?'
 */
char get_card_char(CardColour colour) {
    switch (colour) {
        case BROWN:
            return 'B';
            break;
        case YELLOW:
            return 'Y';
            break;
        case PURPLE:
            return 'P';
            break;
        case RED:
            return 'R';
            break;
        default:
            return '?';
    }
}

/*
 * Frees memory associated with a malloc'ed game struct.
 * If passed a NULL pointer, does nothing.
 *
 * @param game The malloc'ed game struct to free
 */
void free_game(Game* game) {
    if (game == NULL) {
        return;
    }

    // For hub: Free DeckCard's if any
    DeckCard* oldTop;
    while (game->topOfDeck != NULL) {
        oldTop = game->topOfDeck;
        game->topOfDeck = game->topOfDeck->next;
        free(oldTop);
    }

    // For hub: Close file streams to players' stdin/stdout
    for (int playerId = 0; playerId < game->numPlayers; playerId++) {
        if (game->players[playerId].in != NULL) {
            fclose(game->players[playerId].in);
        }

        if (game->players[playerId].out != NULL) {
            fclose(game->players[playerId].out);
        }
    }

    // Free players array
    free(game->players);

    free(game);
}


/*
 * Allocates memory and initialises game struct
 *
 * @param numPlayers Number of players in game to initialise array
 * @return pointer to game
 */
Game* setup_game(int numPlayers) {
    Game* game = malloc(sizeof(Game));

    game->topOfDeck = NULL;
    game->cardsFacedUp = 0;
    game->numPlayers = numPlayers;

    game->players = malloc(sizeof(Player) * numPlayers);

    for (int i = 0; i < numPlayers; i++) {
        game->players[i].totalPoints = 0;

        // Initialise tokens and discounts for a player to be all 0
        memset(game->players[i].tokens, 0, sizeof(game->players[i].tokens));
        memset(game->players[i].discounts, 0,
                sizeof(game->players[i].discounts));

        // File streams haven't been setup yet. Player processes don't need.
        game->players[i].in = NULL;
        game->players[i].out = NULL;
    }

    return game;
}

/*
 * Populates a Card struct from a given input string.
 * A valid input is of form "D:V:T_P,T_B,T_Y,T_R"
 * where D is discount colour, V is value of card, and T_(?) indicates
 * price of the card for specified colour (?) of tokens
 *
 * @param input String to try and convert
 * @param card
 * @return true if input was valid, otherwise false
 */
bool create_card(char* input, Card* card) {
    char colour;
    int value, price[4];

    int result = sscanf(input, "%c:%d:%d,%d,%d,%d", &colour, &value,
            &price[PURPLE], &price[BROWN], &price[YELLOW], &price[RED]);

    if (result != 6) {
        return false;
    }

    card->value = value;

    // Check card colour is valid
    if ((card->discount = get_card_colour(colour)) == INVALID_COLOUR) {
        return false;
    }
    // copy price array into card struct
    memcpy(card->price, &price, sizeof(price));

    return true;
}

/*
 * Adds a card to the board
 *
 * @param game The game struct
 * @param card The card to add to the board
 */
void add_card_to_board(Game* game, Card card) {
    game->cards[game->cardsFacedUp] = card;
    game->cardsFacedUp++;
}

/*
 * Removes a face up card from the board.
 * Once a card has been removed from the board, the other cards id's
 * will be moved down if neccessary

 * @param game The game struct
 * @param cardId Index of card from 0-7, 0 being the oldest, 7 the newest
 */
void take_card_from_board(Game* game, int cardId) {
    // Start from card being removed, and shift the remaining down the array
    for (int i = cardId; i < game->cardsFacedUp; i++) {
        // If next card exists, move it down
        if (i + 1 < game->cardsFacedUp) {
            game->cards[i] = game->cards[i + 1];
        }

    }

    game->cardsFacedUp = game->cardsFacedUp - 1;
}

/*
 * Gets the players with highest scores in game and stores their letters
 * as a comma separated string in dest.
 *
 * @param game The game struct
 * @param dest Destination to store string
 */
void get_winners(Game* game, char* dest) {
    int highestScore = 0;

    // Find highest score
    for (int i = 0; i < game->numPlayers; i++) {
        if (game->players[i].totalPoints > highestScore) {
            highestScore = game->players[i].totalPoints;
        }
    }

    *dest = '\0'; // Empty string first

    // Make string
    int numFound = 0;
    for (int i = 0; i < game->numPlayers; i++) {
        if (game->players[i].totalPoints == highestScore) {
            numFound++;

            // Comma separate letters if needed
            if (numFound > 1) {
                dest[strlen(dest)] = ',';
            }

            dest[strlen(dest)] = player_int_to_char(i);
            // Terminate string
            dest[strlen(dest) + 1] = '\0';
        }
    }
}

/*
 * Gets how many wilds a player would need to purchase a card.
 *
 * @param game The game struct
 * @param playerId The id of the player (0-25)
 * @param cardId The id of the card (0-7)
 */
int get_wilds_needed_for_card(Game* game, int playerId, int cardId) {
    Card card = game->cards[cardId];
    Player player = game->players[playerId];
    int wildsNeeded = 0;

    for (int i = 0; i < NUM_COLOURS; i++) {
        int tokensNeeded = (card.price[i] - player.discounts[i]);
        // Make sure tokensNeeded is not negative
        tokensNeeded = (tokensNeeded < 0 ? 0 : tokensNeeded);

        if (player.tokens[i] >= tokensNeeded) {
            continue;
        }

        wildsNeeded += (player.tokens[i] - tokensNeeded);
    }

    return wildsNeeded;
}

/*
 * Gets how many tokens a player has
 *
 * @param game The game struct
 * @param playerId Which player to check (starting from 0)
 * @return Number of tokens player has
 */
int get_player_token_count(Game* game, int playerId) {
    int total = 0;

    for (int i = 0; i < TOKEN_SLOTS; i++) {
        total += game->players[playerId].tokens[i];
    }

    return total;
}

/*
 * Gets the price of a faced up card for a specific players
 * Takes into account discounts the player has, and includes wilds.
 *
 * @param game The game struct
 * @param playerId Which player to check for (starting from 0)
 * @param cardId Card to get price for (starting from 0)
 * @return Number of tokens player has
 */
int get_card_price_for_player(Game* game, int playerId, int cardId) {
    int total = 0;
    for (int i = 0; i < NUM_COLOURS; i++) {
        int tokensNeeded =
                game->cards[cardId].price[i] -
                game->players[playerId].discounts[i];
        // Make sure tokensNeeded is not negative, and add to total
        total += (tokensNeeded < 0 ? 0 : tokensNeeded);
    }
    return total;
}

/*
 * Chooses tokens for a player to buy a card.
 * Takes into account a player's discounts and uses as little
 * wilds as possible.
 * Stores result in destTokens array passed.
 * Assumes player can afford card before calling.
 *
 * @param game The game struct
 * @param playerId The player buying the card
 * @param cardId The card on the board the player is buying
 * @param destTokens Destination array to store result
 */
void choose_tokens_to_buy_card(Game* game, int playerId, int cardId,
        int destTokens[TOKEN_SLOTS]) {

    destTokens[WILD] = 0;
    for (int i = 0; i < NUM_COLOURS; i++) {
        // Get how many tokens of this colour needed, and make non-negative
        int tokensNeeded =
                (game->cards[cardId].price[i] -
                game->players[playerId].discounts[i]);
        tokensNeeded = (tokensNeeded < 0 ? 0 : tokensNeeded);

        if (tokensNeeded > game->players[playerId].tokens[i]) {
            // Need more tokens of this colour, so use wilds to make up
            destTokens[WILD] +=
                    (tokensNeeded - game->players[playerId].tokens[i]);
            destTokens[i] = game->players[playerId].tokens[i];
        } else {
            destTokens[i] = tokensNeeded;
        }
    }
}

/*
 * Checks if any player has reached the max number of points
 *
 * @param game The game struct
 * @return whether any player has reached the max number of points
 */
bool is_game_over(Game* game) {
    for (int playerId = 0; playerId < game->numPlayers; playerId++) {
        if (game->players[playerId].totalPoints >= game->maxPoints) {
            return true;
        }
    }

    return false;
}

/*
 * Checks if tokens can be taken from the board.
 * For tokens to be able to be taken, there must be at least
 * three non-empty token piles on the board.
 *
 * @param game The game struct
 * @return true if tokens can be taken, otherwise false.
 */
bool can_tokens_be_taken(Game* game) {
    int pilesWithTokens = 0;
    for (int i = 0; i < NUM_COLOURS; i++) {
        if (game->tokens[i] > 0) {
            pilesWithTokens++;
        }
    }

    return (pilesWithTokens >= TOKENS_PER_TAKE);
}

/*
 * Checks whether a given player can afford a card currently on the board.
 *
 * @param game The game struct
 * @param playerId The player to check
 * @param cardId The card id to check
 */
bool can_player_afford_card(Game* game, int playerId, int cardId) {
    Card card = game->cards[cardId];
    Player player = game->players[playerId];

    int wildsUsed = 0;
    for (int i = 0; i < NUM_COLOURS; i++) {
        // Get how many tokens needed for this colour, make non-negative
        int tokensNeeded = (card.price[i] - player.discounts[i]);
        tokensNeeded = (tokensNeeded < 0 ? 0 : tokensNeeded);

        // Player has enough tokens of this colour
        if (player.tokens[i] >= tokensNeeded) {
            continue;
        }

        // Check if player has enough wilds to fill discrepancy
        if (player.tokens[WILD] > 0 &&
                player.tokens[i] + (player.tokens[WILD] - wildsUsed)
                >= tokensNeeded) {
            // Keep track of how many wilds used so far
            wildsUsed += (tokensNeeded - player.tokens[i]);
        } else {
            // not enough wilds to afford card
            return false;
        }
    }

    return true;
}

/*
 * Check if given tokens are valid to buy a card for a player.
 * If a player can't afford card, obviously the tokens given can't be used.
 * If a player preferences wilds over normal tokens they have,
 * it is considered invalid too.
 *
 * @param game The game struct
 * @param playerId Player to check for
 * @param cardId Card to check for
 * @param tokens Array of tokens attempting to be used to purchase card
 * @return true if tokens are valid, false otherwise
 */
bool can_tokens_buy_card(Game* game, int playerId, int cardId,
        int tokens[TOKEN_SLOTS]) {

    if (!can_player_afford_card(game, playerId, cardId)) {
        return false;
    }

    int correctTokens[TOKEN_SLOTS];
    choose_tokens_to_buy_card(game, playerId, cardId, correctTokens);

    // Check if their tokens are different from what they should have
    for (int i = 0; i < TOKEN_SLOTS; i++) {
        if (correctTokens[i] != tokens[i]) {
            return false;
        }
    }

    return true;
}

/*
 * Checks if a player's token take is valid.
 * A valid token take must include only 3 different colours, with a
 * maximum of 3 tokens to take.
 *
 * @param game The game struct
 * @param tokens Array of tokens in player's attempted take
 * @return true if valid take, false otherwise
 */
bool is_valid_token_take(Game* game, int tokens[NUM_COLOURS]) {
    int numTaken = 0;
    for (int i = 0; i < NUM_COLOURS; i++) {
        if (tokens[i] == 1 && game->tokens[i] > 0) {
            numTaken++;
        } else if (tokens[i] != 0) {
            return false;
        }
    }

    return (numTaken == TOKENS_PER_TAKE);
}

/*
 * Sets the token piles on the board to be of size initialTokens
 *
 * @param game The game struct
 * @param initialTokens How many tokens to be in each pile
 */
void set_initial_game_tokens(Game* game, int initialTokens) {
    for (int i = 0; i < NUM_COLOURS; i++) {
        game->tokens[i] = initialTokens;
    }
}

/*
 * Updates the game state after a player takes purchases a faced up card.
 * First, removes the card from the board.
 * Then removes the tokens from the player's piles and puts the non-wild
 * tokens back on the board piles.
 * Then updates the discount and total points for a player based on card.
 *
 * Note: Need to check whether this card purchase is valid before calling.
 *
 * @param game The game struct
 * @param playerId The id of the player who took tokens
 * @param cardId Index of card being purchased from board
 * @param tokens Tokens used in purchase
 */
void player_purchased_card(Game* game, int playerId, int cardId,
        int tokens[TOKEN_SLOTS]) {
    Card card = game->cards[cardId];
    take_card_from_board(game, cardId);

    for (int i = 0; i < TOKEN_SLOTS; i++) {
        // Take tokens away from player
        game->players[playerId].tokens[i] -= tokens[i];
        // Add the non wilds back to the board
        if (i != WILD) {
            game->tokens[i] += tokens[i];
        }
    }

    // Update player's points and respective discount
    game->players[playerId].discounts[card.discount] += 1;
    game->players[playerId].totalPoints += card.value;
}

/*
 * Updates the game state after a player takes tokens from the board.
 * Adds the tokens to the player's piles, and removes them from the board.
 *
 * Note: Doesn't check if it is valid to take such tokens, up to caller.
 *
 * @param game The game struct
 * @param playerId The id of the player who took tokens
 * @param tokens Tokens used in purchase
 */
void player_took_tokens(Game* game, int playerId, int tokens[5]) {
    for (int i = 0; i < NUM_COLOURS; i++) {
        // Add tokens to player's account
        game->players[playerId].tokens[i] += tokens[i];
        // Remove those tokens from board
        game->tokens[i] -= tokens[i];
    }
}

/*
 * Updates the game state after a player takes a wild from the board.
 * Increments player's wild pile by 1.
 *
 * @param game The game struct
 * @param playerId The id of the player who took tokens
 */
void player_took_wild(Game* game, int playerId) {
    game->players[playerId].tokens[WILD] += 1;
}

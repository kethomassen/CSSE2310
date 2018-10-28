#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#ifndef GAME_H
#define GAME_H

#define MAX_PLAYERS 26
#define NUM_COLOURS 4
#define TOKEN_SLOTS 5
#define MAX_CARDS_ON_BOARD 8
#define TOKENS_PER_TAKE 3
#define WILD 4

/*
 * CardColour for a card discount, or token
 */
typedef enum {
    PURPLE,
    BROWN,
    YELLOW,
    RED,
    INVALID_COLOUR
} CardColour;

/*
 * Holds information about a card
 * - discount: The discount colour associated with a card
 * - value: The number of points a card is worth
 * - price: The price in tokens for each colour.
 */
typedef struct {
    CardColour discount;
    int value;
    int price[NUM_COLOURS];
} Card;

/*
 * Represents a card in the deck for the hub.
 * - card: Information about the card
 * - next: pointer to the next DeckCard in the deck.
 */
typedef struct DeckCard {
    Card card;
    struct DeckCard* next;
} DeckCard;

/*
 * Stores information about a single player in a game.
 * - totalPoints: How many total points the player has from card purchases
 * - discounts: Holds the discounts held by a player after card purchases
 * - tokens: Holds the player's token counts
 * The following fields are only used by the hub:
 * - in: holds player's stdin, used by hub to send messages
 * - out: holds player's stdout, used by hub to read player moves
 * - pid: The process id of the player after fork
 */
typedef struct {
    int totalPoints;
    int discounts[NUM_COLOURS];
    int tokens[TOKEN_SLOTS];

    FILE* in;
    FILE* out;
    pid_t pid;
} Player;

/*
 * Stores information about a game.
 * - maxPoints: The number of points needed to trigger game end.
 * - tokens: Holds the token piles on the board
 * - cardsFacedUp: Number of cards currently face up on board
 * - cards: Array of cards face up on the board
 * - numPlayers: Number of players in the game
 * - players: Array of players in the game
 * Player process specific:
 * - myId: Holds the id of the current player
 * Hub specific:
 * - topOfDeck: Pointer to the top of the deck
 */
typedef struct {
    int myId;

    int maxPoints;
    int numPlayers;
    int tokens[NUM_COLOURS];
    int cardsFacedUp;

    Card cards[MAX_CARDS_ON_BOARD];
    DeckCard* topOfDeck;
    Player* players;
} Game;


CardColour get_card_colour(char input);
char get_card_char(CardColour colour);

Game* setup_game(int numPlayers);

void free_game(Game* game);

bool create_card(char* input, Card* card);

void get_winners(Game* game, char* dest);
int get_card_overall_price(Card card);
int get_card_price_for_player(Game* game, int playerId, int cardId);
int get_player_token_count(Game* game, int playerId);
int get_wilds_needed_for_card(Game* game, int playerId, int cardId);

void choose_tokens_to_buy_card(Game* game, int playerId, int cardId,
        int destTokens[TOKEN_SLOTS]);

bool is_game_over(Game* game);
bool is_valid_token_take(Game* game, int tokens[TOKEN_SLOTS]);

bool can_player_afford_card(Game* game, int playerId, int cardId);
bool can_tokens_be_taken(Game* game);
bool can_tokens_buy_card(Game* game, int playerId, int cardId,
        int tokens[TOKEN_SLOTS]);

void player_purchased_card(Game* game, int playerId, int cardId,
        int tokens[TOKEN_SLOTS]);
void player_took_tokens(Game* game, int playerId, int tokens[TOKEN_SLOTS]);
void player_took_wild(Game* game, int playerId);

void add_card_to_board(Game* game, Card card);
void take_card_from_board(Game* game, int cardId);

void set_initial_game_tokens(Game* game, int initialTokens);

#endif

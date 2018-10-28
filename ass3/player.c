//#include "shared.h"
#include "player.h"
#include "lib/util.h"
#include "lib/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

typedef enum {
    NORMAL = 0,
    WRONG_NUM_ARGS = 1,
    INVALID_PLAYER_COUNT = 2,
    INVALID_PLAYER_ID = 3,
    COMMUNICATION_ERROR = 6,
    END_OF_GAME
} Result;

/*
 * Exits player process with given exit code and frees memory.
 *
 * @param game The game struct
 * @param code Exit result
 */
void exit_player(Game* game, Result code) {
    free_game(game);

    switch (code) {
        case NORMAL:
            break;
        case END_OF_GAME:
            exit(NORMAL);
            break;
        case WRONG_NUM_ARGS:
            fprintf(stderr, "Usage: %s pcount myid\n", get_player_name());
            break;
        case INVALID_PLAYER_COUNT:
            fprintf(stderr, "Invalid player count\n");
            break;
        case INVALID_PLAYER_ID:
            fprintf(stderr, "Invalid player ID\n");
            break;
        case COMMUNICATION_ERROR:
            fprintf(stderr, "Communication Error\n");
            break;
        default:
            break;
    }

    exit(code);
}

/*
 * Tells hub the player wishes to take a wild by printing "wild" to stdout.
 *
 * @param game The game struct
 */
void tell_hub_take_wild(void) {
    printf("wild\n");
    fflush(stdout);
}

/*
 * Tells hub the player wishes to take tokens by printing
 * a "take" command to stdout.
 *
 * @param tokens The tokens the player wishes to take from the board
 */
void tell_hub_take_tokens(int tokens[NUM_COLOURS]) {
    printf("take%d,%d,%d,%d\n",
            tokens[PURPLE], tokens[BROWN], tokens[YELLOW], tokens[RED]);
    fflush(stdout);
}

/*
 * Tells hub the player wishes to purchase a face up card by
 * printing a "purchase" command to stdout.
 *
 * @param cardId Id (0-7) of card to purchase
 * @param tokens Tokens being used to buy card
 */
void tell_hub_purchase_card(int cardId, int tokens[TOKEN_SLOTS]) {
    printf("purchase%d:%d,%d,%d,%d,%d\n", cardId,
            tokens[PURPLE], tokens[BROWN], tokens[YELLOW], tokens[RED],
            tokens[WILD]);
    fflush(stdout);
}

/*
 * Prints status of game to stderr.
 * Lists cards in the market and their details.
 * Lists all players and their discounts, tokens, points.
 *
 * @param game The game struct
 */
void print_game_status(Game* game) {
    // Print card info
    for (int i = 0; i < game->cardsFacedUp; i++) {
        Card card = game->cards[i];
        fprintf(stderr, "Card %d:%c/%d/%d,%d,%d,%d\n", i,
                get_card_char(card.discount), card.value,
                card.price[PURPLE], card.price[BROWN],
                card.price[YELLOW], card.price[RED]);
    }

    // Print player info
    for (int playerId = 0; playerId < game->numPlayers; playerId++) {
        Player player = game->players[playerId];

        fprintf(stderr,
                "Player %c:%d:Discounts=%d,%d,%d,%d:Tokens=%d,%d,%d,%d,%d\n",
                player_int_to_char(playerId), player.totalPoints,
                player.discounts[PURPLE], player.discounts[BROWN],
                player.discounts[YELLOW], player.discounts[RED],
                player.tokens[PURPLE], player.tokens[BROWN],
                player.tokens[YELLOW], player.tokens[RED],
                player.tokens[WILD]);
        fflush(stderr);
    }

}

/*
 * Handles a newcard message from the hub.
 * Data is of format "discount:value:purple,brown,yellow,red"
 * Message is invalid if card can't be created from the data for any reason,
 * or if the board is already full of cards.
 *
 * @param game The game struct
 * @param data The message data associated with the command from hub
 * @return NORMAL for success, COMMUNICATION_ERROR if message invalid.
 */
Result handle_newcard(Game* game, char* data) {
    Card card;

    // Check if board is full or input invalid.
    if (!create_card(data, &card)
            || game->cardsFacedUp == MAX_CARDS_ON_BOARD) {
        return COMMUNICATION_ERROR;
    }

    add_card_to_board(game, card);

    print_game_status(game);
    return NORMAL;
}

/*
 * Handles a purchased message from the hub.
 * Data is of format "playerletter:cardid:purple,brown,yellow,red,wilds"
 * Message is invalid if input doesn't follow format above, the input has
 * any whitespcace, or if the cardId is invalid.
 *
 * @param game The game struct
 * @param data The message data associated with the command from hub
 * @return Result: NORMAL for success, COMMUNICATION_ERROR if message invalid.
 */
Result handle_card_purchased(Game* game, char* data) {
    char playerId;
    int cardNumber, tokens[TOKEN_SLOTS];
    int result = sscanf(data, "%c:%d:%d,%d,%d,%d,%d", &playerId, &cardNumber,
            &tokens[PURPLE], &tokens[BROWN], &tokens[YELLOW], &tokens[RED],
            &tokens[WILD]);

    // Check for invalid input
    if (result != 7 || cardNumber < 0 || cardNumber >= game->cardsFacedUp
            || has_any_whitespace(data) ||
            !is_valid_player_char(playerId, game->numPlayers)) {
        return COMMUNICATION_ERROR;
    }

    player_purchased_card(game, player_char_to_int(playerId),
            cardNumber, tokens);

    print_game_status(game);
    return NORMAL;
}

/*
 * Handles a "wild" message from the hub.
 * Data is of format "P" where P is the playerId from A-Z.
 * Message is invalid if player is invalid or if there is any whitespace.
 *
 * @param game The game struct
 * @param data The message data associated with the command from hub
 * @return Result: NORMAL for success, COMMUNICATION_ERROR if message invalid.
 */
Result handle_wild_take(Game* game, char* data) {
    // Make sure data is just 1 letter, and valid player char too.
    if (strlen(data) != 1 ||
            !is_valid_player_char(data[0], game->numPlayers)) {

        return COMMUNICATION_ERROR;
    }

    player_took_wild(game, player_char_to_int(data[0]));

    print_game_status(game);
    return NORMAL;
}

/*
 * Handles a "took" message from the hub to indicate tokens were taken.
 * Data is of format "playerletter:purple,brown,yellow,red"
 * Message is invalid if input doesn't follow format above, or if the input has
 * any whitespcace.
 *
 * @param game The game struct
 * @param data The message data associated with the command from hub
 * @return Result: NORMAL for success, COMMUNICATION_ERROR if message invalid.
 */
Result handle_token_take(Game* game, char* data) {
    char player;
    int tokens[NUM_COLOURS];
    int result = sscanf(data, "%c:%d,%d,%d,%d", &player, &tokens[PURPLE],
            &tokens[BROWN], &tokens[YELLOW], &tokens[RED]);

    // Check input is valid
    if (has_any_whitespace(data) || result != 5 ||
            !is_valid_player_char(player, game->numPlayers)) {
        return COMMUNICATION_ERROR;
    }

    player_took_tokens(game, player_char_to_int(player), tokens);

    print_game_status(game);
    return NORMAL;
}

/*
 * Handles a "tokens" message from the hub to indicate initial tokens on board.
 * Data consists of just a single integer.
 * Message is invalid if there is any whitespace or data cannot be parsed to
 * an integer, or the input is negative.
 *
 * @param game The game struct
 * @param data The message data associated with the command from hub
 * @return Result: NORMAL for success, COMMUNICATION_ERROR if message invalid.
 */
Result handle_initial_tokens(Game* game, char* data) {
    int initialTokens = 0;

    // Check for invalid input
    if (has_any_whitespace(data) || !str_to_int(data, &initialTokens)
            || initialTokens < 0) {
        return COMMUNICATION_ERROR;
    }

    set_initial_game_tokens(game, initialTokens);

    print_game_status(game);
    return NORMAL;
}

/*
 * Handles an "eog" message. Has no input data.
 * Prints winners to stderr.
 *
 * @param game The game struct
 * @return Result of call. Always END_OF_GAME
 */
Result handle_end_of_game(Game* game) {
    char dest[60] = "";

    get_winners(game, dest);

    fprintf(stderr, "Game over. Winners are %s\n", dest);
    return END_OF_GAME;
}

/*
 * Handles a "dowhat" message from the hub.
 * Prints "received dowhat" to stderr, and runs the choose_move function,
 * which is implemented by a player type.
 *
 * @param game The game struct
 * @return Result of turn. Always NORMAL.
 */
Result handle_your_turn(Game* game) {
    fprintf(stderr, "Received dowhat\n");

    choose_move(game);
    return NORMAL;
}

/*
 * Handles a message from the hub.
 *
 * @param game The game struct
 * @param input The message received to stdin from hub.
 * @return Result of message handling.
 *         NORMAL if okay, COMMUNICATION_ERROR is message invalid, or
 *         END_OF_GAME is the game is over.
 */
Result handle_input(Game* game, char* input) {
    if (starts_with(input, "tokens")) {
        return handle_initial_tokens(game, input + strlen("tokens"));
    } else if (starts_with(input, "newcard")) {
        return handle_newcard(game, input + strlen("newcard"));
    } else if (starts_with(input, "purchased")) {
        return handle_card_purchased(game, input + strlen("purchased"));
    } else if (starts_with(input, "took")) {
        return handle_token_take(game, input + strlen("took"));
    } else if (starts_with(input, "wild")) {
        return handle_wild_take(game, input + strlen("wild"));
    } else if (strcmp(input, "eog") == 0) {
        return handle_end_of_game(game);
    } else if (strcmp(input, "dowhat") == 0) {
        return handle_your_turn(game);
    }

    // Not a valid message
    return COMMUNICATION_ERROR;
}

/*
 * Runs the main loop of the player process.
 * Takes input from stdin and parses/handles each line until
 * stdin is closed.
 *
 * @param game The game struct
 * @return Final result of loop. COMMUNICATION_ERROR is a message inputted
 *         was invalid, or EOF was reached before a game over.
 *         END_OF_GAME if game ended normally.
 */
Result run_game_loop(Game* game) {
    char inputBuffer[BUFFER_SIZE];

    while (true) {
        if (!get_input(inputBuffer, BUFFER_SIZE, stdin)) {
            return COMMUNICATION_ERROR;
        }

        Result res = handle_input(game, inputBuffer);

        if (res != NORMAL) {
            return res;
        }
    }
    // Reached EOF before eog message
    return COMMUNICATION_ERROR;
}

/*
 * Main Function of player process
 */
int main(int argc, char** argv) {
    if (argc != 3) {
        exit_player(NULL, WRONG_NUM_ARGS);
    }

    // parse playercount and playerid arguments and ensure valid
    int numPlayers = 0, playerId = 0;

    // Check first argument, playercount, is valid
    if (!str_to_int(argv[1], &numPlayers) || numPlayers < 2
            || numPlayers > MAX_PLAYERS) {
        exit_player(NULL, INVALID_PLAYER_COUNT);
    }

    // Check second argument, playerId, is valid
    if (!str_to_int(argv[2], &playerId) || playerId < 0
            || playerId > numPlayers - 1) {
        exit_player(NULL, INVALID_PLAYER_ID);
    }

    // Setup game struct and run loop
    Game* game = setup_game(numPlayers);
    game->myId = playerId;

    Result result = run_game_loop(game);

    // We get here when game is over, either normally or due to error
    exit_player(game, result);

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shared.h"

/* 2310 library */
#include <game.h>
#include <protocol.h>
#include <token.h>
#include <util.h>
#include <player.h>

/*
 * Defines exit codes for program
 */
typedef enum {
    NORMAL_EXIT = 0,
    WRONG_ARGS = 1, // Wrong number of arguments when starting
    BAD_KEYFILE = 2, // Keyfile couldn't be loaded/is invalid
    BAD_NAME = 3, // Player/game name is invalid
    BAD_CONNECTION = 5, // Couldn't connect to server
    BAD_AUTH = 6, // Server rejected authentication key
    BAD_RECONNECT_ID = 7, // Server rejected reconnect id
    COM_ERROR = 8, // Server sent invalid message
    PLAYER_DISCONNECT = 9, // Player in game disconnected, causing eog
    PLAYER_MISBEHAVE = 10 // Player in game sent invalid messages, causing eog
} ExitCode;

/*
 * Exits program with given exit code, and prints an info message to
 * stderr if needed.
 */
void exit_program(ExitCode code) {
    switch (code) {
        case WRONG_ARGS:
            fprintf(stderr, "Usage: zazu keyfile port game pname\n");
            break;
        case BAD_KEYFILE:
            fprintf(stderr, "Bad key file\n");
            break;
        case BAD_NAME:
            fprintf(stderr, "Bad name\n");
            break;
        case BAD_CONNECTION:
            fprintf(stderr, "Failed to connect\n");
            break;
        case BAD_AUTH:
            fprintf(stderr, "Bad auth\n");
            break;
        case BAD_RECONNECT_ID:
            fprintf(stderr, "Bad reconnect id\n");
            break;
        case COM_ERROR:
            fprintf(stderr, "Communication Error\n");
            break;
        default:
            break;
    }

    exit(code);
}

/*
 * Prompts a player (via stdin) for info about their requested card purchase.
 * Asks for the card id wanting to buy, (must be from 0-7), and then
 * asks for how much of each token wanting to use to buy that card.
 * Will continually reprompt until a valid non-negative integer is inputted
 * at each step.
 *
 * Returns a populated PurchaseMessage with details of the requested purchase
 * ready to send to server. Note that even if the token numbers given are
 * insufficient to buy the card, the function will still return normally as it
 * only cares that inputs are validly formatted.
 */
struct PurchaseMessage prompt_purchase(struct GameState* game) {
    struct PurchaseMessage msg = {0};
    char* line;

    while (true) {
        printf("Card> ");
        fflush(stdout);

        if (read_line(stdin, &line, 0) <= 0) {
            free(line);
            continue; // Reprompt
        }

        int card;
        if (!str_to_int(line, &card) || card < 0 || card > 7) {
            free(line);
            continue; // Reprompt
        }
        msg.cardNumber = card;
        break; // Card number valid, now move on
    }

    for (int i = 0; i < TOKEN_MAX; i++) {
        if (game->players[game->selfId].tokens[i] > 0) {
            while (true) {
                printf("Token-%c> ", print_token(i));
                fflush(stdout);

                if (read_line(stdin, &line, 0) <= 0) {
                    free(line);
                    continue; // Reprompt
                }

                int cost;
                if (!str_to_int(line, &cost) || cost < 0
                        || cost > game->players[game->selfId].tokens[i]) {
                    free(line);
                    continue; // Reprompt
                }
                msg.costSpent[i] = cost;
                break; // Done with this token, move on
            }
        }
    }
    return msg;
}

/*
 * Prompts a player (via stdin) for info about their requested token take.
 * Asks for how many of each token colour to take (order P, B, Y, R), and will
 * continually reprompt until a valid input is received. A valid input is a
 * non-negative integer up to how many is available in given game.
 *
 * Returns a populated TakeMessage struct with info about player's request
 * after all prompting is done.
 */
struct TakeMessage prompt_take(struct GameState* game) {
    struct TakeMessage msg = {{0}};

    for (int i = 0; i < TOKEN_MAX - 1; i++) {
        while (true) {
            printf("Token-%c> ", print_token(i));
            fflush(stdout);

            char* line;
            if (read_line(stdin, &line, 0) <= 0) {
                free(line);
                continue; // Reprompt
            }

            int take;
            if (!str_to_int(line, &take) || take < 0
                    || take > game->tokenCount[i]) {
                fflush(stdout);
                free(line);
                continue;
            }

            msg.tokens[i] = take;
            free(line);
            break;
        }
    }

    return msg;
}

/*
 * Prompts a player (via stdin) for info about their action after a "dowhat".
 * Will continue to reprompt until either "take", "purchase" or "wild" is
 * received.
 *
 * Returns a MessageFromPlayer to represent which action the user desires.
 */
enum MessageFromPlayer prompt_action(void) {
    while (true) {
        printf("Action> ");
        fflush(stdout);

        char* line;
        if (read_line(stdin, &line, 0) <= 0) {
            free(line);
            continue; // Reprompt
        }

        enum MessageFromPlayer res;
        if (strcmp(line, "purchase") == 0) {
            res = PURCHASE;
        } else if (strcmp(line, "take") == 0) {
            res = TAKE;
        } else if (strcmp(line, "wild") == 0) {
            res = WILD;
        } else {
            free(line);
            continue; // Reprompt
        }

        free(line);
        return res;
    }
}

/*
 * Handles a "dowhat" from the server. Prompts user for their response via
 * stdin, and then sends formatted request to server.
 */
void handle_dowhat(struct GameState* game, FILE* toServer) {
    enum MessageFromPlayer action = prompt_action();

    if (action == WILD) {
        fprintf(toServer, "wild\n");
        fflush(toServer);
    } else if (action == PURCHASE) {
        struct PurchaseMessage msg = prompt_purchase(game);

        char* encoded = print_purchase_message(msg);
        fprintf(toServer, encoded);
        fflush(toServer);

        free(encoded);
    } else if (action == TAKE) {
        struct TakeMessage msg = prompt_take(game);

        char* encoded = print_take_message(msg);
        fprintf(toServer, encoded);
        fflush(toServer);

        free(encoded);
    }
}

/*
 * Handles some game-related messages from the server. Takes the game state,
 * a MessageFromHub as the type of message received,
 * the full input line received from the server, and the server to write back
 * to if needed.

 * If the given message is invalid, then COMMUNICATION_ERROR is returned.
 * If the message was and end of game message, then INTERRUPTED is returned.
 * If another player disconnected, then PLAYER_CLOSED is returned.
 * If another player sent an invalid move twice in a row, then ILLEGAL_MOVE
 * is returned.
 * Otherwise, if the message was successfully parsed and handles, and the
 * game should continue, then NOTHING_WRONG is returned.
 */
enum ErrorCode handle_game_message(struct GameState* game,
        enum MessageFromHub type, char* line, FILE* toServer) {

    int player;
    switch (type) {
        case DO_WHAT:
            printf("Received dowhat\n");
            handle_dowhat(game, toServer);
            return NOTHING_WRONG;
        case PURCHASED:
            return handle_purchased_message(game, line);
        case TOOK:
            return handle_took_message(game, line);
        case TOOK_WILD:
            return handle_took_wild_message(game, line);
        case NEW_CARD:
            return handle_new_card_message(game, line);
        case END_OF_GAME:
            display_eog_info(game);
            return INTERRUPTED;
        case DISCO:
            if (parse_disco_message(&player, line) == -1) {
                return COMMUNICATION_ERROR;
            }

            fprintf(stderr, "Player %c disconnected\n", 'A' + player);
            return PLAYER_CLOSED;
        case INVALID:
            if (parse_invalid_message(&player, line) == -1) {
                return COMMUNICATION_ERROR;
            }

            fprintf(stderr, "Player %c sent invalid message\n", 'A' + player);
            return ILLEGAL_MOVE;
        default:
            return COMMUNICATION_ERROR;
    }
}

/*
 * Runs the main game loop. Takes the game state, and server file pointer.
 *
 * Returns NORMAL_EXIT on normal end of game, PLAYER_DISCONNECT if game
 * ended due to another player disconnecting, PLAYER_MISBEHAVE if game
 * ended due to another player sending invalid messages to the server
 * twice in a row, or COM_ERROR if the server send an invalid message.
 */
ExitCode play_game(struct GameState* game, FILE* input, FILE* output) {
    while (true) {
        char* line;
        if (read_line(input, &line, 0) <= 0) {
            free(line);
            return COM_ERROR;
        }

        enum MessageFromHub type = classify_from_hub(line);
        enum ErrorCode err = handle_game_message(game, type, line, output);

        free(line);

        switch (err) {
            case NOTHING_WRONG:
                if (type != DO_WHAT) {
                    display_turn_info(game);
                }
                break;
            case COMMUNICATION_ERROR:
                return COM_ERROR;
            case PLAYER_CLOSED:
                return PLAYER_DISCONNECT;
            case ILLEGAL_MOVE:
                return PLAYER_MISBEHAVE;
            case INTERRUPTED:
                return NORMAL_EXIT; // end of game
            default:
                break;
        }
    }
}

/*
 * Reads an expected "rid" message from the server, and parses it.
 * If message is invalid, i.e the message isn't of form "ridR" where R
 * is reconnect id, then returns COM_ERROR.
 * If message is valid, then R/reconnect id is printed to stdout, and
 * NORMAL_EXIT is returned.
 */
ExitCode handle_rid_message(FILE* input) {
    char* line;
    if (read_line(input, &line, 0) <= 0) {
        free(line);
        return COM_ERROR;
    }

    // Check if string doesn't start with "rid", or is just "rid" with no
    // reconnect id following.
    if (strncmp(line, "rid", strlen("rid")) != 0
            || strlen(line) == strlen("rid")) {
        free(line);
        return COM_ERROR;
    }

    // print just the reconnect id part of the message
    printf("%s\n", line + strlen("rid"));
    free(line);
    return NORMAL_EXIT;
}

/*
 * Reads and handles a "playinfo" message from server, and parses it.
 * On success, updates the game by setting the self id of current player,
 * and initialising player array to the number of players given.
 *
 * If valid and everything was successful, NORMAL_EXIT is returned.
 * Otherwise, COM_ERROR is returned to indicate an invalid server message.
 */
ExitCode handle_playinfo_message(struct GameState* game, FILE* server) {
    game->players = NULL;
    char* line;
    if (read_line(server, &line, 0) <= 0) {
        free(line);
        return COM_ERROR;
    }

    char playerLetter;
    int numPlayers;

    if (sscanf(line, "playinfo%c/%d", &playerLetter, &numPlayers) != 2) {
        free(line);
        return COM_ERROR;
    }
    free(line);

    if (playerLetter < 'A' || playerLetter > 'Z' ||
            (playerLetter - 'A') >= numPlayers ||
            numPlayers < MIN_PLAYERS || numPlayers > MAX_PLAYERS) {
        return COM_ERROR;
    }

    game->playerCount = numPlayers;
    game->selfId = playerLetter - 'A';

    game->players = malloc(sizeof(struct Player) * game->playerCount);
    for (int i = 0; i < game->playerCount; ++i) {
        initialize_player(&game->players[i], i);
    }

    return NORMAL_EXIT;
}

/*
 * Reads and then handles a "token" message from the server. Sets the token
 * count in all the piles in the game.
 *
 * Returns NORMAL_EXIT if all valid and successful, otherwise COM_ERROR if
 * message read wasn't a tokens message, or if it was invalid in any other
 * way.
 */
ExitCode handle_tokens_message(struct GameState* game, FILE* fromServer) {
    char* line;
    if (read_line(fromServer, &line, 0) <= 0) {
        return COM_ERROR;
    }

    int tokens;
    if (parse_tokens_message(&tokens, line) != 0) {
        free(line);
        return COM_ERROR;
    }
    free(line);

    for (int i = 0; i < TOKEN_MAX - 1; ++i) {
        game->tokenCount[i] = tokens;
    }
    return NORMAL_EXIT;
}

/*
 * Sets up a player's data after receiving a player message after
 * reconnecting. Sets the total points, discounts, and tokens for given
 * player id in given game.
 */
void setup_player_data(struct GameState* game, int playerId,
        int points, int discounts[TOKEN_MAX - 1], int tokens[TOKEN_MAX]) {

    game->players[playerId].score += points;

    for (int i = 0; i < TOKEN_MAX - 1; i++) {
        game->players[playerId].discounts[i] += discounts[i];
    }

    for (int i = 0; i < TOKEN_MAX; i++) {
        game->players[playerId].tokens[i] += tokens[i];
        game->tokenCount[i] -= tokens[i];
    }
}

/*
 * Reads and handles the catchup messsages. Reads all newcard messages
 * until the first "player" message. Reads a "player" message for every
 * player in the game, and they must be in order otherwise invalid.
 *
 * Returns NORMAL_EXIT if everything is valid, otherwise COM_ERROR.
 */
ExitCode handle_catchup_messages(struct GameState* game, FILE* input) {
    char* line;

    while (true) {
        read_line(input, &line, 0);
        // Check string starts with "newcard"
        if (strncmp(line, "newcard", strlen("newcard")) == 0) {
            if (handle_new_card_message(game, line) > 0) {
                free(line);
                return COM_ERROR;
            }
            display_turn_info(game);
            free(line);
        } else {
            break;
        }
    }

    for (int i = 0; i < game->playerCount; i++) {
        if (i > 0) { // On first loop, line will be read already from above
            read_line(input, &line, 0);
        }

        char playerLetter;
        int totalPoints, discounts[TOKEN_MAX - 1], tokens[TOKEN_MAX];

        int result = sscanf(line, "player%c:%d:d=%d,%d,%d,%d:t=%d,%d,%d,%d,%d",
                &playerLetter, &totalPoints, &discounts[TOKEN_PURPLE],
                &discounts[TOKEN_BROWN], &discounts[TOKEN_YELLOW],
                &discounts[TOKEN_RED], &tokens[TOKEN_PURPLE],
                &tokens[TOKEN_BROWN], &tokens[TOKEN_YELLOW],
                &tokens[TOKEN_RED], &tokens[TOKEN_WILD]);

        if (result != 11 || playerLetter != 'A' + i) {
            free(line);
            return COM_ERROR;
        }
        setup_player_data(game, i, totalPoints, discounts, tokens);
        display_player_state(game->players[i]);
        free(line);
    }

    return NORMAL_EXIT;
}

/*
 * Sets up the game state by parsing server messages after successful
 * connection/reconnection and authenatication.
 * Handles the "rid" message (if not reconnecting), the "playinfo" message,
 * the initial "tokens" message, and if reconnecting, handles the catchup
 * "newcard"/"player" messages. Returns COM_ERROR if anything was invalid,
 * otherwise returns NORMAL_EXIT after printing all game info.
 */
ExitCode setup_game(struct GameState* game, FILE* server, bool isReconnect) {
    if (!isReconnect) {
        if (handle_rid_message(server)) {
            return COM_ERROR;
        }
    }

    if (handle_playinfo_message(game, server)) {
        return COM_ERROR;
    }
    display_turn_info(game);

    if (handle_tokens_message(game, server)) {
        return COM_ERROR;
    }
    display_turn_info(game);

    if (isReconnect) {
        if (handle_catchup_messages(game, server)) {
            return COM_ERROR;
        }
    } else {
        game->boardSize = 0;
    }

    return NORMAL_EXIT;
}

/*
 * Connects to rafiki server on localhost at given port.
 * If connection wasn't successful, BAD_CONNECTION is returned.
 *
 * Once connected, will attempt to authenticate with given key. If auth is
 * unsuccessful, BAD_AUTH is returned.
 *
 * If the player is reconnecting (i.e. player is "reconnect"), then it will
 * send the reconnect id. If server doesn't accept, BAD_RECONNECT_ID returns.
 * Otherwise, if player is connecting normally, the game name and player
 * name will be sent to the server.
 * If everything is successful, NORMAL_EXIT is returned, and file pointer to
 * write to the server will be stored in destTo, and the file to read from
 * the server will be stored at destFrom.
 */
ExitCode connect_to_server(FILE** destFrom, FILE** destTo, char* key,
        char* port, char* game, char* player, bool isReconnect) {
    int serverfd = connect_to(NULL, port);
    if (serverfd == -1) {
        return BAD_CONNECTION;
    }

    FILE* fromServer = fdopen(serverfd, "r");
    FILE* toServer = fdopen(serverfd, "w");

    // send auth to server
    fprintf(toServer, "%s%s\n", isReconnect ? "reconnect" : "play", key);
    fflush(toServer);

    char* response;
    if (read_line(fromServer, &response, 0) <= 0) {
        free(response);
        return COM_ERROR;
    }

    // Ensure response is "yes", otherwise authentication failed.
    if (strcmp(response, "yes") != 0) {
        free(response);
        return BAD_AUTH;
    }
    free(response);

    if (isReconnect) {
        fprintf(toServer, "rid%s\n", player);
        fflush(toServer);

        if (read_line(fromServer, &response, 0) <= 0) {
            free(response);
            return COM_ERROR;
        }

        if (strcmp(response, "yes") != 0) {
            free(response);
            return BAD_RECONNECT_ID;
        }
        free(response);
    } else {
        fprintf(toServer, "%s\n%s\n", game, player);
        fflush(toServer);
    }

    *destTo = toServer;
    *destFrom = fromServer;
    return NORMAL_EXIT;
}

int main(int argc, char** argv) {
    if (argc != 5) {
        exit_program(WRONG_ARGS);
    }

    // load authentication key from keyfile
    char* key;
    if (!get_keyfile(argv[1], &key)) {
        exit_program(BAD_KEYFILE);
    }
    bool isReconnect = (strcmp(argv[3], "reconnect") == 0);

    if (!is_valid_game_name(argv[3]) ||
            (!isReconnect && !is_valid_game_name(argv[4]))) {
        free(key);
        exit_program(BAD_NAME);
    }

    FILE* fromServer;
    FILE* toServer;
    ExitCode err = connect_to_server(&fromServer, &toServer, key, argv[2],
            argv[3], argv[4], isReconnect);

    free(key);

    if (err != NORMAL_EXIT) {
        exit_program(err);
    }

    struct GameState game;
    memset(&game, 0, sizeof(game));

    err = setup_game(&game, fromServer, isReconnect);

    // If game setup normally, play it.
    if (err == NORMAL_EXIT) {
        err = play_game(&game, fromServer, toServer);
    }

    free(game.players);
    fclose(fromServer);
    fclose(toServer);

    exit_program(err);
}

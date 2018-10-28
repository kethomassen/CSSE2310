#include "lib/util.h"
#include "lib/game.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

/* Ends of pipe file descriptor arrays */
#define READ_END 0
#define WRITE_END 1

/* Wait time before killing children after sending end of game */
#define CHILD_KILL_WAIT 2

#define MSG_WILD_TAKE "wild"
#define MSG_TOKEN_TAKE "take"
#define MSG_CARD_PURCHASE "purchase"

/*
 * Represents a result/exit code from the hub
 */
typedef enum {
    NORMAL = 0,
    WRONG_NUM_ARGS = 1,
    INVALID_ARGS = 2,
    DECKFILE_UNREADABLE = 3,
    DECKFILE_INCORRECT = 4,
    PLAYER_START_FAIL = 5,
    PLAYER_DISCONNECT = 6,
    BAD_PROTOCOL = 7,
    SIGINT_CAUGHT = 10
} Result;

// global game variable to allow use in signal handler
Game* globalGame;

/*
 * Sends a formatted message to all players in game.
 *
 * @param game The game struct
 * @param format String format
 * @param args Formatting arguments
 */
void send_message_all(Game* game, const char* format, ...) {
    va_list args;

    for (int i = 0; i < game->numPlayers; i++) {
        va_start(args, format);
        if (game->players[i].in != NULL) {
            vfprintf(game->players[i].in, format, args);
            fflush(game->players[i].in);
        }
    }

    va_end(args);
}

/*
 * Ends game and kills player processes.
 * Prints winners, then sends "eog" to all players and waits 2 seconds.
 * If any processes haven't terminated yet, they will be sent SIGKILL.
 *
 * @param game The game struct
 * @param code Exit code of hub
 */
void kill_players(Game* game, Result code) {
    if (code == NORMAL) {
        char dest[60] = "";
        get_winners(game, dest);
        fprintf(stdout, "Winner(s) %s\n", dest);
    }

    // Can only send eog if some players have actually started up.
    if (code == NORMAL || code >= PLAYER_START_FAIL) {
        send_message_all(game, "eog\n");
    }

    // If no player processes were ever started, no more to do.
    if (code != NORMAL && code < PLAYER_START_FAIL) {
        return;
    }

    // wait 2 seconds before attempting to kill any alive processes
    sleep(CHILD_KILL_WAIT);

    for (int i = 0; i < game->numPlayers; i++) {
        int pid = game->players[i].pid;
        int exitStatus;

        if (waitpid(pid, &exitStatus, WNOHANG) == 0) {
            // child still running. Kill it.
            kill(pid, SIGKILL);
        }
        waitpid(pid, &exitStatus, WNOHANG); // get new exit status after kill

        // Print exit statuses if all players started, and no sigint caught
        if (code > PLAYER_START_FAIL && code != SIGINT_CAUGHT) {
            if (WIFEXITED(exitStatus)) {
                if (WEXITSTATUS(exitStatus) != 0) {
                    fprintf(stderr, "Player %c ended with status %d\n",
                            player_int_to_char(i), WEXITSTATUS(exitStatus));
                }
            } else if (WIFSIGNALED(exitStatus)) {
                fprintf(stderr, "Player %c shutdown after receiving "
                        "signal %d\n",
                        player_int_to_char(i), WTERMSIG(exitStatus));
            }
        }
    }
}

/*
 * Exits hub with given code.
 * Prints to stderr and shuts down players if neccessary.
 *
 * @param game The game struct
 * @param code Exit reason
 */
void exit_game(Game* game, Result code) {
    switch(code) {
        case WRONG_NUM_ARGS:
            fprintf(stderr, "Usage: austerity tokens points deck "
                    "player player [player ...]\n");
            break;
        case INVALID_ARGS:
            fprintf(stderr, "Bad argument\n");
            break;
        case DECKFILE_UNREADABLE:
            fprintf(stderr, "Cannot access deck file\n");
            break;
        case DECKFILE_INCORRECT:
            fprintf(stderr, "Invalid deck file contents\n");
            break;
        case PLAYER_START_FAIL:
            fprintf(stderr, "Bad start\n");
            break;
        case PLAYER_DISCONNECT:
            printf("Game ended due to disconnect\n");
            fflush(stdout);
            fprintf(stderr, "Client disconnected\n");
            break;
        case BAD_PROTOCOL:
            fprintf(stderr, "Protocol error by client\n");
            break;
        case SIGINT_CAUGHT:
            fprintf(stderr, "SIGINT caught\n");
            break;
        default:
            break;
    }

    kill_players(game, code);
    free_game(game);
    exit(code);
}

/*
 * SIGINT handler. Exits game with SIGINT_CAUGHT exit code.
 *
 * @param sig Signal id
 */
void handle_sigint(int sig) {
    exit_game(globalGame, SIGINT_CAUGHT);
}

/*
 * Sets up a child player process and sets up pipes to its stdin/stdout.
 * After forking, the child process with exec the given path.
 * If exec fails, it will write to a checking pipe to indicate exec failed.
 * The parent process will check for this, and if anything is read,
 * PLAYER_START_FAIL will be returned. The player's associated pid,
 * and in/out streams will be setup by parent process if successful.
 *
 * @param game The game struct
 * @param numPlayers The number of players in the game
 * @param playerId Identifier (0-25) of current player
 * @param path Pathname of executable of child process
 * @return NORMAL on success, PLAYER_START_FAIL on failure to start properly
 */
Result setup_child(Game* game, int numPlayers, int playerId, char* path) {
    int toPipe[2], fromPipe[2], checkPipe[2];
    pid_t pid;

    if (pipe(toPipe) == -1 || pipe(fromPipe) == -1 || pipe(checkPipe) == -1) {
        return PLAYER_START_FAIL; // System call error while piping
    }

    if ((pid = fork()) == 0) { // Child process
        int devNull = open("/dev/null", O_WRONLY);
        dup2(toPipe[READ_END], STDIN_FILENO);
        dup2(fromPipe[WRITE_END], STDOUT_FILENO);
        dup2(devNull, STDERR_FILENO); // Hide child process' stderr.

        close(toPipe[WRITE_END]); // Close opposite sides of the pipes
        close(fromPipe[READ_END]);
        close(checkPipe[READ_END]);
        fcntl(checkPipe[WRITE_END], F_SETFD, FD_CLOEXEC);

        char id[3], playerCount[3]; // Strings to hold two numbers and \0
        sprintf(id, "%d", playerId); // Convert playerId to string
        sprintf(playerCount, "%d", numPlayers); // Convert numPlayers to str

        execlp(path, path, playerCount, id, NULL);
        write(checkPipe[WRITE_END], "!", 1); // Write arbritrary char to check
    } else if (pid > 0) { // Parent process
        game->players[playerId].pid = pid;
        game->players[playerId].in = fdopen(toPipe[WRITE_END], "w");
        game->players[playerId].out = fdopen(fromPipe[READ_END], "r");

        fcntl(toPipe[WRITE_END], F_SETFD, FD_CLOEXEC);
        fcntl(fromPipe[READ_END], F_SETFD, FD_CLOEXEC);
        close(toPipe[READ_END]); // close opposite sides of the pipes
        close(fromPipe[WRITE_END]);
        close(checkPipe[WRITE_END]);

        // Try to read from the checkpipe. This will only receive any data
        // if exec failed
        char test = 0;
        if (read(checkPipe[READ_END], &test, 1) != 0) {
            return PLAYER_START_FAIL; // exec failed
        }

        return NORMAL;
    }
    return PLAYER_START_FAIL; // fork failed
}

/*
 * Starts child player processes.
 *
 * @param game The game struct
 * @param numPlayers The number of players in the game
 * @param paths Array of paths to player executables
 * @return NORMAL on success, PLAYER_START_FAIL if any players failed to start
 */
Result setup_children(Game* game, int numPlayers, char** paths) {
    for (int playerId = 0; playerId < numPlayers; playerId++) {
        Result r = setup_child(game, numPlayers, playerId, paths[playerId]);

        if (r != NORMAL) {
            return r;
        }
    }

    return NORMAL;
}

/*
 * Loads a deckfile into game struct.
 * Each line of deckfile is formatted as D:V:P,B,Y,R, where D is the
 * discount colour, V is the value, and P, B, Y, R are the prices in tokens for
 * Purple, Brown, Yellow, Red respectively.
 * Each line is seperated by a newline and the last line can either
 * have a newline or just EOF.
 * Deck is ordered in same way as deckfile, so first line of deckfile
 * will be the top of the deck.

 *
 * @param game The game struct
 * @param filename Path to deckfile
 * @return Result of deckfile load.
 *         NORMAL on success, DECKFILE_UNREADABLE if filename can't be opened,
 *         DECKFILE_INCORRECT if file incorrectly formatted.
 *
 */
Result load_deckfile(Game* game, char* filename) {
    FILE* deckfile = fopen(filename, "r");
    if (deckfile == NULL) {
        return DECKFILE_UNREADABLE;
    }

    char lineBuffer[BUFFER_SIZE];
    while (get_input(lineBuffer, BUFFER_SIZE, deckfile)) {

        // Handle deckfile ending in newline
        if (strlen(lineBuffer) == 0) {
            // If deckfile ends in newline, then EOF must be next
            if (fgetc(deckfile) == EOF) {
                break;
            }
            return DECKFILE_INCORRECT;
        }

        Card card;
        if (!create_card(lineBuffer, &card)
                || has_any_whitespace(lineBuffer)) {
            return DECKFILE_INCORRECT;
        }

        DeckCard* newCard = malloc(sizeof(DeckCard));
        newCard->card = card;
        newCard->next = NULL;

        if (game->topOfDeck == NULL) {
            // If deck is empty, add it top
            game->topOfDeck = newCard;
        } else {
            // Otherwise, traverse to bottom of deck, and add it
            DeckCard* cur = game->topOfDeck;
            while (cur->next != NULL) {
                cur = cur->next;
            }
            cur->next = newCard;
        }

    }

    fclose(deckfile);

    if (game->topOfDeck == NULL) {
        return DECKFILE_INCORRECT;
    }
    return NORMAL;
}

/*
 * Takes a card from top of deck and faces it up on the board.
 * Tells players that a new card was added to market and prints to stdout.
 *
 * Does nothing if board is already full or deck is empty.
 *
 * @param game The game struct
 */
void take_card_from_deck(Game* game) {
    // Do nothing if board full, or deck empty
    if (game->cardsFacedUp == MAX_CARDS_ON_BOARD || game->topOfDeck == NULL) {
        return;
    }

    Card card = game->topOfDeck->card;

    add_card_to_board(game, card);

    send_message_all(game, "newcard%c:%d:%d,%d,%d,%d\n",
            get_card_char(card.discount), card.value,
            card.price[PURPLE], card.price[BROWN],
            card.price[YELLOW], card.price[RED]);

    printf("New card = Bonus %c, worth %d, costs %d,%d,%d,%d\n",
            get_card_char(card.discount), card.value,
            card.price[PURPLE], card.price[BROWN],
            card.price[YELLOW], card.price[RED]);
    fflush(stdout);

    // Take card off deck and free memory associated with it
    DeckCard* oldTop = game->topOfDeck;
    game->topOfDeck = oldTop->next;
    free(oldTop);
}

/*
 * Sets up a new game by setting the initial tokens and facing up cards,
 * and setting the number of points needed to win.
 * Tells players how many tokens are in the piles and the details of cards
 * now on market.
 *
 * @param game The game struct
 * @param initialTokens The initial number of tokens on board piles
 * @param maxPoints The number of points a player needs to win
 */
void start_new_game(Game* game, int initialTokens, int maxPoints) {
    game->maxPoints = maxPoints;

    set_initial_game_tokens(game, initialTokens);
    send_message_all(game, "tokens%d\n", initialTokens);

    for (int i = 0; i < MAX_CARDS_ON_BOARD; i++) {
        take_card_from_deck(globalGame);
    }
}

/*
 * Handles a player asking to take a wild.
 * Updates internal game state, prints to stdout
 * and tells players who took wild.
 *
 * @param game The game struct
 * @param playerId Player asking to take wild
 */
void handle_wild_take(Game* game, int playerId) {
    player_took_wild(game, playerId);
    send_message_all(game, "wild%c\n", player_int_to_char(playerId));

    printf("Player %c took a wild\n", player_int_to_char(playerId));
}

/*
 * Handles a player asking to take tokens
 * Updates internal game state, prints to stdout
 * and tells players who took tokens.
 *
 * Doesn't check if token take is a valid move.

 * @param game The game struct
 * @param playerId Player asking to take tokens
 * @param tokens The tokens the player is taking
 */
void handle_token_take(Game* game, int playerId, int tokens[NUM_COLOURS]) {
    player_took_tokens(game, playerId, tokens);
    send_message_all(game, "took%c:%d,%d,%d,%d\n",
            player_int_to_char(playerId),
            tokens[PURPLE], tokens[BROWN], tokens[YELLOW], tokens[RED]);

    printf("Player %c drew %d,%d,%d,%d\n", player_int_to_char(playerId),
            tokens[PURPLE], tokens[BROWN], tokens[YELLOW], tokens[RED]);
}

/*
 * Handles a player asking to purchase a card.
 * Updates internal game state, prints to stdout
 * and tells players who purchases which card.
 *
 * Doesn't check if purchase is a valid move.

 * @param game The game struct
 * @param playerId Player asking to purchase card
 * @param cardId Card faced up to be purchased
 * @param tokens The tokens the player is using to purchase card
 */
void handle_card_purchase(Game* game, int playerId, int cardId,
        int tokens[TOKEN_SLOTS]) {

    send_message_all(game, "purchased%c:%d:%d,%d,%d,%d,%d\n",
            player_int_to_char(playerId), cardId,
            tokens[PURPLE], tokens[BROWN], tokens[YELLOW], tokens[RED],
            tokens[WILD]);

    printf("Player %c purchased %d using %d,%d,%d,%d,%d\n",
            player_int_to_char(playerId), cardId,
            tokens[PURPLE], tokens[BROWN], tokens[YELLOW], tokens[RED],
            tokens[WILD]);

    player_purchased_card(game, playerId, cardId, tokens);
    take_card_from_deck(game);
}

/*
 * Handles input from a player process.
 * Valid message must either start with "wild", "take" or "purchase".
 *
 * @param game The game struct
 * @param playerId The player input is from
 * @param input Input string received from player process
 * @return true if input was valid and processed, false otherwise
 */
bool handle_input(Game* game, int playerId, char* input) {
    if (strcmp(input, MSG_WILD_TAKE) == 0) {
        handle_wild_take(game, playerId);
        return true;
    } else if (starts_with(input, MSG_TOKEN_TAKE)) {
        char* info = input + strlen(MSG_TOKEN_TAKE); // Get data from msg
        int tokens[NUM_COLOURS];

        int result = sscanf(info, "%d,%d,%d,%d", &tokens[PURPLE],
                &tokens[BROWN], &tokens[YELLOW], &tokens[RED]);

        // Check if invalid input, or dodgy token take
        if (has_any_whitespace(info) || result != 4
                || !is_valid_token_take(game, tokens)) {
            return false;
        }

        handle_token_take(game, playerId, tokens);
        return true;
    } else if (starts_with(input, MSG_CARD_PURCHASE)) {
        char* info = input + strlen(MSG_CARD_PURCHASE); // Get data from msg
        int cardId, tokens[TOKEN_SLOTS];

        int result = sscanf(info, "%d:%d,%d,%d,%d,%d",
                &cardId, &tokens[PURPLE], &tokens[BROWN], &tokens[YELLOW],
                &tokens[RED], &tokens[WILD]);

        // Check if invalid input, or wrong tokens
        if (has_any_whitespace(info) || result != 6
                || cardId < 0 || cardId >= game->cardsFacedUp
                || !can_tokens_buy_card(game, playerId, cardId, tokens)) {
            return false;
        }

        handle_card_purchase(game, playerId, cardId, tokens);
        return true;
    }
    // Input was invalid
    return false;
}

/*
 * Sends a "dowhat" message to a player with given id.
 *
 * @param game The game struct
 * @param playerId Player to send message to
 */
void send_dowhat(Game* game, int playerId) {
    fprintf(game->players[playerId].in, "dowhat\n");
    fflush(game->players[playerId].in);
}

/*
 * Runs the main game loop.
 * Starts from player 0, and goes around to each player, sending them
 * a "dowhat" message to ask for their move. If they send an invalid
 * message, ask them again. If still invalid, end game with protocol error.
 * If a player reaches the max number of points needed, game will end after
 * all players have had their turn in the round. If the game runs out of cards
 * then the game will end immediately when it occurs.
 * If a player disconnects, game will also end when trying to get their input.
 *
 * @param game The game struct
 * @return NORMAL on normal end of game,
 *         BAD_PROTOCOL if player sent invalid message twice in a row,
 *         PLAYER_DISCONNECT if a player disconnected
 */
Result run_game_loop(Game* game) {
    bool maxPointsReached = false;

    while (true) {
        if (maxPointsReached) { // check if game over first.
            return NORMAL;
        }

        for (int curPlayer = 0; curPlayer < game->numPlayers; curPlayer++) {
            // Player gets two attempts to give a valid move.
            bool alreadyAttempted = false;
            while (true) {
                send_dowhat(game, curPlayer); // Ask player for their move

                char buffer[BUFFER_SIZE];
                if (!get_input(buffer, BUFFER_SIZE,
                        game->players[curPlayer].out)) {
                    // EOF reached, they have disconnected
                    return PLAYER_DISCONNECT;
                }

                // Check if input is valid
                if (!handle_input(game, curPlayer, buffer)) {
                    // If they have given an invalid response twice,
                    // it is a protocol error.
                    if (alreadyAttempted) {
                        return BAD_PROTOCOL;
                    }
                    alreadyAttempted = true;
                } else {
                    break; // valid response, quit loop
                }
            }

            // Check if max points been reached
            if (is_game_over(game)) {
                maxPointsReached = true;
            }

            // If no cards left, end game immediately
            if (game->cardsFacedUp == 0) {
                return NORMAL;
            }
        }
    }
    // Won't ever actually reach here as loop only breaks by returning early
    return NORMAL;
}

int main(int argc, char** argv) {
    // Parse command line arguments.
    if (argc < 6 || argc > MAX_PLAYERS + 4) {
        exit_game(NULL, WRONG_NUM_ARGS);
    }

    int initialTokens = 0, maxPoints = 0;
    if (!str_to_int(argv[1], &initialTokens)
            || !str_to_int(argv[2], &maxPoints)
            || initialTokens < 0 || maxPoints < 0) {
        exit_game(NULL, INVALID_ARGS);
    }

    // Setup game struct. argc - 4 == number of players
    Game* game = setup_game(argc - 4);
    globalGame = game;

    // Load deckfile
    Result deckfileStatus = load_deckfile(game, argv[3]);
    if (deckfileStatus != NORMAL) {
        exit_game(game, deckfileStatus);
    }

    // Setup child processes
    Result childStatus = setup_children(game, argc - 4, argv + 4);
    if (childStatus != NORMAL) {
        exit_game(game, childStatus);
    }

    // send initial tokens and newcard messages
    start_new_game(game, initialTokens, maxPoints);

    // Handle SIGINT. Ignore SIGPIPE, as detecting pipe closes by read fails
    struct sigaction sa = {.sa_handler = handle_sigint};
    sigaction(SIGINT, &sa, 0);
    signal(SIGPIPE, SIG_IGN);

    Result finalResult = run_game_loop(game);
    exit_game(game, finalResult);

    return 0;
}

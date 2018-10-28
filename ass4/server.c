#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <stdarg.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>

#include "shared.h"

/* 2310 library */
#include <game.h>
#include <protocol.h>
#include <token.h>
#include <util.h>
#include <player.h>
#include <deck.h>
#include <server.h>

#define MAX_PORT 65535
#define PORT_STRING_SIZE 6
#define MIN_START_TOKENS 1
#define MIN_WIN 1

/*
 * Defines exit codes for program
 */
enum ExitCode {
    NORMAL_EXIT = 0, // Normal exit due to SIGERM
    WRONG_ARGS = 1, // Wrong argument count
    BAD_KEYFILE = 2, // Keyfile not found/invalid
    BAD_DECKFILE = 3, // Deckfile not found/invalid
    BAD_STATFILE = 4, // Statfile not found/invalid
    BAD_TIMEOUT = 5, // Timeout given isn't non-negative integer
    FAILED_LISTEN = 6, // Server couldn't listen on all ports
    SYSTEM_ERROR = 10 // System error, (e.g. malloc failed)
};

/*
 * Holds information from statfile about a port
 */
struct StatfileEntry {
    // Port this entry is associated with. This is replaced with actual
    // port listening on if it is ephemeral.
    int port;
    // Initial tokens in the non-wild piles
    int tokens;
    // Points required to win the game
    int points;
    // Number of players in the game
    int players;
};

/*
 * Holds additional information about a game.
 */
struct GameData {
    // Whether the game has finished
    bool finished;
    // Game counter to indicate how many games with this name been played
    int counter;
    // Initial amount of tokens in each pile
    int initialTokens;
    // Locks
    int reconnectingPlayer;
    // Game thread identifier
    pthread_t tid;
    // Condition to signal to a game that disconnected player reconnected
    pthread_cond_t reconnectWait;
    // Mutex to allow reconnect signals to work
    pthread_mutex_t reconnectLock;
};

/*
 * Holds information about a port listener
 */
struct Listener {
    // Socket listening on
    int listenFd;
    // Connection accepting thread identifier
    pthread_t acceptTid;
};

/*
 * Holds information about a lobby, which is a game that hasn't started yet
 * as it hasn't filled up with players yet.
 */
struct Lobby {
    // Name of game
    char* name;
    // Current amount of clients connected
    int currentClients;
    // Current players in the game
    struct GamePlayer* players;
    // Game details made from port of the first person to connect to lobby
    struct StatfileEntry details;
    // Whether this lobby is still open for players to connect to.
    bool open;
};

/*
 * Holds all the information about the server. A struct is used so it can
 * all easily be passed between threads and functions.
 */
typedef struct {
    // Number of total (past and current) games played on the server.
    int gameCount;
    // Array to hold game information (past and present).
    struct Game* games;
    // Number of lobbies started on this server (past and present)
    int numLobbies;
    // Array to hold lobby information (past and prsent).
    struct Lobby* lobbies;
    // Size of deck loaded from deckfile
    int deckSize;
    // Deck loaded from deckfile
    struct Card* deckEntries;
    // Timeout before ending game due to a disconnect
    int timeout;
    // Key loaded from keyfile
    char* key;
    // Number of entries in statfile loaded.
    int statfileSize;
    // Entries loaded from statfile,
    struct StatfileEntry* statfileEntries;
    // Current port listeners.
    struct Listener* listeners;
    // Mutex to prevent race conditions when players join games
    pthread_mutex_t joinLobbyLock;
    // Mutex to prevent race conditions when shutting down server
    pthread_mutex_t shutdownLock;
} Server;

/*
 * Arguments sent to a connection accept thread.
 */
struct AcceptThreadArg {
    // Server details
    Server* server;
    // Listener id which this thread should accept connections for
    struct StatfileEntry details;
    // Id of listener to get socket which thread is accepting for
    int whichListener;
};

/*
 * Arguments sent to a connection handler thread after a client connects.
 */
struct ConnectionThreadArg {
    // Server details
    Server* server;
    // Socket fd of connected client
    int connectedFd;
    // Information from statfile corresponding to port client connected via.
    struct StatfileEntry details;
};

/*
 * Arguments sent to a game thread when the game is started.
 */
struct GameThreadArg {
    // Server details
    Server* server;
    // Id of game which this thread should run.
    int whichGame;
};

/*
 * Holds information about a player's scores for the high score table.
 */
struct PlayerScore {
    // Name of player
    char* name;
    // Total tokens all player's with name have
    int tokens;
    // Total points all player's with name have
    int points;
};

/*
 * Holds information about a reconnect id sent by a reconnecting client.
 */
struct ReconnectId {
    // Name of game reconnecting to
    char* name;
    // Game counter of game reconnecting to
    int gameCounter;
    // Player reconnecting
    int playerId;
};

/*
 * Indicates the status of a client after the authentication handshake.
 */
enum AuthStatus {
    INVALID_AUTH = 0, // Client didn't authenticate
    NEW = 1, // Client is connecting normally to a game
    RECONNECT = 2, // Client is reconnecting
    SCORES = 3 // Client is requesting scores.
};

/*
 * Flag to indicate whether siginthas been received. Initially 0.
 */
volatile sig_atomic_t sigintReceived = 0;

/*
 * Flag to indicate whether sigterm has been received. Initially 0.
 */
volatile sig_atomic_t sigtermReceived = 0;

/*
 * Handles a signal and updates relevant flag if needed.
 */
void handle_signal(int sig) {
    if (sig == SIGINT) {
        sigintReceived = 1;
    } else if (sig == SIGTERM) {
        sigtermReceived = 1;
    }
}

/*
 * Exits program with given exit code, and prints an info message to stderr.
 */
void exit_program(enum ExitCode code) {
    switch (code) {
        case WRONG_ARGS:
            fprintf(stderr, "Usage: rafiki keyfile deckfile statfile "
                    "timeout\n");
            break;
        case BAD_KEYFILE:
            fprintf(stderr, "Bad keyfile\n");
            break;
        case BAD_DECKFILE:
            fprintf(stderr, "Bad deckfile\n");
            break;
        case BAD_STATFILE:
            fprintf(stderr, "Bad statfile\n");
            break;
        case BAD_TIMEOUT:
            fprintf(stderr, "Bad timeout\n");
            break;
        case FAILED_LISTEN:
            fprintf(stderr, "Failed listen\n");
            break;
        case SYSTEM_ERROR:
            fprintf(stderr, "System error\n");
            break;
        default:
            break;
    }

    exit(code);
}

/*
 * Intitialises and returns a pointer to a server struct
 */
Server* setup_server_struct(void) {
    Server* server = malloc(sizeof(Server));

    server->games = NULL;
    server->gameCount = 0;

    server->numLobbies = 0;
    server->lobbies = NULL;

    server->deckSize = 0;
    server->deckEntries = NULL;

    server->timeout = 0;
    server->key = NULL;

    server->statfileSize = 0;
    server->statfileEntries = NULL;

    pthread_mutex_init(&server->joinLobbyLock, NULL);
    pthread_mutex_init(&server->shutdownLock, NULL);

    return server;
}

/*
 * Starts listening to a given port on localhost.
 * Returns the fd of the socket to accept for if successful, or -1 if failed.
 */
int listen_to_port(const char* port) {
    struct addrinfo hints;
    struct addrinfo* res0;
    int sock = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(NULL, port, &hints, &res0) != 0) {
        return -1;
    }

    for (struct addrinfo* res = res0; res != NULL; res = res->ai_next) {
        sock = socket(res->ai_family, res->ai_socktype,
                res->ai_protocol);
        if (sock == -1) {
            continue;
        }

        // Allow immediate reuse of ports after closing
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));

        if (bind(sock, res->ai_addr, res->ai_addrlen) == -1) {
            close(sock);
            sock = -1;
            continue;
        }

        if (listen(sock, SOMAXCONN) == -1) {
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
 * Starts listening to ports using information already loaded from the statfile
 * If a given port in the statfile is 0, the actual port to listen on will
 * be chosen by the kernel. Once chosen and successfully listening, the port
 * information in the relevant StatfileEntry will be replaced with the actual
 * port that is being listened on.
 *
 * Once successful, prints a list of ports listening on to stderr.
 *
 * Returns true if successful in listening/binding to all ports, otherwise
 * returns false if couldn't listen to one or more ports.
 */
bool start_listening(Server* server) {
    server->listeners = malloc(sizeof(struct Listener) * server->statfileSize);

    int numListened = 0;
    for (int i = 0; i < server->statfileSize; i++) {
        char port[PORT_STRING_SIZE];
        sprintf(port, "%d", server->statfileEntries[i].port);

        int sockfd = listen_to_port(port);

        if (sockfd == -1) {
            break;
        } else {
            numListened++;
        }

        struct Listener listener;
        listener.listenFd = sockfd;
        server->listeners[i] = listener;

        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        getsockname(sockfd, (struct sockaddr*) &addr, &len);

        // Get actual port and save it
        server->statfileEntries[i].port = ntohs(addr.sin_port);
    }

    // If not all ports could be listened on, clean up and return false
    if (numListened != server->statfileSize) {
        for (int i = 0; i < numListened; i++) {
            close(server->listeners[i].listenFd);
        }
        free(server->listeners);
        return false;
    }

    // Print list of ports to stderr
    for (int i = 0; i < server->statfileSize; i++) {
        fprintf(stderr, "%s%d", i == 0 ? "" : " ",
                server->statfileEntries[i].port);
    }
    fprintf(stderr, "\n");

    return true;
}

/*
 * Loads statfile at given filename, checks its validity and loads into
 * server, along with the statfile size.
 *
 * Returns true if statfile was valid and loaded successfully, false otherwise
 */
bool load_statfile(Server* server, const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        return false;
    }

    bool valid;
    while (true) {
        valid = false;
        char* line;
        if (read_line(file, &line, 0) <= 0) {
            free(line);
            valid = true; // Finished reading.
            break;
        }

        struct StatfileEntry entry;
        int result = sscanf(line, "%d,%d,%d,%d", &entry.port, &entry.tokens,
                &entry.points, &entry.players);
        free(line);

        if (result != 4 || entry.port < 0 || entry.port > MAX_PORT ||
                entry.tokens < MIN_START_TOKENS || entry.points < MIN_WIN ||
                entry.players < MIN_PLAYERS ||
                entry.players > MAX_PLAYERS) {
            break;
        }

        // Check if port already in statfile (except if ephemeral)
        if (entry.port != 0) {
            for (int i = 0; i < server->statfileSize; i++) {
                if (server->statfileEntries[i].port == entry.port) {
                    break;
                }
            }
        }

        if (server->statfileSize == 0) {
            server->statfileEntries = malloc(sizeof(struct StatfileEntry));
        } else {
            server->statfileEntries = realloc(server->statfileEntries,
                    sizeof(struct StatfileEntry) * (server->statfileSize + 1));
        }
        server->statfileEntries[server->statfileSize++] = entry;
        valid = true;
    }
    valid = valid && !does_file_end_newline(file);
    fclose(file);
    return valid;
}

/*
 * Loads deckfile at given filename, checks its validity, then stores the
 * loaded deck and its size into the server.
 *
 * Returns true if valid deckfile, false otherwise.
 */
bool load_deckfile(Server* server, const char* filename) {
    int numCards;
    struct Card* deck;

    if (parse_deck_file(&numCards, &deck, filename) != VALID) {
        return false;
    }

    server->deckSize = numCards;
    server->deckEntries = deck;

    return true;
}

/*
 * Loads keyfile at given filename and checks its validity, then stores in
 * server.
 *
 * Returns true if valid keyfile, false otherwise.
 */
bool load_keyfile(Server* server, const char* filename) {
    char* key;

    if (!get_keyfile(filename, &key)) {
        return false;
    }

    server->key = key;

    return true;
}

/*
 * Free's all allocated memory in the given server struct
 */
void free_server(Server* server) {
    free(server->deckEntries);
    free(server->statfileEntries);
    free(server->key);

    for (int gameId = 0; gameId < server->gameCount; gameId++) {
        struct Game* game = &server->games[gameId];
        free(game->data);
        free(game->deck);
        free(game->name);
        free(game->players);
    }
    free(server->games);

    for (int lobbyId = 0; lobbyId < server->numLobbies; lobbyId++) {
        struct Lobby* lobby = &server->lobbies[lobbyId];
        free(lobby->name);

        for (int playerId = 0; playerId < lobby->currentClients; playerId++) {
            free(lobby->players[playerId].state.name);

            if (lobby->open) {
                fclose(lobby->players[playerId].fromPlayer);
                fclose(lobby->players[playerId].toPlayer);
            }
        }
        free(lobby->players);
    }
    free(server->lobbies);

    free(server);
}

/*
 * Close all the player input/output streams and sockets in a game
 */
void close_players(struct Game* game) {
    for (int i = 0; i < game->playerCount; i++) {
        shutdown(fileno(game->players[i].fromPlayer), SHUT_RD);
        fclose(game->players[i].fromPlayer);
        fclose(game->players[i].toPlayer);
    }
}

/*
 * Sends a formatted message to all players in the given game
 */
void send_message_game_players(struct Game* game, const char* format, ...) {
    va_list args;

    for (int i = 0; i < game->playerCount; i++) {
        va_start(args, format);
        vfprintf(game->players[i].toPlayer, format, args);
        fflush(game->players[i].toPlayer);
    }

    va_end(args);
}

/*
 * Shuts down the given game running on the server.
 * Updates the 'finished' state of the game, sends "eog" to all players
 * and then closes all the player input/output sockets.
 * Once this is done, the game's thread is shutdown.
 */
void shutdown_game(Server* server, struct Game* game) {
    struct GameData* data = game->data;

    data->finished = true;

    send_message_game_players(game, "eog\n");

    close_players(game);

    // wake threads waiting on a reconnect.
    pthread_cond_signal(&data->reconnectWait);
}

/*
 * Shuts down all currently running games on the server.
 */
void shutdown_games(Server* server) {
    pthread_mutex_lock(&server->shutdownLock);

    for (int i = 0; i < server->gameCount; i++) {

        struct GameData* data = server->games[i].data;
        if (!data->finished) {
            shutdown_game(server, &server->games[i]);
        }
    }

    pthread_mutex_unlock(&server->shutdownLock);

    for (int i = 0; i < server->gameCount; i++) {

        struct GameData* data = server->games[i].data;
        pthread_join(data->tid, NULL);
    }
}

/*
 * Comparison function used by qsort to sort scores prior to printing to a
 * gopher client. Accepts two pointers to PlayerScore structs.
 * Players are sorted in descending order by how many points they have.
 * If two players have the same amount of scores, they will be sorted
 * in ascending order according to how many tokens they have.
 * lobby.
 *
 * Returns an int < 0 if a should come before b, or an int > 0 if b should
 * come before a in sorted array.
 */
int score_sort(const void* a, const void* b) {
    struct PlayerScore p1 = *((struct PlayerScore*) a);
    struct PlayerScore p2 = *((struct PlayerScore*) b);

    int pointCompare = p2.points - p1.points;

    // Same points, so compare tokens
    if (pointCompare == 0) {
        return p1.tokens - p2.tokens;
    }

    return pointCompare;
}

/*
 * Prints scores to the client given.
 * Scores appear in CSV form, with a header line to indicate which column
 * is for their name, total tokens, and total points.
 */
void print_scores(Server* server, FILE* toClient) {
    fprintf(toClient, "Player Name,Total Tokens,Total Points\n");
    fflush(toClient);

    // Count total players in all games
    int totalPlayers = 0;
    for (int gameId = 0; gameId < server->gameCount; gameId++) {
        totalPlayers += server->games[gameId].playerCount;
    }

    int uniquePlayers = 0; // Number of unique player names
    struct PlayerScore scores[totalPlayers]; // Scores for each unique name

    // Go through each game
    for (int i = 0; i < server->gameCount; i++) {
        // Go through each player in this game
        for (int playerId = 0; playerId < server->games[i].playerCount;
                playerId++) {
            int newPlayer = -1;
            struct Player player = server->games[i].players[playerId].state;
            // Go through already found unique player names
            for (int j = 0; j < uniquePlayers; j++) {
                if (strcmp(scores[j].name, player.name) == 0) {
                    // Add this players score's to existing name
                    newPlayer = j;
                }
            }
            // No other player with this name found yet, create new entry
            if (newPlayer == -1) {
                newPlayer = uniquePlayers++;
                scores[newPlayer].tokens = 0;
                scores[newPlayer].points = 0;
            }
            // Store data
            scores[newPlayer].name = player.name;
            scores[newPlayer].tokens +=
                    count_tokens(player.tokens, TOKEN_MAX);
            scores[newPlayer].points += player.score;
        }
    }
    // Sort players by points, then tokens (see score_sort)
    qsort(scores, uniquePlayers, sizeof(struct PlayerScore), score_sort);
    // Print table
    for (int i = 0; i < uniquePlayers; i++) {
        fprintf(toClient, "%s,%d,%d\n", scores[i].name,
                scores[i].tokens, scores[i].points);
    }
    fflush(toClient);
}

/*
 * Waits for a player in the game with given playerId to reconnect. Returns
 * a bool indicating whether the player reconnected in time.
 * If the timeout for the server is 0, the function immediately returns false.
 * Otherwise, if it is signaled that this player reconnected before the
 * timeout, then it will return true. If the player doesn't reconnect
 * before the server's timeout option (in seconds), then returns false.
 */
bool wait_for_reconnect(Server* server, struct Game* game, int playerId) {
    // Instantly end game
    if (server->timeout == 0) {
        return false;
    }

    struct GameData* data = game->data;
    // Game is already finished, can't reconnect.
    if (data->finished) {
        return false;
    }

    data->reconnectingPlayer = playerId;

    // Wait for time given as timeout in seconds
    struct timespec maxWait = {0,0};
    clock_gettime(CLOCK_REALTIME, &maxWait);
    maxWait.tv_sec += server->timeout;

    pthread_mutex_lock(&data->reconnectLock);

    // Wait for reconnect to be signalled
    int waitStatus = pthread_cond_timedwait(&data->reconnectWait,
            &data->reconnectLock, &maxWait);

    pthread_mutex_unlock(&data->reconnectLock);

    // Game ended due to SIGTERM, can't reconnect
    if (data->finished) {
        return false;
    }

    // waitStatus is 0 if player reconnected (i.e didn't timeout cond)
    return waitStatus == 0;
}

/*
 * Sends a dowhat message to a given player in a game and handles their
 * response by updating the game state.
 *
 * Returns PLAYER_CLOSED if the player disconnects, PROTOCOL_ERROR if the
 * player sends an invalid message, and NOTHING_WRONG if all successful.
 */
enum ErrorCode do_what(struct Game* game, int playerId) {
    FILE* toPlayer = game->players[playerId].toPlayer;
    FILE* fromPlayer = game->players[playerId].fromPlayer;

    fputs("dowhat\n", toPlayer);
    fflush(toPlayer);

    char* line;
    if (read_line(fromPlayer, &line, 0) <= 0) {
        free(line);
        return PLAYER_CLOSED;
    }

    enum MessageFromPlayer type = classify_from_player(line);

    enum ErrorCode err;
    switch(type) {
        case PURCHASE:
            err = handle_purchase_message(playerId, game, line);
            break;
        case TAKE:
            err = handle_take_message(playerId, game, line);
            break;
        case WILD:
            handle_wild_message(playerId, game);
            err = NOTHING_WRONG;
            break;
        default: /* Unknown message */
            err = PROTOCOL_ERROR;
    }

    free(line);
    return err;
}

/*
 * Plays the game, starting at the first round, until the game is over,
 * a player disconnects, a player sends two invalid messages in a row,
 * or the game is terminated by main thread (i.e. SIGTERM)
 */
void run_game_loop(Server* server, struct Game* game) {
    enum ErrorCode err = 0;
    bool gameStopped = false;
    int player;
    while (!is_game_over(game) && !gameStopped) {
        // each player takes their turn
        for (player = 0; player < game->playerCount && cards_left(game)
                && !gameStopped; player++) {
            bool hadAttempt = false;
            while (true) {
                err = do_what(game, player);
                if (err == PROTOCOL_ERROR) {
                    if (hadAttempt) {
                        gameStopped = true;
                        break;
                    }
                    hadAttempt = true;
                } else if (err == PLAYER_CLOSED) {
                    if (wait_for_reconnect(server, game, player)) {
                        continue;
                    } else { // Player didn't reconnect in time
                        gameStopped = true;
                        break;
                    }
                } else if (err == NOTHING_WRONG) {
                    break; // Next player...
                }
            }
        }
    }
    // Use a lock to maintain no race condition with server shutting down
    pthread_mutex_lock(&server->shutdownLock);
    // send eog if the game is ending normally (i.e. not due to SIGTERM)
    struct GameData* data = game->data;
    if (!data->finished) {
        if (err == PLAYER_CLOSED) {
            send_message_game_players(game, "disco%c\n", 'A' + (player - 1));
        } else if (err == PROTOCOL_ERROR) {
            send_message_game_players(game, "invalid%c\n", 'A' + (player - 1));
        } else if (err == NOTHING_WRONG) {
            send_message_game_players(game, "eog\n");
        }
        data->finished = true;
        close_players(game);
    }
    pthread_mutex_unlock(&server->shutdownLock);
}

/*
 * Thread for a running game. Lives for the lifecycle of a running game from
 * sending initial information to players until game is finished.
 * Takes a void pointer to a GameThreadArg, which indicates the game id
 * which this thread should handle.
 *
 * Always returns NULL.
 */
void* game_thread(void* arg) {
    struct GameThreadArg* args = arg;
    Server* server = args->server;
    int gameId = args->whichGame;

    free(args);

    struct Game* game = &server->games[gameId];
    struct GameData* data = game->data;

    // Send initial messages about game to all players
    for (int i = 0; i < game->playerCount; i++) {
        fprintf(game->players[i].toPlayer, "rid%s,%d,%d\n", game->name,
                data->counter, i);
        fprintf(game->players[i].toPlayer, "playinfo%c/%d\n", 'A' + i,
                game->playerCount);
        fprintf(game->players[i].toPlayer, "tokens%d\n", game->tokenCount[0]);

        fflush(game->players[i].toPlayer);
    }

    // Draw initial cards from board
    for (int i = 0; i < BOARD_SIZE; i++) {
        draw_card(game);
    }

    run_game_loop(server, game);

    return NULL;
}

/*
 * Adds a game struct to the server and returns its new game id.
 */
int add_game_to_server(Server* server, struct Game game) {
    if (server->games == NULL) {
        server->games = malloc(sizeof(struct Game));
    } else {
        server->games = realloc(server->games,
                sizeof(struct Game) * (server->gameCount + 1));
    }

    int gameCounter = 1;
    for (int gameId = 0; gameId < server->gameCount; gameId++) {
        if (strcmp(game.name, server->games[gameId].name) == 0) {
            // Another game with same name exists, increase GC
            gameCounter++;
        }
    }

    struct GameData* data = malloc(sizeof(struct GameData));
    data->finished = false;
    data->counter = gameCounter;

    pthread_mutex_init(&data->reconnectLock, NULL);
    pthread_cond_init(&data->reconnectWait, NULL);

    game.data = data;

    server->games[server->gameCount] = game;

    return server->gameCount++;
}

/*
 * Comparison function used by qsort to sort players in a lobby prior
 * to the game starting. Accepts two pointers to GamePlayer structs.
 * Players are sorted in alphabetical order, and if two players have an
 * identical name, they will be sorted by the order in which they joined the
 * lobby.
 *
 * Returns an int < 0 if a should come before b, or an int > 0 if b should
 * come before a in sorted array.
 */
int sort_players(const void* a, const void* b) {
    struct Player p1 = ((struct GamePlayer*) a)->state;
    struct Player p2 = ((struct GamePlayer*) b)->state;

    int nameCompare = strcmp(p1.name, p2.name);
    if (nameCompare == 0) {
        return p1.playerId - p2.playerId;
    }

    return nameCompare;
}

/*
 * Starts a game after lobby with given lobbyId is full.
 * Creates a game struct and copies relevant game/player data from the lobby
 * and server into it. Sorts the player's by their name/join order and adds
 * the server to game and indicates it has started. Then spawns a thread
 * to handle the game and communication with players.
 */
void start_game(Server* server, int lobbyId) {
    struct Lobby* lobby = &server->lobbies[lobbyId];
    struct Game game;

    // Copy players from lobby to new game
    game.players = malloc(sizeof(struct GamePlayer) * lobby->currentClients);
    memcpy(game.players, lobby->players,
            sizeof(struct GamePlayer) * lobby->currentClients);
    game.playerCount = lobby->currentClients;

    // Copy deck from server to new game
    game.deck = malloc(sizeof(struct Card) * server->deckSize);
    memcpy(game.deck, server->deckEntries,
            sizeof(struct Card) * server->deckSize);
    game.deckSize = server->deckSize;

    // Set initial tokens in the piles
    for (int i = 0; i < TOKEN_MAX - 1; i++) {
        game.tokenCount[i] = lobby->details.tokens;
    }

    game.winScore = lobby->details.points; // Store required win score
    game.boardSize = 0; // No cards drawn initially.

    // Copy lobby name to game name
    game.name = malloc(sizeof(char) * (strlen(lobby->name) + 1));
    strcpy(game.name, lobby->name);

    // Sort players and then reassign player id's
    qsort(game.players, game.playerCount,
            sizeof(struct GamePlayer), sort_players);
    for (int i = 0; i < game.playerCount; i++) {
        game.players[i].state.playerId = i;
    }

    int gameId = add_game_to_server(server, game);

    struct GameThreadArg* threadArg = malloc(sizeof(struct GameThreadArg));
    threadArg->server = server;
    threadArg->whichGame = gameId;

    struct GameData* data = server->games[gameId].data;
    data->initialTokens = lobby->details.tokens;

    // Create game thread
    pthread_create(&data->tid, NULL, game_thread, (void*) threadArg);
}

/*
 * Adds a given player to lobby with given lobbyId. Sets their player name
 * and input/output streams to the arguments given.
 *
 * If the lobby is then full after this player joins, then the game will be
 * marked as ready to start.
 */
void join_lobby(Server* server, int lobbyId, const char* playerName,
        FILE* fromPlayer, FILE* toPlayer) {

    // Get lobby to join
    struct Lobby* lobby = &server->lobbies[lobbyId];

    struct GamePlayer newPlayer;
    newPlayer.fromPlayer = fromPlayer;
    newPlayer.toPlayer = toPlayer;

    struct Player newPlayerData;
    initialize_player(&newPlayerData, lobby->currentClients);

    // Copy player name
    newPlayerData.name = malloc(sizeof(char) * (strlen(playerName) + 1));
    strcpy(newPlayerData.name, playerName);

    newPlayer.state = newPlayerData;

    lobby->players[lobby->currentClients++] = newPlayer;

    // If lobby is full, create actual game.
    if (lobby->currentClients == lobby->details.players) {
        lobby->open = false; // Close lobby

        start_game(server, lobbyId);
    }
}

/*
 * Creates a new lobby with given name, using the details associated with
 * the he port used to connect by first person in this lobby.
 *
 * Returns id of the new lobby created.
 */
int create_lobby(Server* server, const char* gameName,
        struct StatfileEntry portDetails) {

    // Allocate lobbies array to size needed
    if (server->lobbies == NULL) {
        server->lobbies = malloc(sizeof(struct Lobby));
    } else {
        server->lobbies = realloc(server->lobbies,
                sizeof(struct Lobby) * (server->numLobbies + 1));
    }

    struct Lobby newLobby;
    newLobby.currentClients = 0;
    newLobby.details = portDetails;
    newLobby.open = true;
    newLobby.players = malloc(sizeof(struct GamePlayer) * portDetails.players);

    newLobby.name = malloc(sizeof(char) * (strlen(gameName) + 1));
    strcpy(newLobby.name, gameName);

    server->lobbies[server->numLobbies] = newLobby;
    return server->numLobbies++;
}

/*
 * Finds an open lobby with the given name on the server.
 * If no open lobby with given name exists, a new one is created.
 *
 * The id of the lobby is always returned.
 */
int find_lobby_with_name(Server* server, const char* gameName,
        struct StatfileEntry portDetails) {

    int lobbyFound = -1;

    for (int id = 0; id < server->numLobbies; id++) {
        if (strcmp(server->lobbies[id].name, gameName) == 0
                && server->lobbies[id].open) {
            // Found an open lobby with same name.
            lobbyFound = id;
        }
    }

    if (lobbyFound == -1) {
        // No open lobbies with given name, so create new one.
        return create_lobby(server, gameName, portDetails);
    } else {
        return lobbyFound;
    }
}

/*
 * Handles a player wishing to join a game/create a new game.
 * Reads their wanted game name, and their player name.
 * Tries to find an existing lobby with the name, and joins the lobby,
 * otherwise creates a new lobby with the details (e.g. num players,
 * initial tokens, etc) associated with the port they connected on.
 *
 * Returns a bool indicating whether this client should be kept connected.
 * It will return false if the player should be disconnected.
 */
bool join_game(Server* server, FILE* fromClient, FILE* toClient,
        struct StatfileEntry portDetails) {

    char* gameName = NULL;
    char* playerName = NULL;

    if (read_line(fromClient, &gameName, 0) <= 0
            || read_line(fromClient, &playerName, 0) <= 0
            || !is_valid_game_name(gameName)
            || !is_valid_game_name(playerName)) {
        free(gameName);
        free(playerName);

        return false; // Disconnect client
    }

    // start lobby lock
    pthread_mutex_lock(&server->joinLobbyLock);

    int lobbyId = find_lobby_with_name(server, gameName, portDetails);
    join_lobby(server, lobbyId, playerName, fromClient, toClient);

    // unlock
    pthread_mutex_unlock(&server->joinLobbyLock);

    free(gameName);
    free(playerName);

    return true; // Keep client connected
}

/*
 * Sends catch up messages to a player after reconnecting.
 * Sends a "newcard" message for all cards currently on the board.
 * Sends a "player" message to indicate each player in the game's current
 * state/points/tokens/discounts.
 */
void send_catchup_messages(Server* server, struct Game* game, FILE* toClient) {
    // Send newcard messages
    for (int cardId = 0; cardId < game->boardSize; cardId++) {
        char* message = print_new_card_message(game->board[cardId]);
        fprintf(toClient, message);

        free(message);
    }

    // Send Player messages
    for (int playerId = 0; playerId < game->playerCount; playerId++) {
        struct Player player = game->players[playerId].state;

        int discounts[TOKEN_MAX - 1];
        int tokens[TOKEN_MAX];

        memcpy(discounts, player.discounts, sizeof(int) * (TOKEN_MAX - 1));
        memcpy(tokens, player.tokens, sizeof(int) * (TOKEN_MAX));

        fprintf(toClient, "player%c:%d:d=%d,%d,%d,%d:t=%d,%d,%d,%d,%d\n",
                'A' + playerId, player.score, discounts[TOKEN_PURPLE],
                discounts[TOKEN_BROWN], discounts[TOKEN_YELLOW],
                discounts[TOKEN_RED], tokens[TOKEN_PURPLE],
                tokens[TOKEN_BROWN], tokens[TOKEN_YELLOW],
                tokens[TOKEN_RED], tokens[TOKEN_WILD]);
    }

    fflush(toClient);
}

/*
 * Finds an running game on the server with given game name and game counter.
 *
 * Returns game id if game found, otherwise -1.
 */
int find_open_game(Server* server, char* gameName, int gameCounter) {
    for (int gameId = 0; gameId < server->gameCount; gameId++) {
        struct GameData* data = server->games[gameId].data;

        if (strcmp(server->games[gameId].name, gameName) == 0
                && data->counter == gameCounter
                && !data->finished) {
            return gameId;
        }
    }

    // No game found
    return -1;
}

/*
 * Reads an "rid" message from a client, parses it and then stores it
 * in a ReconnectId struct at location given in dest.
 *
 * Returns true if message was received/read and was valid, otherwise
 * will return false.
 */
bool get_and_parse_rid(FILE* fromClient, struct ReconnectId* dest) {
    char* ridMessage;
    if (read_line(fromClient, &ridMessage, 0) <= 0) {
        free(ridMessage);
        return false;
    }
    // Check message is of form "rid[...]"
    if (strncmp(ridMessage, "rid", strlen("rid")) != 0
            || strlen(ridMessage) == strlen("rid")) {
        free(ridMessage);
        return false;
    }
    // Extract actual rid part
    char* rid = ridMessage + strlen("rid");

    // Get game name from rid
    char* gameName = malloc(strlen(rid) + 1);
    int i = 0;
    for (i = 0; i < strlen(rid); i++) {
        if (rid[i] == ',') { // First comma reached, end of game name
            break;
        }
        gameName[i] = rid[i];
    }
    // Null terminate game name string
    gameName[i] = '\0';

    int gc;
    int playerId;
    // Scan remaining string (after game name and comma) for gc, playerid
    if (sscanf(rid + strlen(gameName) + 1, "%d,%d", &gc, &playerId) != 2
            || playerId >= MAX_PLAYERS || playerId < 0) {
        free(ridMessage);
        free(gameName);
        return false;
    }

    free(ridMessage);

    struct ReconnectId reconnect;
    reconnect.name = gameName;
    reconnect.gameCounter = gc;
    reconnect.playerId = playerId;

    *dest = reconnect;
    return true;
}

/*
 * Handles client attempting to reconnect to a game. Reads and parses their
 * rid message, then tries to match their rid to running game. Waits until
 * this player is indicated as disconnected.
 *
 * Returns a bool indicating whether this client's connection should be kept
 * open (i.e. false if client should be disconnected).
 */
bool reconnect_game(Server* server, FILE* fromClient, FILE* toClient) {
    struct ReconnectId rid;
    if (!get_and_parse_rid(fromClient, &rid)) {
        fprintf(toClient, "no\n");
        fflush(toClient);
        return false; // disconnect.
    }

    int gameId = find_open_game(server, rid.name, rid.gameCounter);
    free(rid.name);

    // Check game/player id is valid
    if (gameId == -1 || rid.playerId >= server->games[gameId].playerCount) {
        fprintf(toClient, "no\n");
        fflush(toClient);
        return false; // disconnect.
    }

    struct GameData* data = server->games[gameId].data;
    struct Game* game = &server->games[gameId];

    // Wait until game detects this player is disconnected.
    while (data->reconnectingPlayer != rid.playerId) {
        if (data->finished) {
            fprintf(toClient, "no\n");
            fflush(toClient);
            return false;
        }
    }

    fprintf(toClient, "yes\n");
    fprintf(toClient, "playinfo%c/%d\n",
            'A' + rid.playerId, game->playerCount);
    fprintf(toClient, "tokens%d\n", data->initialTokens);
    send_catchup_messages(server, game, toClient);

    fflush(toClient);

    // Update player streams
    game->players[rid.playerId].fromPlayer = fromClient;
    game->players[rid.playerId].toPlayer = toClient;

    // Tell game thread that this player has reconnected.
    pthread_cond_signal(&data->reconnectWait);

    return true; // Keep connection open
}

/*
 * Authenticates a connection with a client. Takes file streams to communicate
 * with client.
 *
 * Returns NEW if client authenticates and wants to connect to a lobby.
 * Returns RECONNECT if client authenaticates and wants to reconnect to a game.
 * Returns SCORES if client wants the scores table.
 * Returns INVALID_AUTH if couldn't authenticate with client.
 */
enum AuthStatus authenticate_connection(Server* server, FILE* fromClient,
        FILE* toClient) {
    char* line;
    if (read_line(fromClient, &line, 0) <= 0) {
        free(line);
        return INVALID_AUTH;
    }

    enum AuthStatus res = INVALID_AUTH;
    if (strncmp(line, "play", strlen("play")) == 0) {
        char* key = line + strlen("play");
        if (strcmp(key, server->key) == 0) {
            res = NEW;
        }
    } else if (strncmp(line, "reconnect", strlen("reconnect")) == 0) {
        char* key = line + strlen("reconnect");
        if (strcmp(key, server->key) == 0) {
            res = RECONNECT;
        }
    } else if (strcmp(line, "scores") == 0) {
        res = SCORES;
    }

    free(line);
    return res;
}

/*
 * Thread to handle an individual connection to a client, after accepting.
 * Reads their initial message and tries to authenaticates with client.
 * If auth'ed, it will either send them scores, try to get them to join a game
 * or reconnect them to a game depending on what they ask for.
 * If needed closes the connection, otherwise leaves the connection open (i.e.
 * if they are rejoining a  game) and exits the thread, allowing the game
 * threads to do further communication if needed.
 *
 * Thread always self-exits and returns NULL
 */
void* handle_connection(void* arg) {
    struct ConnectionThreadArg* args = arg;
    Server* server = args->server;
    struct StatfileEntry portDetails = args->details;
    int connectedFd = args->connectedFd;

    free(args);

    FILE* fromClient = fdopen(connectedFd, "r");
    FILE* toClient = fdopen(connectedFd, "w");

    enum AuthStatus auth = authenticate_connection(server,
            fromClient, toClient);

    fprintf(toClient, "%s\n", auth == INVALID_AUTH ? "no" : "yes");
    fflush(toClient);

    bool keepConnOpen = true;
    switch (auth) {
        case NEW:
            keepConnOpen = join_game(server, fromClient,
                    toClient, portDetails);
            break;
        case RECONNECT:
            keepConnOpen = reconnect_game(server, fromClient, toClient);
            break;
        case SCORES:
            print_scores(server, toClient);
            /* Fall through as we want to close the client now */
        default:
            keepConnOpen = false;
            break;
    }

    // Shutdown connection if needed
    if (!keepConnOpen) {
        shutdown(connectedFd, SHUT_RD);
        fclose(fromClient);
        fclose(toClient);
    }

    /* This thread can exit, as sockets to client still remain open
       if they need to be, and can be used by a game thread */
    pthread_exit(NULL);

    return NULL; // Not needed
}

/*
 * Thread for an accept call on a socket listening to a port. Lives until
 * the socket listening on the port is shutdown. Every time a new connection
 * is accepted, a new thread is spawned to handle it, with the port details
 * passed along. The thread will then continue to accept connections until
 * the socket is closed or the thread is killed.
 *
 * Takes a void pointer to an AcceptThreadArg, which indicates the details
 * associated with the port listening on, and the id of the Listener.
 *
 * Always returns NULL.
 */
void* accept_thread(void* arg) {
    struct AcceptThreadArg* args = arg;
    Server* server = args->server;
    struct StatfileEntry portDetails = args->details;
    int whichListener = args->whichListener;

    free(args);

    // Keep accepting connections
    while (true) {
        struct sockaddr_storage ss;
        socklen_t sslen = sizeof(ss);

        if (server->listeners == NULL) {
            return NULL;
        }

        int conn = accept(server->listeners[whichListener].listenFd,
                (struct sockaddr*) &ss, &sslen);

        if (conn == -1) {
            switch (errno) {
                case EAGAIN:
                    continue;
                default:
                    /* Listener has been closed, exit thread */
                    return NULL;
            }
        }

        struct ConnectionThreadArg* connectArgs
                = malloc(sizeof(struct ConnectionThreadArg));
        connectArgs->server = server;
        connectArgs->details = portDetails;
        connectArgs->connectedFd = conn;

        pthread_t conntid;
        pthread_create(&conntid, NULL,
                handle_connection, (void*) connectArgs);
        pthread_detach(conntid); // this thread doesn't care about client
    }

    return NULL;
}

/*
 * Stops listeners on the server by shutting down the sockets,
 * then waiting for the accepting threads to subsequently shutdown due to
 * socket shutdowns.
 */
void stop_accepting(Server* server) {
    for (int i = 0; i < server->statfileSize; i++) {
        // shutdown listener socket
        shutdown(server->listeners[i].listenFd, SHUT_RD);
        // thread should then exit, free its resources.
        pthread_join(server->listeners[i].acceptTid, NULL);
    }

    free(server->listeners);

    server->listeners = NULL;
}

/*
 * Spawns a thread for each port in statfile to start accepting connections
 * after binding and listening.
 */
void start_accepting(Server* server) {
    for (int i = 0; i < server->statfileSize; i++) {
        struct AcceptThreadArg* args = malloc(sizeof(struct AcceptThreadArg));
        args->server = server;
        args->details = server->statfileEntries[i];
        args->whichListener = i;

        pthread_create(&server->listeners[i].acceptTid, NULL,
                accept_thread, (void*) args);
    }
}

/*
 * Runs the main server loop. Loads the statfile, starts listening to ports
 * specified, and starts accepting connections. When SIGTERM is received
 * (as updated by the signal handler), the server will shutdown the listening
 * ports, and reload the statfile to start listening again. It will continue
 * to do this until SIGTERM is received.
 *
 * If loading the statfile ever fails, BAD_STATFILE is returned.
 * If the timeout string given is not a non-negative integer, then BAD_TIMEOUT
 * is returned.
 * If the server couldn't listen to all the ports specified, then
 * failed_LISTEN is returned.
 * If the server is to exit normally, due to SIGTERM, then NORMAL_EXIT returns
 */
enum ExitCode run_server(Server* server, const char* statfile,
        const char* timeout) {

    while (!sigtermReceived) {
        if (!load_statfile(server, statfile)) {
            return BAD_STATFILE;
        }

        if (!str_to_int(timeout, &server->timeout) || server->timeout < 0) {
            return BAD_TIMEOUT;
        }

        if (!start_listening(server)) {
            return FAILED_LISTEN;
        }

        start_accepting(server);

        while (!sigintReceived && !sigtermReceived) {
            // Wait for either SIGTERM or SIGINT...
        }

        stop_accepting(server);

        free(server->statfileEntries);
        server->statfileEntries = NULL;
        server->statfileSize = 0;

        sigintReceived = 0; // go back to normal
    }

    return NORMAL_EXIT;
}

int main(int argc, char** argv) {
    if (argc != 5) {
        exit_program(WRONG_ARGS);
    }

    Server* server = setup_server_struct();

    if (!load_keyfile(server, argv[1])) {
        free_server(server);
        exit_program(BAD_KEYFILE);
    }

    if (!load_deckfile(server, argv[2])) {
        free_server(server);
        exit_program(BAD_DECKFILE);
    }

    // Setup signal handlers for SIGINT/SIGTERM.
    struct sigaction sa = {.sa_handler = handle_signal};
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    // Ignore SIGPIPE as disconnects detected by read failure, not write fails
    signal(SIGPIPE, SIG_IGN);

    enum ExitCode err = run_server(server, argv[3], argv[4]);

    shutdown_games(server);
    free_server(server);

    exit_program(err);
}

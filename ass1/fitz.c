#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>

/* Exit status codes */
#define ERROR_INCORRECT_ARGS 1
#define ERROR_TILEFILE_UNREADABLE 2
#define ERROR_TILEFILE_INVALID 3
#define ERROR_INVALID_PLAYER_TYPE 4
#define ERROR_INVALID_DIMENSIONS 5
#define ERROR_SAVEFILE_UNREADABLE 6
#define ERROR_SAVEFILE_INVALID 7
#define ERROR_EOF 10

#define TILE_SIZE 5
#define MAX_BOARD_SIZE 999

#define EMPTY_TILE_CELL ','
#define OCCUPIED_TILE_CELL '!'
#define EMPTY_GRID_CELL '.'

/* Player types */
#define PLAYER_TYPE_HUMAN 0
#define PLAYER_TYPE_AUTO_ONE 1
#define PLAYER_TYPE_AUTO_TWO 2

#define PLAYER_ONE 0
#define PLAYER_TWO 1

#define INITIAL_BUFFER 100
#define MAX_VALID_LINE_LENGTH 70

/* Macro to give player symbol from currentPlayer int */
#define PLAYER_SYMBOL(x) ((x) == 0 ? '*' : '#')

/*
 * Stores a previous move made by a player
 *  - row: Row of the centre of tile this move placed 
 *  - column: Column of the centre of tile this move placed 
 */
typedef struct {
    int row;
    int column;
} PreviousMove;

/* 
 * Stores details of a fitz game
 *  - height: Height of the game grid
 *  - width: Width of the game grid
 *  - grid: The game grid
 *  - currentTile: Tile being placed by current player (starting from 0)
 *  - currentPlayer: Player whose turn it is currently (0 for P1, 1 for P2)
 *  - playerTypes: Array specifying the player types for each player
 *  - lastPlay: Stores the last move for each player
 *  - numMoves: Total number of successful moves so far this game
 *  - savefile: Path to savefile if loaded from, otherwise NULL.
 */
typedef struct {
    int height;
    int width;
    char** grid;
    int currentTile;
    int currentPlayer;
    int playerTypes[2];
    PreviousMove lastPlay[2];
    int numMoves;
    char* savefile;
} Game;

/* Main game functions */
void run_game_loop(Game* game, int numTiles, 
        char tiles[numTiles][TILE_SIZE][TILE_SIZE]);
void initialise_game(Game* game, int numTiles);
void parse_cmd_arguments(int argc, char** argv, Game* game);

/* Tilefile functions */
int check_tilefile(char* filename);
void load_tiles(char* filename, int numTiles, 
        char tiles[numTiles][TILE_SIZE][TILE_SIZE]);
       
/* Savefile functions */
void load_savefile_grid(Game* game, char* filename);
bool write_savefile(Game* game, char* filename);
    
/* Tile/rotation/placement logic */
void rotate_tile(char tile[TILE_SIZE][TILE_SIZE], 
        char destTile[TILE_SIZE][TILE_SIZE], int degrees);
bool is_tile_placeable(Game* game, char tile[TILE_SIZE][TILE_SIZE], 
        int row, int column);
bool is_game_over(Game* game, char tile[TILE_SIZE][TILE_SIZE]);
void place_tile(Game* game, char tile[TILE_SIZE][TILE_SIZE], 
        int row, int column);
        
/* Printing functions */
void print_tile(char tile[TILE_SIZE][TILE_SIZE]);
void print_tilefile(int numTiles, char tiles[numTiles][TILE_SIZE][TILE_SIZE]);
void print_grid(Game* game);

/* Next move processing */
bool prompt_user(Game* game, char tile[TILE_SIZE][TILE_SIZE]);
void auto_type_one_move(Game* game, char tile[TILE_SIZE][TILE_SIZE]);
void auto_type_two_move(Game* game, char tile[TILE_SIZE][TILE_SIZE]);

/* Game exiting */
void exit_game(int exitCode);

/* Helper functions */
bool read_line(char* buffer, int maxLength, FILE* fileStream, int lineNum);
bool is_valid_input_line(char* input, int numInputs);
int get_player_type(char* input);
int str_to_int(char* str, bool* error);

int main(int argc, char** argv) {
    if ((argc != 2) && (argc != 5) && (argc != 6)) {
        exit_game(ERROR_INCORRECT_ARGS);
    }

    Game game;
    
    char* tilefileName = argv[1];
    int numTiles = check_tilefile(tilefileName);
    
    char tiles[numTiles][TILE_SIZE][TILE_SIZE];
    load_tiles(tilefileName, numTiles, tiles);
    
    // If just given tilefile, print and exit.
    if (argc == 2) {
        print_tilefile(numTiles, tiles);
        return 0;
    } else {
        parse_cmd_arguments(argc, argv, &game);
    }
    
    initialise_game(&game, numTiles);
    print_grid(&game);
    run_game_loop(&game, numTiles, tiles);
            
    return 0;
}

/*
 * Runs the main loop of the fitz game.
 * 
 * @param game Game struct
 * @param numTiles Number of tiles loaded from tilefile
 * @param tiles Tiles loaded from tilefile
 * @exit ERROR_EOF if end of input occurs unexpectedly
 */
void run_game_loop(Game* game, int numTiles, 
        char tiles[numTiles][TILE_SIZE][TILE_SIZE]) {
            
    while (true) {
        // Check if OTHER player has won
        if (is_game_over(game, tiles[game->currentTile])) {
            printf("Player %c wins\n", 
                    PLAYER_SYMBOL(!game->currentPlayer));
            return;
        }
        
        int currentPlayerType = game->playerTypes[game->currentPlayer];
        
        if (currentPlayerType == PLAYER_TYPE_HUMAN) {
            print_tile(tiles[game->currentTile]);
            
            while (true) {
                // If user inputs a valid move, stop prompting them and go on
                if (prompt_user(game, tiles[game->currentTile])) {
                    break;
                }     
            }
        } else if (currentPlayerType == PLAYER_TYPE_AUTO_ONE) {
            auto_type_one_move(game, tiles[game->currentTile]);
        } else if (currentPlayerType == PLAYER_TYPE_AUTO_TWO) {
            auto_type_two_move(game, tiles[game->currentTile]);
        }

        print_grid(game);
        
        game->currentPlayer = !game->currentPlayer;
        // Go to next tile, or wrap around if no more to cycle through
        if (game->currentTile == numTiles - 1) {
            game->currentTile = 0;
        } else {
            game->currentTile = game->currentTile + 1;
        }
    }
}

/*
 * Initialises game struct. If there is a savefile to load, will load data
 * from savefile and initialise game to that state if valid to do so.
 *
 * @param game Game struct to initialise
 * @param numTiles Number of tiles loaded from tilefile
 * @exit ERROR_SAVEFILE_UNREADABLE if savefile can't be open/read
 * @exit ERROR_SAVEFILE_INVALID if savefile is invalid
 */
void initialise_game(Game* game, int numTiles) {
    game->currentPlayer = PLAYER_ONE;
    game->currentTile = 0;
    game->numMoves = 0; 

    if (game->savefile != NULL) {
        FILE* file = fopen(game->savefile, "r");
        if (file == NULL) {
            exit_game(ERROR_SAVEFILE_UNREADABLE);
        }
        char inputBuffer[INITIAL_BUFFER];
        read_line(inputBuffer, INITIAL_BUFFER, file, 0);
        
        // Check line is four single space separated integers.
        if (!is_valid_input_line(inputBuffer, 4)) {
            exit_game(ERROR_SAVEFILE_INVALID); 
        }
        
        int nextTile, currentPlayer, height, width;
        sscanf(inputBuffer, "%d %d %d %d", 
                &nextTile, &currentPlayer, &height, &width);
        
        // Check savefile data is correct
        if ((currentPlayer != 0 && currentPlayer != 1) ||
                (height < 1) || (height > MAX_BOARD_SIZE) || 
                (width < 1) || (width > MAX_BOARD_SIZE) ||
                (nextTile >= numTiles) || (nextTile < 0)) {
            exit_game(ERROR_SAVEFILE_INVALID);
        }
                
        game->currentPlayer = currentPlayer;
        game->currentTile = nextTile;
        game->height = height;
        game->width = width;
    }
    // Allocate and initialise memory for grid
    game->grid = malloc(sizeof(char*) * game->height);
    for (int i = 0; i < game->height; i++) {
        game->grid[i] = malloc(sizeof(char*) * game->width);
        for (int j = 0; j < game->width; j++) {
            game->grid[i][j] = EMPTY_GRID_CELL;
        }
    }
    // Load grid from savefile if needed, after memory allocated
    if (game->savefile != NULL) {
        load_savefile_grid(game, game->savefile);
    }
}

/*
 * Parses and processes command line arguments given to program
 *
 * @param argc Argument count passed from main()
 * @param argv Arguments passed from main()
 * @param game Game struct
 * @exit ERROR_INVALID_PLAYER_TYPE If a player type given is invalid
 * @exit ERROR_INVALID_DIMENSIONS If a dimension given is invalid
 */
void parse_cmd_arguments(int argc, char** argv, Game* game) {
    int playerOneType = get_player_type(argv[2]);
    int playerTwoType = get_player_type(argv[3]);

    if (playerOneType == -1 || playerTwoType == -1) {
        exit_game(ERROR_INVALID_PLAYER_TYPE);
    }
    
    game->playerTypes[0] = playerOneType;
    game->playerTypes[1] = playerTwoType;

    if (argc == 5) {
        game->savefile = argv[4];
    } else if (argc == 6) {
        bool notValid = false;
        int height = str_to_int(argv[4], &notValid);
        int width = str_to_int(argv[5], &notValid);
        
        // Check if dimensions invalid, or not exactly integers
        if (notValid || (height < 1) || (height > MAX_BOARD_SIZE) 
                || (width < 1) || (width > MAX_BOARD_SIZE)) {
            exit_game(ERROR_INVALID_DIMENSIONS);
        }
        
        game->height = height;
        game->width = width;
    }
}

/*
 * Checks the tilefile is valid and returns how many tiles in file
 * A valid tilefile must contain exactly TILE_SIZE rows of TILE_SIZE length, 
 * with each row ending in \n. Tiles must be separated by an extra \n.
 *
 * @param filename Path to tilefile to check
 * @return Number of tiles in the tilefile
 * @exit ERROR_TILEFILE_UNREADABLE if tilefile can't be read/opened
 * @exit ERROR_TILEFILE_INVALID if tilefile is invalid
 */
int check_tilefile(char* filename) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        exit_game(ERROR_TILEFILE_UNREADABLE);
    }

    int numTiles = 0;
    char curr;
    
    while (true) {
        for (int row = 0; row < TILE_SIZE; row++) {
            // Check there exactly TILE_SIZE valid characters in row
            for (int column = 0; column < TILE_SIZE; column++) {
                curr = fgetc(file);
                if (curr != EMPTY_TILE_CELL && curr != OCCUPIED_TILE_CELL) {
                    exit_game(ERROR_TILEFILE_INVALID);
                }
            }
            // Next character must be newline
            curr = fgetc(file);
            if (curr != '\n') {
                exit_game(ERROR_TILEFILE_INVALID);
            }
        }
        numTiles++;
        
        curr = fgetc(file);
        if (curr == EOF) { // If file is finished, exit loop and return
            break;
        } else if (curr != '\n') { // If not finished, \n must seperate tiles
            exit_game(ERROR_TILEFILE_INVALID);
        }
    }
    
    return numTiles;
}

/*
 * Loads tilefile at specified filename into the given tiles array.
 * This function assumes the tilefile is already checked to be valid
 * and it is already known how many tiles are in the tilefile.
 *
 * @param filename Path of tilefile to load
 * @param numTiles Number of tiles in the tilefile
 * @param tiles Destination to store tiles loaded from file
 * @exit ERROR_TILEFILE_UNREADABLE if can't access tilefile at filename
 */
void load_tiles(char* filename, int numTiles, 
        char tiles[numTiles][TILE_SIZE][TILE_SIZE]) {
            
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        exit_game(ERROR_TILEFILE_UNREADABLE);
    }
    
    char curr;
    for (int tile = 0; tile < numTiles; tile++) {
        for (int row = 0; row < TILE_SIZE; row++) {
            for (int column = 0; column < TILE_SIZE; column++) {
                curr = fgetc(file);
                tiles[tile][row][column] = curr;
            }
            // flush newline character between rows
            curr = fgetc(file); 
        }
        // flush blank newline between each tile
        curr = fgetc(file); 
    }
}

/*
 * Loads the grid stored in savefile. Assumes first line containing
 * game and grid information has already been processed and correctly stored.
 *
 * @param game Game struct
 * @param filename Path of savefile to load grid from
 * @exit ERROR_SAVEFILE_UNREADABLE if savefile can't be opened/read
 * @exit ERROR_SAVEFILE_INVALID if savefile is invalid
 */
void load_savefile_grid(Game* game, char* filename) {
    FILE* file = fopen(filename, "r");
    
    if (file == NULL) {
        exit_game(ERROR_SAVEFILE_UNREADABLE);
    }
    
    char curr;

    // Since first line of savefile is assumed to have already been read
    while (curr = fgetc(file), curr != '\n') {
        // Flush through the first line 
    }
    
    for (int row = 0; row < game->height; row++) {
        for (int column = 0; column < game->width; column++) {
            curr = fgetc(file);
            if (curr != '.' && curr != '#' && curr != '*') {
                exit_game(ERROR_SAVEFILE_INVALID);
            }
            game->grid[row][column] = curr;
        }
        // Each row must be ended with \n character
        curr = fgetc(file);
        if (curr != '\n') {
            exit_game(ERROR_SAVEFILE_INVALID);
        }
    }
    // Make sure file doesn't have extra lines it shouldn't have
    curr = fgetc(file);
    if (curr != EOF) {
        exit_game(ERROR_SAVEFILE_INVALID);
    }
}

/*
 * Writes savefile to given file.
 *
 * @param filename Path to desired savefile location 
 * @return true if write successful, false otherwise
 */
bool write_savefile(Game* game, char* filename) {
    FILE* file = fopen(filename, "w");
    
    if (file == NULL) {
        return false;
    }
    
    // First line contains next tile, player and dimensions
    fprintf(file, "%d %d %d %d\n", game->currentTile, 
            game->currentPlayer, game->height, game->width);
    
    // Write grid
    for (int row = 0; row < game->height; row++) {
        fprintf(file, "%s\n", game->grid[row]);
    }
    
    fclose(file);
    return true;
}

/*
 * Rotates given tile specified number of degrees and stores it in destTile
 * If given degrees is 0, tile be copied to destTile
 *
 * @param tile Original tile to be rotated
 * @param destTile Destination where rotated tile will be stored
 * @param degrees Number of degrees to rotate (divisible by 90).
 */
void rotate_tile(char tile[TILE_SIZE][TILE_SIZE], 
        char destTile[TILE_SIZE][TILE_SIZE], int degrees) {
            
    int numRotations = degrees / 90;
    
    char tempTile[TILE_SIZE][TILE_SIZE];
    
    // Copy tile to destTile. Used for if degrees == 0
    for (int i = 0; i < TILE_SIZE; i++) {
        for (int j = 0; j < TILE_SIZE; j++) {
            destTile[i][j] = tile[i][j];
        }            
    }

    for (int iteration = 0; iteration < numRotations; iteration++) {
        // Copy tile to a temp location array.
        // This allows for rotation multiple times.
        for (int i = 0; i < TILE_SIZE; i++) {
            for (int j = 0; j < TILE_SIZE; j++) {
                tempTile[i][j] = destTile[i][j];
            }            
        }
        
        // Rotate tempTile 90 degrees and store in destTIle
        for (int i = 0; i < TILE_SIZE; i++) {
            for (int j = 0; j < TILE_SIZE; j++) {
                destTile[i][j] = tempTile[(TILE_SIZE - 1) - j][i];
            }
        }
    }
}

/*
 * Checks whether the given tile is validly placeable on the board
 * at the given row and column.
 *
 * @param game Game struct
 * @param tile Tile to place
 * @param row Row where middle of tile will be placed (starting at 0)
 * @param column Column where middle of tile will be placed (starting at 0)
 * @return true if valid placement, false otherwise
 */
bool is_tile_placeable(Game* game, char tile[TILE_SIZE][TILE_SIZE], 
        int row, int column) {
        
    // If the whole tile is off the board, consider invalid placement
    if (column >= game->width + 2 || row >= game->height + 2 
            || column < -2 || row < -2) {
        return false;
    }
    
    // Iterate through each cell in given tile
    for (int i = 0; i < TILE_SIZE; i++) {
        for (int j = 0; j < TILE_SIZE; j++) {
            // Translate cell in tile to location it will be placed on grid.
            int xCord = (column - 2) + j;
            int yCord = (row - 2) + i;
            
            char tileCell = tile[i][j];
            // See if a non-empty tile cell will be placed off of the grid
            if (xCord < 0 || yCord < 0 || xCord >= game->width 
                    || yCord >= game->height) {
                if (tileCell != EMPTY_TILE_CELL) {
                    return false;
                } else {
                    continue; // Cell is empty, doesn't matter
                }
            }
            
            // If a non-empty tile cell will be placed on non-empty grid cell
            char gridCell = game->grid[yCord][xCord];
            if (gridCell != EMPTY_GRID_CELL && tileCell != EMPTY_TILE_CELL) {
                return false;
            }
        }
    }
    
    return true;
}

/*
 * Determines whether the game is over for the current player.
 * Checks every possible move to be made on board with given tile.
 *
 * @param game Game struct
 * @param tile Next tile that needs to be placed
 * @return true if game is over, false if there are moves that can be made
 */
bool is_game_over(Game* game, char tile[TILE_SIZE][TILE_SIZE]) {
    char rotations[4][TILE_SIZE][TILE_SIZE];
    
    rotate_tile(tile, rotations[0], 0);
    rotate_tile(tile, rotations[1], 90);
    rotate_tile(tile, rotations[2], 180);
    rotate_tile(tile, rotations[3], 270);
    
    // Iterate through every placeable row/column on grid
    for (int row = -2; row <= game->height + 2; row++) {
        for (int column = -2; column <= game->width + 2; column++) {
            // Test all four rotations at current location
            for (int curRotation = 0; curRotation < 4; curRotation++) {
                if (is_tile_placeable(game, rotations[curRotation], 
                        row, column)) {
                    return false; // Tile can be placed, game not over
                }
            }
        }
    }

    return true;
}

/*
 * Places given tile on game grid and updates the last play information.
 * Assumes given tile and coordinates is a valid placement.
 *
 * @param game Game struct
 * @param tile Tile to be placed on board
 * @param row Row where middle of tile will be placed (starting at 0)
 * @param column Column where middle of tile will be placed (starting at 0)
 */
void place_tile(Game* game, char tile[TILE_SIZE][TILE_SIZE], 
        int row, int column) {
            
    for (int i = 0; i < TILE_SIZE; i++) {
        for (int j = 0; j < TILE_SIZE; j++) {
            // Translate cell in tile to location it will be placed on grid.
            int xCord = (column - 2) + j;
            int yCord = (row - 2) + i;
            
            char tileCell = tile[i][j];
            
            // Tile cell isn't being placed on board
            if (xCord < 0 || yCord < 0 || xCord >= game->width 
                    || yCord >= game->height) {
                continue;
            }
            
            // Only copy over non-empty tile cells
            if (tileCell != EMPTY_TILE_CELL) {
                game->grid[yCord][xCord] = PLAYER_SYMBOL(game->currentPlayer); 
            }
            
        }
    } 
    
    game->lastPlay[game->currentPlayer].row = row;
    game->lastPlay[game->currentPlayer].column = column;
    game->numMoves = game->numMoves + 1;
}

/*
 * Prints given tile to stdout.
 *
 * @param tile Tile to be printed
 */
void print_tile(char tile[TILE_SIZE][TILE_SIZE]) {
    for (int row = 0; row < TILE_SIZE; row++) {
        for (int column = 0; column < TILE_SIZE; column++) {
            printf("%c", tile[row][column]);
        }
        printf("\n");    
    }
}

/*
 * Prints tiles and their associated 90, 180, and 270 degree rotations
 * side by side in order with a space between. In between each tile and its
 * associated rotations, a blank line is printed.
 *
 * @param numTiles number of tiles in tiles array
 * @param tiles array of tiles to print
 */
void print_tilefile(int numTiles, 
        char tiles[numTiles][TILE_SIZE][TILE_SIZE]) {
            
    for (int tile = 0; tile < numTiles; tile++) {
        // Store all four possible rotations for this particular tile
        char rotations[4][TILE_SIZE][TILE_SIZE];
        
        rotate_tile(tiles[tile], rotations[0], 0);
        rotate_tile(tiles[tile], rotations[1], 90);
        rotate_tile(tiles[tile], rotations[2], 180);
        rotate_tile(tiles[tile], rotations[3], 270);
        
        for (int row = 0; row < TILE_SIZE; row++) {
            // Need to print same row of each rotation on same line
            for (int curRotation = 0; curRotation < 4; curRotation++) {
                for (int column = 0; column < TILE_SIZE; column++) {
                    printf("%c", rotations[curRotation][row][column]);
                }
                // Separate each rotation with space between (but not after)
                if (curRotation != 3) {
                    printf(" ");
                }
            }
            printf("\n");        
        }
        
        // Print a blank line between tiles, but not afterwards.
        if (tile != (numTiles - 1)) {
            printf("\n");
        }
    }
}

/*
 * Prints current game grid to stdout.
 *
 * @param game Game struct
 */
void print_grid(Game* game) {
    for (int row = 0; row < game->height; row++) {
        printf("%s\n", game->grid[row]);
    }
}

/*
 * Prompts human player for input and moves/saves file is input is valid.
 * 
 * @param game Game struct
 * @paramm tile Current tile to be placed on board
 * @return false if input (or move) was invalid, user must be prompted again
 *         true if input was valid. Next player's move can be processed.
 * @exit ERROR_EOF if unexpected end of file while reading input
 */
bool prompt_user(Game* game, char tile[TILE_SIZE][TILE_SIZE]) {
    printf("Player %c] ", PLAYER_SYMBOL(game->currentPlayer));
    char inputCommand[INITIAL_BUFFER];
    
    // Read input from stdin into inputCommand and exit if unexpected EOF
    if (!read_line(inputCommand, INITIAL_BUFFER, stdin, 0)) {
        exit_game(ERROR_EOF);
    }
    
    // Check line is of valid length
    if (strlen(inputCommand) > MAX_VALID_LINE_LENGTH) {
        return false;
    }
    
    char savefileName[INITIAL_BUFFER];
    int row, column, rotation;

    int moveResult = sscanf(inputCommand, "%d %d %d", 
            &row, &column, &rotation);
    int saveResult = sscanf(inputCommand, "save%s", savefileName);
    
    if (is_valid_input_line(inputCommand, 3) && moveResult == 3) {
        if (rotation != 0 && rotation != 90 
                && rotation != 180 && rotation != 270) {
            return false;
        }
        
        char tileRotation[TILE_SIZE][TILE_SIZE];
        rotate_tile(tile, tileRotation, rotation);
    
        if (is_tile_placeable(game, tileRotation, row, column)) {
            place_tile(game, tileRotation, row, column);
            // User doesn't need to be prompted again.
            return true;
        }
    } else if (saveResult == 1) {
        if (!write_savefile(game, savefileName)) {
            fprintf(stderr, "Unable to save game\n");
        }
    }
    
    return false;
}

/*
 * Calculates, performs, and prints move for a type one automatic fitz player.
 * Assumes game is not already over and current tile is placeable somewhere.
 *
 * @param game Game struct
 * @param tile Current tile to place on grid
 */
void auto_type_one_move(Game* game, char tile[TILE_SIZE][TILE_SIZE]) {
    int row, column, rowStart, columnStart;
    
    // Use last move by either player if available, otherwise start at -2, -2
    if (game->numMoves == 0) {
        rowStart = -2;
        columnStart = -2;
    } else {
        // Last play will be made by opposite player
        rowStart = game->lastPlay[!game->currentPlayer].row;
        columnStart = game->lastPlay[!game->currentPlayer].column;
    }
    
    row = rowStart;
    column = columnStart;

    for (int theta = 0; theta <= 270; theta += 90) {
        char tileRotation[TILE_SIZE][TILE_SIZE];
        rotate_tile(tile, tileRotation, theta);
        
        // Loop until row, column = rowStart, columnStart
        do { 
            if (is_tile_placeable(game, tileRotation, row, column)) {
                place_tile(game, tileRotation, row, column);
                printf("Player %c => %d %d rotated %d\n", 
                        PLAYER_SYMBOL(game->currentPlayer), 
                        row, column, theta);
                return;
            }
            column += 1;
            
            // If column is too far off grid, wrap around to next row.
            if (column >= game->width + 2) {
                column = -2;
                row += 1;
            }
            
            // If row is too far off grid, wrap around to start.
            if (row >= game->height + 2) {
                row = -2;
            }
        
        } while (!(row == rowStart && column == columnStart));
    }
}

/*
 * Calculates, performs, and prints move for a type one automatic fitz player.
 * Assumes game is not already over and current tile is placeable somewhere.
 *
 * @param game Game struct
 * @param tile Current tile to place on grid
 */
void auto_type_two_move(Game* game, char tile[TILE_SIZE][TILE_SIZE]) {
    int row, column, rowStart, columnStart;
    int currentPlayer = game->currentPlayer;
    // If less than two moves have occured in game, then this player 
    // has no previous last move to refer to
    if (game->numMoves < 2) {
        rowStart = (currentPlayer == PLAYER_ONE) ? -2 : game->height + 1;
        columnStart = (currentPlayer == PLAYER_ONE) ? -2 : game->width + 1;
    } else {
        rowStart = game->lastPlay[game->currentPlayer].row;
        columnStart = game->lastPlay[game->currentPlayer].column;
    }

    row = rowStart;
    column = columnStart;  
    
    // Loop until row, column = rowStart, columnStart
    do {
        for (int theta = 0; theta <= 270; theta += 90) {
            char tileRotation[TILE_SIZE][TILE_SIZE];
            rotate_tile(tile, tileRotation, theta);
            if (is_tile_placeable(game, tileRotation, row, column)) {
                place_tile(game, tileRotation, row, column);
                printf("Player %c => %d %d rotated %d\n", 
                        PLAYER_SYMBOL(game->currentPlayer), 
                        row, column, theta);
                return;     
            }            
        }
        // Move to next position on grid
        if (currentPlayer == PLAYER_ONE) {
            // If first player, move left->right, top->bottom
            column += 1;
            if (column > game->width + 1) { // Wrap around if needed
                row += 1;
                column = -2;
            }
        } else if (currentPlayer == PLAYER_TWO) {
            // If second player, move right->left, bottom->top
            column -= 1;
            if (column < -2) { // Wrap around if needed
                row -= 1;
                column = game->width + 1;
            }
        }
    } while (!(row == rowStart && column == columnStart));
}

/*
 * Exits program with specified error code.
 * Also prints an informative message to stderr.
 *
 * @param exitCode specified error code.
 * @exits with exitCode.
 */
void exit_game(int exitCode) {
    switch(exitCode) {
        case ERROR_INCORRECT_ARGS:
            fprintf(stderr, "Usage: fitz tilefile [p1type p2type " 
                    "[height width | filename]]\n");
            break;
        case ERROR_TILEFILE_UNREADABLE:
            fprintf(stderr, "Can't access tile file\n");
            break;
        case ERROR_TILEFILE_INVALID:
            fprintf(stderr, "Invalid tile file contents\n");
            break;
        case ERROR_INVALID_PLAYER_TYPE:
            fprintf(stderr, "Invalid player type\n");
            break;
        case ERROR_INVALID_DIMENSIONS:
            fprintf(stderr, "Invalid dimensions\n");
            break;
        case ERROR_SAVEFILE_UNREADABLE:
            fprintf(stderr, "Can't access save file\n");
            break;
        case ERROR_SAVEFILE_INVALID:
            fprintf(stderr, "Invalid save file contents\n");
            break;
        case ERROR_EOF:
            fprintf(stderr, "End of input\n");
            break;
        default:
            break;
    }

    exit(exitCode);
}

/*
 * Reads given line number from given file stream until \n or EOF 
 * or bufferSize reached and then stores to buffer
 * 
 * @param buffer Destination to store input read
 * @param bufferSize Size of intial buffer. buffer must be at least this size.
 * @param fileStream Input to read from
 * @param lineNum Line number to read
 * @return false if reached unexpected EOF, true if otherwise
 */
bool read_line(char* buffer, int bufferSize, FILE* fileStream, int lineNum) {
    char curr;
    int pos = 0;
    
    // Flush fgetc til reached lineNum line
    for (int curLine = 0; curLine < lineNum; curLine++) {
        while ((curr = fgetc(fileStream)) != '\n') {
            // Do nothing. Flush through
        }
    }
    
    curr = fgetc(fileStream);
    // Continue reading until newline, EOF, or bufferSize reached
    while (curr != '\n' && curr != EOF && pos < (bufferSize - 1)) {
        buffer[pos] = curr;
        pos++;
        
        curr = fgetc(fileStream);
    }
    
    // Null terminate string
    buffer[pos] = '\0';
    
    // Flush through remaining charcters in file until a \n or EOF
    while (curr != EOF && curr != '\n') {
        curr = fgetc(fileStream);
    }
    
    // If curr == EOF, then file isn't \n terminated
    return curr != EOF;
}


/*
 * Checks a given input is a space delimited line of a specified amount of
 * integers with no trailing or leading whitespace. Integers can only have
 * one space between them.
 *
 * For example ("1 1 0", 3), ("-1 0 2 4", 4) is valid,
 * but ("1 1 0", 2), (" 1 1 0 ", 3), ("2.0 0 0", 3), ("2    2 0", 3) are not.
 *
 * @param input Input to check
 * @param numInputs Number of separated inputs if line is to be valid
 * @return true if valid, false otherwise
 */
bool is_valid_input_line(char* input, int numInputs) {
    int inputLength = strlen(input);
    
    // If leading or trailing whitespace, invalid
    if (isspace(input[0]) || isspace(input[inputLength - 1])) {
        return false;
    }
    
    int inputCount = 1;
        
    for (int i = 0; i < inputLength; i++) {
        if (isspace(input[i])) {
            // two spaces in a row means invalid.
            if (isspace(input[i + 1])) {
                return false;
            }
            inputCount++;
        } else if (input[i] == '-') {
            // If a negative sign occurs and isn't immediately preceded
            // by a space, or at the start of string, invalid
            if (i > 0 && !isspace(input[i - 1])) {
                return false;
            }
            // If a negative sign occurs and isn't followed by digit, invalid
            if (!isdigit(input[i + 1])) {
                return false;
            }
        } else if (!isdigit(input[i])) {
            // Not a space, minus sign, or digit, therefore invalid
            return false;
        }
    }
    
    return inputCount == numInputs;
}

/*
 * Converts given input to a player type.
 *
 * @param input String to be converted to player type
 * @return PLAYER_TYPE_HUMAN, PLAYER_TYPE_AUTO_ONE, or PLAYER_TYPE_AUTO_TWO
 *         if successful
 * @return -1 if invalid input
 */
int get_player_type(char* input) {
    if (strcmp(input, "h") == 0) {
        return PLAYER_TYPE_HUMAN;
    } else if (strcmp(input, "1") == 0) {
        return PLAYER_TYPE_AUTO_ONE;
    } else if (strcmp(input, "2") == 0) {
        return PLAYER_TYPE_AUTO_TWO;
    } else {
        return -1;
    }
}

/*
 * Converts a string to integer. 
 * Sets error to true if string contains a non integer part.
 * 
 * @param str String to convert
 * @param error Set to true if string contains a non integer part.
 * @return Converted integer
 */
int str_to_int(char* str, bool* error) {
    char* strpart;
    long value;

    value = strtol(str, &strpart, 10);
    
    if (*strpart != '\0') {
        *error = true;
    }

    return (int) value;
}

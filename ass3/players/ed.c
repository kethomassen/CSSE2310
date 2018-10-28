#include "../player.h"

/*
 * Gets the name of this player implementation, "ed".
 *
 * @return "ed"
 */
const char* get_player_name(void) {
    return "ed";
}


/*
 * Identifies the face up card worth the highest number of points which
 * any opponent can afford right now. If there is more than one such card,
 * it prefer cards affordable by players after this one in the round
 * (ie. if you are player 2 of 4, then prefer cards for Player 3,
 * then Player 0, then Player 1.).
 * If there are still multiple such cards, the oldest card is chosen.
 *
 * @param game The game struct
 * @return -1 if no card found, else the cardId of the identified card
 */
int identify_card(Game* game) {
    int highestValue = -1;
    int cardToBuy = -1;

    // Search in opposite direction to players after them in the round.
    // I.e. search from player before this player, and wrap around until
    // till player after them
    // This means the latest chosen card will be prioritised based on
    // going after them in the round
    int playerId = game->myId - 1;
    while (playerId != game->myId) {
        // Need to wrap around
        if (playerId < 0) {
            // Can't wrap around if player is last. Already searched all.
            if (game->myId == game->numPlayers - 1) {
                break;
            }
            playerId = game->numPlayers - 1;
        }

        // Search from newest to oldest
        for (int cardId = game->cardsFacedUp - 1; cardId >= 0; cardId--) {
            if (can_player_afford_card(game, playerId, cardId)) {
                int value = game->cards[cardId].value;

                // If this card is worth more than chosen, choose it instead
                if (value >= highestValue) {
                    highestValue = value;
                    cardToBuy = cardId;
                }
            }
        }

        // go to next player
        playerId--;
    }

    return cardToBuy;
}


/*
 * Checks if tokens can be taken. If they can, they will be prioritsed to be
 * taken in order to afford the identified highest value card, if it exists.
 * They will be taken in the following order
 * (skip steps with a * if there was no card identified):
 * - Yellow* (if more Yellow tokens are needed to afford the card)
 * - Red* (if more Red tokens are needed to afford the card)
 * - Brown* (if more Brown tokens are needed to afford the card)
 * - Purple* (if more Purple tokens are needed to afford the card)
 * - Yellow, Red, Brown, Purple
 *
 * @param game The game struct
 * @param cardToBuy Highest value face up card affordable that was found
 * @return true if tokens were taken, false otherwise
 */
bool check_tokens(Game* game, int cardToBuy) {
    // Check tokens can actually be taken from board
    if (!can_tokens_be_taken(game)) {
        return false;
    }

    int numTaken = 0;
    int taking[NUM_COLOURS] = {0, 0, 0, 0};
    int order[NUM_COLOURS] = {YELLOW, RED, BROWN, PURPLE};

    // If a card was identified to buy
    if (cardToBuy > -1) {
        Card card = game->cards[cardToBuy];

        for (int i = 0; i < NUM_COLOURS; i++) {
            // get how many tokens of this colour needed
            int tokensNeeded = card.price[order[i]] -
                    (game->players[game->myId].discounts[order[i]] +
                    game->players[game->myId].tokens[order[i]]);

            // If tokens needed, and can be taken, take one
            if (game->tokens[order[i]] > 0 && tokensNeeded > 0 &&
                    numTaken < TOKENS_PER_TAKE) {
                taking[order[i]] = 1;
                numTaken++;
            }
        }
    }

    // Otherwise search normally
    for (int i = 0; i < NUM_COLOURS; i++) {
        // Make sure token of that colour hasn't been taken already, or over 3
        if (game->tokens[order[i]] > 0 && taking[order[i]] == 0 &&
                numTaken < TOKENS_PER_TAKE) {

            taking[order[i]] = 1;
            numTaken++;
        }
    }

    tell_hub_take_tokens(taking);

    return true;
}

/*
 * Performs a move for ed.
 * First, identifies face up card worth the highest number of points which
 * any opponent can afford right now (see identify_card for specifics).
 * If this card can be afforded, then buy it.
 * Otherwise, if tokens can be taken, they will be prioritised in order of
 * Yellow, Red, Brown, Purple in order to afford token (see check_tokens).
 * Otherwise, ed will take a wild token.
 *
 * @param game The game struct
 */
void choose_move(Game* game) {
    // Identify card worth highest number of points opponents can afford now
    int cardToBuy = identify_card(game);

    // If this card exists, and you can afford it, buy it.
    if (cardToBuy > -1
            && can_player_afford_card(game, game->myId, cardToBuy)) {

        int tokensToUse[TOKEN_SLOTS];
        choose_tokens_to_buy_card(game, game->myId, cardToBuy, tokensToUse);
        tell_hub_purchase_card(cardToBuy, tokensToUse);

        return;
    }

    // Otherwise, check if tokens be taken and if needed
    if (check_tokens(game, cardToBuy)) {
        return;
    }

    // Otherwise, just take a wild.
    tell_hub_take_wild();
}

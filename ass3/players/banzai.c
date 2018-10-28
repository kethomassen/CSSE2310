#include "../player.h"
#include <stdio.h>

/*
 * Gets the name of this player implementation, "banzai".
 *
 * @return "banzai"
 */
const char* get_player_name(void) {
    return "banzai";
}

/*
 * Checks if banzai can take any tokens from the board.
 * If banzai has 3 or more tokens already, they won't take anymore.
 * Prioritises tokens in the following order: Yellow, Brown, Purple, Red
 *
 * @param game The game struct
 * @return true if tokens were taken, false otherwise.
 */
bool check_tokens(Game* game) {
    // If tokens can't be taken, or already have 3 or more, don't take.
    if (!can_tokens_be_taken(game)
            || get_player_token_count(game, game->myId) >= 3) {

        return false;
    }

    int numTaken = 0;
    int taking[NUM_COLOURS] = {0, 0, 0, 0};
    int order[NUM_COLOURS] = {YELLOW, BROWN, PURPLE, RED};

    // Take 3 tokens in yellow, brown, purple, red order
    for (int i = 0; i < NUM_COLOURS; i++) {
        if (game->tokens[order[i]] > 0 && numTaken < 3) {
            taking[order[i]] = 1;
            numTaken++;
        }
    }

    tell_hub_take_tokens(taking);

    return true;
}

/*
 * Checks if banzai can afford to buy any cards. Banzai will only buy
 * non-zero value cards. The most expensive card will be prioritised.
 * If there are multiple, the card which would use the most wild tokens by
 * the player is chosen. If there are still multiple left, the oldest card
 * will be chosen.
 *
 * @param game The game struct
 * @return true if card was bought, false if no cards could be bought.
 */
bool check_cards(Game* game) {
    int mostWilds = -1;
    int highestPrice = -1;
    int cardToBuy = -1;

    // Search from newest to oldest (backwards), as banzai
    // preferences older cards
    for (int cardId = game->cardsFacedUp - 1; cardId >= 0; cardId--) {
        // Check player can afford card, and is worth more than 0 points
        if (can_player_afford_card(game, game->myId, cardId) &&
                game->cards[cardId].value > 0) {
            int cardPrice = get_card_price_for_player(game, game->myId,
                    cardId);
            int wildsNeeded = get_wilds_needed_for_card(game, game->myId,
                    cardId);

            // If this card is same price as chosen card
            if (highestPrice == cardPrice) {
                // If same wilds needed, choose this one as older.
                if (wildsNeeded == mostWilds) {
                    cardToBuy = cardId;
                } else if (wildsNeeded > mostWilds) {
                    // Otherwise if more wilds needed, choose this one
                    // and update mostWilds
                    mostWilds = wildsNeeded;
                    cardToBuy = cardId;
                }
            } else if (cardPrice > highestPrice) {
                // If this card is more expensive than currently chosen,
                // choose it instead
                highestPrice = cardPrice;
                mostWilds = wildsNeeded;
                cardToBuy = cardId;
            }
        }
    }

    // If there is a card to buy, buy it
    if (cardToBuy > -1) {
        int tokensToUse[TOKEN_SLOTS];
        choose_tokens_to_buy_card(game, game->myId, cardToBuy, tokensToUse);
        tell_hub_purchase_card(cardToBuy, tokensToUse);
        return true;
    } else {
        return false;
    }
}


/*
 * Performs a move for shenzi.
 * If less then 3 tokens, they will be taken in order of Yellow, Brown,
 * Purple, Red.
 * If one or more cards are available and worth non-zero points,
 * purchase one. The most expensive card is prioritised, then the card which
 * would use the most wild tokens, and then the oldest card.
 * Otherwise, banzai will take a wild token.
 *
 * @param game The game struct
 */
void choose_move(Game* game) {
    if (check_tokens(game)) {
        return;
    }

    if (check_cards(game)) {
        return;
    }

    tell_hub_take_wild();
}

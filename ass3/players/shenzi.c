#include "../player.h"
#include <stdio.h>

/*
 * Gets the name of this player implementation, "shenzi".
 *
 * @return "shenzi"
 */
const char* get_player_name(void) {
    return "shenzi";
}

/*
 * Checks if shenzi can afford to buy any cards. The card worth the most
 * points is chosen. If multiple cards are worth the most points,
 * the one worth the smallest number of tokens is prioritised. If there are
 * still multiple, then the newest card is chosen.
 *
 * @param game The game struct
 * @return true if card was bought, false if no cards could be bought.
 */
bool check_cards(Game* game) {
    int highestValue = -1; // Value of current card chosen
    int lowestPrice = -1; // Price of current card chosen
    int cardToBuy = -1; // Current card chosen

    // Search cards from oldest to newest, as newer cards are preferenced
    for (int cardId = 0; cardId < game->cardsFacedUp; cardId++) {
        if (can_player_afford_card(game, game->myId, cardId)) {
            int cardValue = game->cards[cardId].value;
            int cardPrice = get_card_price_for_player(game, game->myId,
                    cardId);

            // If this card is worth as much as current chosen card
            if (highestValue == cardValue) {
                // If prices are equal, preference this card as
                // searching from oldest to newest
                if (cardPrice == lowestPrice) {
                    cardToBuy = cardId;
                } else if (cardPrice < lowestPrice) {
                    // If this card is cheaper, yet worth same, select it
                    lowestPrice = cardPrice;
                    cardToBuy = cardId;
                }
            } else if (highestValue < cardValue) {
                // If this card is worth more than current chosen card,
                // overwrite with this card.
                highestValue = cardValue;
                lowestPrice = cardPrice;
                cardToBuy = cardId;
            }
        }
    }

    // Check if a card was chosen, and then buy it
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
 * Checks if shenzi can take any tokens from the board.
 * Player must take 3 tokens, each of different colour.
 * Prioritises tokens in the following order: Purple, Brown, Yellow, Red.
 *
 * @param game The game struct
 * @return true if tokens were took, false otherwise.
 */
bool check_tokens(Game* game) {
    // Check there are 3 token piles on board to take from
    if (!can_tokens_be_taken(game)) {
        return false;
    }

    int numTaken = 0;
    int taking[NUM_COLOURS];

    // Since tokens are ordered in Shenzi's preferred order, just loop
    for (int i = 0; i < NUM_COLOURS; i++) {
        if (game->tokens[i] > 0 && numTaken < TOKENS_PER_TAKE) {
            taking[i] = 1;
            numTaken++;
        } else {
            taking[i] = 0;
        }
    }

    tell_hub_take_tokens(taking);

    return true;
}

/*
 * Performs a move for shenzi.
 * If a card can be bought, most valuable, then the cheapest, and newest card
 * that can be afforded will be taken.
 * Otherwise, if tokens can be taken, they will be prioritised in order of
 * Purple, Brown, Yellow, Red.
 * Otherwise, shenxi will take a wild token.
 *
 * @param game The game struct
 */
void choose_move(Game* game) {
    // Check if any cards can be purchased
    if (check_cards(game)) {
        return;
    }

    // Check if any tokens can be token
    if (check_tokens(game)) {
        return;
    }

    // Otherwise, take a wild
    tell_hub_take_wild();
}

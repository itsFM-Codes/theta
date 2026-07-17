#include "search_internal.h"

#include <string.h>

#include "src/chess/movegen.h"

static void clear_move(Move *move) {
    move->from = NO_SQUARE;
    move->to = NO_SQUARE;
    move->promotion = PIECE_NONE;
    move->flags = MOVE_FLAG_NONE;
}

void initialize_search_context(SearchContext *context, int time_limit_ms) {
    int ply;

    context->start_time = clock();
    context->time_limit_ms = time_limit_ms;
    context->stopped = 0;
    memset(context->history, 0, sizeof(context->history));

    for (ply = 0; ply < MAX_KILLER_PLY; ++ply) {
        clear_move(&context->killer_moves[ply][0]);
        clear_move(&context->killer_moves[ply][1]);
    }
}

int search_has_stopped(SearchContext *context) {
    clock_t elapsed;

    if (context == 0 || context->time_limit_ms <= 0) {
        return 0;
    }

    if (context->stopped) {
        return 1;
    }

    elapsed = clock() - context->start_time;

    if (elapsed * 1000 / CLOCKS_PER_SEC >= context->time_limit_ms) {
        context->stopped = 1;
        return 1;
    }

    return 0;
}

int position_is_in_check(const Position *position) {
    int king_square = find_king(position, position->side_to_move);

    return is_valid_square(king_square) &&
           is_square_attacked(
               position,
               king_square,
               opposite_color(position->side_to_move)
           );
}

void clear_variation(PrincipalVariation *variation) {
    if (variation != 0) {
        variation->count = 0;
    }
}

void update_variation(
    PrincipalVariation *variation,
    Move move,
    const PrincipalVariation *child_variation
) {
    int index;

    if (variation == 0) {
        return;
    }

    variation->moves[0] = move;
    variation->count = 1;

    if (child_variation == 0) {
        return;
    }

    for (index = 0;
         index < child_variation->count &&
         variation->count < MAX_PRINCIPAL_VARIATION;
         ++index) {
        variation->moves[variation->count] = child_variation->moves[index];
        variation->count++;
    }
}

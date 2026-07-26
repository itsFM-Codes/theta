#include "piece_square_tables.h"
#include "eval_params.h"

static const int *table_for_piece(Piece piece, int endgame) {
    const EvalParams *params = current_eval_params();

    switch (piece_type(piece)) {
        case PIECE_TYPE_PAWN:
            return params->pawn_table;
        case PIECE_TYPE_KNIGHT:
            return params->knight_table;
        case PIECE_TYPE_BISHOP:
            return params->bishop_table;
        case PIECE_TYPE_ROOK:
            return params->rook_table;
        case PIECE_TYPE_QUEEN:
            return params->queen_table;
        case PIECE_TYPE_KING:
            return endgame
                ? params->king_endgame_table
                : params->king_middlegame_table;
        case PIECE_TYPE_NONE:
            return 0;
    }

    return 0;
}

int piece_square_value(Piece piece, int square, int endgame_weight) {
    const int *middle_table;
    const int *endgame_table;
    int table_square;
    int middle_value;
    int endgame_value;

    if (square < 0 || square >= SQUARE_COUNT) {
        return 0;
    }

    if (endgame_weight < 0) {
        endgame_weight = 0;
    } else if (endgame_weight > 256) {
        endgame_weight = 256;
    }

    middle_table = table_for_piece(piece, 0);
    endgame_table = table_for_piece(piece, 1);

    if (middle_table == 0 || endgame_table == 0) {
        return 0;
    }

    table_square = square;

    if (piece_color(piece) == COLOR_WHITE) {
        table_square ^= 56;
    }

    middle_value = middle_table[table_square];
    endgame_value = endgame_table[table_square];

    return (middle_value * (256 - endgame_weight) +
            endgame_value * endgame_weight) / 256;
}

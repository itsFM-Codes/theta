#include <assert.h>

#include "src/chess/move.h"

static Move create_move(int from, int to, Piece promotion, int flags) {
    Move move;

    move.from = from;
    move.to = to;
    move.promotion = promotion;
    move.flags = flags;
    return move;
}

static void assert_positions_equal(
    const Position *first,
    const Position *second
) {
    int square;

    for (square = 0; square < SQUARE_COUNT; ++square) {
        assert(first->board[square] == second->board[square]);
    }

    assert(first->side_to_move == second->side_to_move);
    assert(first->castling_rights == second->castling_rights);
    assert(first->en_passant_square == second->en_passant_square);
    assert(first->halfmove_clock == second->halfmove_clock);
    assert(first->fullmove_number == second->fullmove_number);
}

static void test_double_pawn_move(void) {
    Position position;
    Position original;
    UndoState undo;
    Move move;

    set_starting_position(&position);
    original = position;
    move = create_move(
        make_square(6, 4),
        make_square(4, 4),
        PIECE_NONE,
        MOVE_FLAG_DOUBLE_PAWN
    );

    assert(make_move(&position, move, &undo));
    assert(position_piece_at_coordinates(&position, 4, 4) == PIECE_WHITE_PAWN);
    assert(position_piece_at_coordinates(&position, 6, 4) == PIECE_NONE);
    assert(position.en_passant_square == make_square(5, 4));
    assert(position.side_to_move == COLOR_BLACK);

    undo_move(&position, move, &undo);
    assert_positions_equal(&position, &original);
}

static void test_capture(void) {
    Position position;
    Position original;
    UndoState undo;
    Move move;

    clear_position(&position);
    position_set_piece_at_coordinates(&position, 4, 4, PIECE_WHITE_PAWN);
    position_set_piece_at_coordinates(&position, 3, 3, PIECE_BLACK_KNIGHT);
    original = position;
    move = create_move(
        make_square(4, 4),
        make_square(3, 3),
        PIECE_NONE,
        MOVE_FLAG_CAPTURE
    );

    assert(make_move(&position, move, &undo));
    assert(position_piece_at_coordinates(&position, 3, 3) == PIECE_WHITE_PAWN);
    assert(position.halfmove_clock == 0);

    undo_move(&position, move, &undo);
    assert_positions_equal(&position, &original);
}

static void test_en_passant(void) {
    Position position;
    Position original;
    UndoState undo;
    Move move;

    clear_position(&position);
    position_set_piece_at_coordinates(&position, 3, 4, PIECE_WHITE_PAWN);
    position_set_piece_at_coordinates(&position, 3, 3, PIECE_BLACK_PAWN);
    position.en_passant_square = make_square(2, 3);
    original = position;
    move = create_move(
        make_square(3, 4),
        make_square(2, 3),
        PIECE_NONE,
        MOVE_FLAG_CAPTURE | MOVE_FLAG_EN_PASSANT
    );

    assert(make_move(&position, move, &undo));
    assert(position_piece_at_coordinates(&position, 2, 3) == PIECE_WHITE_PAWN);
    assert(position_piece_at_coordinates(&position, 3, 3) == PIECE_NONE);

    undo_move(&position, move, &undo);
    assert_positions_equal(&position, &original);
}

static void test_promotion(void) {
    Position position;
    Position original;
    UndoState undo;
    Move move;

    clear_position(&position);
    position_set_piece_at_coordinates(&position, 1, 0, PIECE_WHITE_PAWN);
    original = position;
    move = create_move(
        make_square(1, 0),
        make_square(0, 0),
        PIECE_WHITE_QUEEN,
        MOVE_FLAG_PROMOTION
    );

    assert(make_move(&position, move, &undo));
    assert(position_piece_at_coordinates(&position, 0, 0) == PIECE_WHITE_QUEEN);

    undo_move(&position, move, &undo);
    assert_positions_equal(&position, &original);
}

static void test_castling(void) {
    Position position;
    Position original;
    UndoState undo;
    Move move;

    clear_position(&position);
    position.castling_rights = CASTLING_WHITE_KING_SIDE;
    position_set_piece_at_coordinates(&position, 7, 4, PIECE_WHITE_KING);
    position_set_piece_at_coordinates(&position, 7, 7, PIECE_WHITE_ROOK);
    original = position;
    move = create_move(
        make_square(7, 4),
        make_square(7, 6),
        PIECE_NONE,
        MOVE_FLAG_CASTLE_KINGSIDE
    );

    assert(make_move(&position, move, &undo));
    assert(position_piece_at_coordinates(&position, 7, 6) == PIECE_WHITE_KING);
    assert(position_piece_at_coordinates(&position, 7, 5) == PIECE_WHITE_ROOK);
    assert(position.castling_rights == CASTLING_NONE);

    undo_move(&position, move, &undo);
    assert_positions_equal(&position, &original);
}

int main(void) {
    test_double_pawn_move();
    test_capture();
    test_en_passant();
    test_promotion();
    test_castling();
    return 0;
}

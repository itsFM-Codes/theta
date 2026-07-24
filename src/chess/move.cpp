#include "move.h"

#include "zobrist.h"

static void remove_castling_right(Position *position, int right) {
    position->castling_rights &= ~right;
}

static void update_rook_castling_right(Position *position, Piece rook, int square) {
    if (rook == PIECE_WHITE_ROOK) {
        if (square == make_square(7, 0)) {
            remove_castling_right(position, CASTLING_WHITE_QUEEN_SIDE);
        } else if (square == make_square(7, 7)) {
            remove_castling_right(position, CASTLING_WHITE_KING_SIDE);
        }
    } else if (rook == PIECE_BLACK_ROOK) {
        if (square == make_square(0, 0)) {
            remove_castling_right(position, CASTLING_BLACK_QUEEN_SIDE);
        } else if (square == make_square(0, 7)) {
            remove_castling_right(position, CASTLING_BLACK_KING_SIDE);
        }
    }
}

static void update_castling_rights(Position *position, Move move, const UndoState *undo) {
    if (undo->moved_piece == PIECE_WHITE_KING) {
        remove_castling_right(position, CASTLING_WHITE_KING_SIDE);
        remove_castling_right(position, CASTLING_WHITE_QUEEN_SIDE);
    } else if (undo->moved_piece == PIECE_BLACK_KING) {
        remove_castling_right(position, CASTLING_BLACK_KING_SIDE);
        remove_castling_right(position, CASTLING_BLACK_QUEEN_SIDE);
    }

    update_rook_castling_right(
        position,
        undo->moved_piece,
        move.from
    );
    update_rook_castling_right(
        position,
        undo->captured_piece,
        undo->captured_square
    );
}

static void move_castling_rook(Position *position, Move move) {
    int row = square_row(move.from);
    int rook_from;
    int rook_to;
    Piece rook;

    if (move.flags & MOVE_FLAG_CASTLE_KINGSIDE) {
        rook_from = make_square(row, 7);
        rook_to = make_square(row, 5);
    } else {
        rook_from = make_square(row, 0);
        rook_to = make_square(row, 3);
    }

    rook = position_piece_at(position, rook_from);
    position_set_piece(position, rook_from, PIECE_NONE);
    position_set_piece(position, rook_to, rook);
}

static void undo_castling_rook(Position *position, Move move) {
    int row = square_row(move.from);
    int rook_from;
    int rook_to;
    Piece rook;

    if (move.flags & MOVE_FLAG_CASTLE_KINGSIDE) {
        rook_from = make_square(row, 5);
        rook_to = make_square(row, 7);
    } else {
        rook_from = make_square(row, 3);
        rook_to = make_square(row, 0);
    }

    rook = position_piece_at(position, rook_from);
    position_set_piece(position, rook_from, PIECE_NONE);
    position_set_piece(position, rook_to, rook);
}

static int is_valid_promotion_piece(Piece piece, Color color) {
    PieceType type = piece_type(piece);

    if (piece_color(piece) != color) {
        return 0;
    }

    return type == PIECE_TYPE_QUEEN ||
           type == PIECE_TYPE_ROOK ||
           type == PIECE_TYPE_BISHOP ||
           type == PIECE_TYPE_KNIGHT;
}

int make_move(Position *position, Move move, UndoState *undo) {
    Piece moved_piece;
    Piece target_piece;
    Piece placed_piece;
    uint64_t key;
    int old_castling_rights;
    int old_en_passant_index;

    // Check if args are valid
    if (position == 0 || undo == 0) {
        return 0;
    }

    if (!is_valid_square(move.from) ||
        !is_valid_square(move.to) ||
        move.from == move.to) {
        return 0;
    }

    // Check if source contains a piece from the side to move
    moved_piece = position_piece_at(position, move.from);
    if (moved_piece == PIECE_NONE ||
        piece_color(moved_piece) != position->side_to_move) {
        return 0;
    }

    target_piece = position_piece_at(position, move.to);
    if (target_piece != PIECE_NONE &&
        piece_color(target_piece) == position->side_to_move) {
        return 0;
    }

    // Check promotion data before changing the position
    if (move.flags & MOVE_FLAG_PROMOTION) {
        if (piece_type(moved_piece) != PIECE_TYPE_PAWN ||
            !is_valid_promotion_piece(
                move.promotion,
                position->side_to_move
            )) {
            return 0;
        }
    }

    key = position_key(position);
    old_castling_rights = position->castling_rights;
    old_en_passant_index = zobrist_en_passant_index(position);

    // Save current state for undo
    undo->moved_piece = moved_piece;
    undo->captured_piece = target_piece;
    undo->captured_square = move.to;
    undo->side_to_move = position->side_to_move;
    undo->castling_rights = position->castling_rights;
    undo->en_passant_square = position->en_passant_square;
    undo->halfmove_clock = position->halfmove_clock;
    undo->fullmove_number = position->fullmove_number;
    undo->zobrist_key = position->zobrist_key;
    undo->zobrist_key_valid = position->zobrist_key_valid;
    undo->zobrist_side_to_move = position->zobrist_side_to_move;
    undo->zobrist_castling_rights = position->zobrist_castling_rights;
    undo->zobrist_en_passant_square = position->zobrist_en_passant_square;

    // Handle en passant capture square
    if (move.flags & MOVE_FLAG_EN_PASSANT) {
        Piece expected_pawn;

        if (position->side_to_move == COLOR_WHITE) {
            undo->captured_square = move.to + BOARD_SIZE;
            expected_pawn = PIECE_BLACK_PAWN;
        } else {
            undo->captured_square = move.to - BOARD_SIZE;
            expected_pawn = PIECE_WHITE_PAWN;
        }

        undo->captured_piece = position_piece_at(
            position,
            undo->captured_square
        );

        if (target_piece != PIECE_NONE ||
            undo->captured_piece != expected_pawn) {
            return 0;
        }
    }

    // Clear source and captured piece
    position_set_piece(position, move.from, PIECE_NONE);
    if (undo->captured_piece != PIECE_NONE) {
        position_set_piece(
            position,
            undo->captured_square,
            PIECE_NONE
        );
    }

    // Place moved or promoted piece
    if (move.flags & MOVE_FLAG_PROMOTION) {
        placed_piece = move.promotion;
        position_set_piece(position, move.to, placed_piece);
    } else {
        placed_piece = moved_piece;
        position_set_piece(position, move.to, placed_piece);
    }

    // Move rook when castling
    if ((move.flags & MOVE_FLAG_CASTLE_KINGSIDE) ||
        (move.flags & MOVE_FLAG_CASTLE_QUEENSIDE)) {
        move_castling_rook(position, move);
    }

    update_castling_rights(position, move, undo);

    // Update en passant square
    position->en_passant_square = NO_SQUARE;
    if (move.flags & MOVE_FLAG_DOUBLE_PAWN) {
        position->en_passant_square = (move.from + move.to) / 2;
    }

    // Update move clocks
    if (piece_type(moved_piece) == PIECE_TYPE_PAWN ||
        undo->captured_piece != PIECE_NONE) {
        position->halfmove_clock = 0;
    } else {
        position->halfmove_clock++;
    }

    if (position->side_to_move == COLOR_BLACK) {
        position->fullmove_number++;
    }

    position->side_to_move = opposite_color(position->side_to_move);

    key ^= zobrist_piece_square_key(moved_piece, move.from);
    if (undo->captured_piece != PIECE_NONE) {
        key ^= zobrist_piece_square_key(
            undo->captured_piece,
            undo->captured_square
        );
    }
    key ^= zobrist_piece_square_key(placed_piece, move.to);

    if ((move.flags & MOVE_FLAG_CASTLE_KINGSIDE) ||
        (move.flags & MOVE_FLAG_CASTLE_QUEENSIDE)) {
        int row = square_row(move.from);
        int rook_from = (move.flags & MOVE_FLAG_CASTLE_KINGSIDE)
            ? make_square(row, 7)
            : make_square(row, 0);
        int rook_to = (move.flags & MOVE_FLAG_CASTLE_KINGSIDE)
            ? make_square(row, 5)
            : make_square(row, 3);
        Piece rook = piece_color(moved_piece) == COLOR_WHITE
            ? PIECE_WHITE_ROOK
            : PIECE_BLACK_ROOK;

        key ^= zobrist_piece_square_key(rook, rook_from);
        key ^= zobrist_piece_square_key(rook, rook_to);
    }

    key ^= zobrist_castling_rights_key(old_castling_rights);
    key ^= zobrist_castling_rights_key(position->castling_rights);
    key ^= zobrist_en_passant_key(old_en_passant_index);
    key ^= zobrist_en_passant_key(zobrist_en_passant_index(position));
    key ^= zobrist_side_to_move_key();

    position->zobrist_key = key;
    position->zobrist_key_valid = 1;
    position->zobrist_side_to_move = position->side_to_move;
    position->zobrist_castling_rights = position->castling_rights;
    position->zobrist_en_passant_square = position->en_passant_square;
    return 1;
}

void undo_move(Position *position, Move move, const UndoState *undo) {
    if (position == 0 || undo == 0) {
        return;
    }

    if (!is_valid_square(move.from) || !is_valid_square(move.to)) {
        return;
    }

    // Move castling rook back first
    if ((move.flags & MOVE_FLAG_CASTLE_KINGSIDE) ||
        (move.flags & MOVE_FLAG_CASTLE_QUEENSIDE)) {
        undo_castling_rook(position, move);
    }

    // Restore moved and captured pieces
    position_set_piece(position, move.to, PIECE_NONE);
    position_set_piece(position, move.from, undo->moved_piece);

    if (undo->captured_piece != PIECE_NONE) {
        position_set_piece(
            position,
            undo->captured_square,
            undo->captured_piece
        );
    }

    // Restore position state
    position->side_to_move = undo->side_to_move;
    position->castling_rights = undo->castling_rights;
    position->en_passant_square = undo->en_passant_square;
    position->halfmove_clock = undo->halfmove_clock;
    position->fullmove_number = undo->fullmove_number;
    position->zobrist_key = undo->zobrist_key;
    position->zobrist_key_valid = undo->zobrist_key_valid;
    position->zobrist_side_to_move = undo->zobrist_side_to_move;
    position->zobrist_castling_rights = undo->zobrist_castling_rights;
    position->zobrist_en_passant_square = undo->zobrist_en_passant_square;
}

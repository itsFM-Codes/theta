#include "zobrist.h"

static uint64_t piece_keys[PIECE_BLACK_KING + 1][SQUARE_COUNT];
static uint64_t castling_keys[CASTLING_ALL + 1];
static uint64_t en_passant_keys[SQUARE_COUNT + 1];
static uint64_t side_to_move_key;
static int keys_initialized;

static uint64_t next_random_key(uint64_t *state) {
    uint64_t value;

    *state += UINT64_C(0x9e3779b97f4a7c15);
    value = *state;
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static void initialize_keys(void) {
    uint64_t state = UINT64_C(0x4d595df4d0f33173);
    int piece;
    int square;
    int rights;

    if (keys_initialized) {
        return;
    }

    for (piece = PIECE_NONE; piece <= PIECE_BLACK_KING; ++piece) {
        for (square = 0; square < SQUARE_COUNT; ++square) {
            piece_keys[piece][square] = next_random_key(&state);
        }
    }

    for (rights = CASTLING_NONE; rights <= CASTLING_ALL; ++rights) {
        castling_keys[rights] = next_random_key(&state);
    }

    for (square = 0; square <= SQUARE_COUNT; ++square) {
        en_passant_keys[square] = next_random_key(&state);
    }

    side_to_move_key = next_random_key(&state);
    keys_initialized = 1;
}

static int en_passant_capture_is_possible(const Position *position) {
    int row;
    int column;
    int pawn_row;
    int file;
    Piece pawn;

    if (position == 0 ||
        !is_valid_square(position->en_passant_square) ||
        position->side_to_move == COLOR_NONE) {
        return 0;
    }

    row = square_row(position->en_passant_square);
    column = square_column(position->en_passant_square);
    pawn_row = position->side_to_move == COLOR_WHITE ? row + 1 : row - 1;
    pawn = position->side_to_move == COLOR_WHITE
        ? PIECE_WHITE_PAWN
        : PIECE_BLACK_PAWN;

    if (pawn_row < 0 || pawn_row >= BOARD_SIZE) {
        return 0;
    }

    for (file = column - 1; file <= column + 1; file += 2) {
        if (file >= 0 && file < BOARD_SIZE &&
            position_piece_at_coordinates(position, pawn_row, file) == pawn) {
            return 1;
        }
    }

    return 0;
}

uint64_t position_key(const Position *position) {
    uint64_t key = 0;
    int square;
    int en_passant_index;

    if (position == 0) {
        return 0;
    }

    if (position->zobrist_key_valid &&
        position->zobrist_side_to_move == position->side_to_move &&
        position->zobrist_castling_rights == position->castling_rights &&
        position->zobrist_en_passant_square == position->en_passant_square) {
        return position->zobrist_key;
    }

    initialize_keys();

    for (square = 0; square < SQUARE_COUNT; ++square) {
        Piece piece = position_piece_at(position, square);

        if (piece != PIECE_NONE) {
            key ^= piece_keys[piece][square];
        }
    }

    if (position->side_to_move == COLOR_BLACK) {
        key ^= side_to_move_key;
    }

    key ^= castling_keys[position->castling_rights];

    en_passant_index = en_passant_capture_is_possible(position)
        ? position->en_passant_square + 1
        : 0;

    key ^= en_passant_keys[en_passant_index];
    position->zobrist_key = key;
    position->zobrist_key_valid = 1;
    position->zobrist_side_to_move = position->side_to_move;
    position->zobrist_castling_rights = position->castling_rights;
    position->zobrist_en_passant_square = position->en_passant_square;
    return key;
}

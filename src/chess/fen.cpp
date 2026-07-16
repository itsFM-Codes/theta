#include "fen.h"

#include <stdlib.h>

static Piece piece_from_fen_character(char character) {
    switch (character) {
        case 'P':
            return PIECE_WHITE_PAWN;
        case 'N':
            return PIECE_WHITE_KNIGHT;
        case 'B':
            return PIECE_WHITE_BISHOP;
        case 'R':
            return PIECE_WHITE_ROOK;
        case 'Q':
            return PIECE_WHITE_QUEEN;
        case 'K':
            return PIECE_WHITE_KING;
        case 'p':
            return PIECE_BLACK_PAWN;
        case 'n':
            return PIECE_BLACK_KNIGHT;
        case 'b':
            return PIECE_BLACK_BISHOP;
        case 'r':
            return PIECE_BLACK_ROOK;
        case 'q':
            return PIECE_BLACK_QUEEN;
        case 'k':
            return PIECE_BLACK_KING;
        default:
            return PIECE_NONE;
    }
}

static int parse_board(Position *position, const char **cursor) {
    int row;

    for (row = 0; row < BOARD_SIZE; ++row) {
        int column = 0;

        while (**cursor != '\0' &&
               **cursor != '/' &&
               **cursor != ' ') {
            char character = **cursor;

            if (character >= '1' && character <= '8') {
                column += character - '0';
            } else {
                Piece piece = piece_from_fen_character(character);

                if (piece == PIECE_NONE || column >= BOARD_SIZE) {
                    return 0;
                }

                position_set_piece_at_coordinates(
                    position,
                    row,
                    column,
                    piece
                );
                column++;
            }

            if (column > BOARD_SIZE) {
                return 0;
            }

            (*cursor)++;
        }

        if (column != BOARD_SIZE) {
            return 0;
        }

        if (row < BOARD_SIZE - 1) {
            if (**cursor != '/') {
                return 0;
            }
            (*cursor)++;
        }
    }

    return **cursor == ' ';
}

static int parse_side_to_move(Position *position, const char **cursor) {
    (*cursor)++;

    if (**cursor == 'w') {
        position->side_to_move = COLOR_WHITE;
    } else if (**cursor == 'b') {
        position->side_to_move = COLOR_BLACK;
    } else {
        return 0;
    }

    (*cursor)++;
    return **cursor == ' ';
}

static int parse_castling_rights(Position *position, const char **cursor) {
    (*cursor)++;
    position->castling_rights = CASTLING_NONE;

    if (**cursor == '-') {
        (*cursor)++;
        return **cursor == ' ';
    }

    while (**cursor != '\0' && **cursor != ' ') {
        switch (**cursor) {
            case 'K':
                position->castling_rights |= CASTLING_WHITE_KING_SIDE;
                break;
            case 'Q':
                position->castling_rights |= CASTLING_WHITE_QUEEN_SIDE;
                break;
            case 'k':
                position->castling_rights |= CASTLING_BLACK_KING_SIDE;
                break;
            case 'q':
                position->castling_rights |= CASTLING_BLACK_QUEEN_SIDE;
                break;
            default:
                return 0;
        }
        (*cursor)++;
    }

    return **cursor == ' ';
}

static int parse_en_passant_square(Position *position, const char **cursor) {
    int column;
    int row;

    (*cursor)++;
    if (**cursor == '-') {
        position->en_passant_square = NO_SQUARE;
        (*cursor)++;
        return **cursor == ' ';
    }

    if ((*cursor)[0] < 'a' || (*cursor)[0] > 'h' ||
        (*cursor)[1] < '1' || (*cursor)[1] > '8') {
        return 0;
    }

    column = (*cursor)[0] - 'a';
    row = '8' - (*cursor)[1];
    position->en_passant_square = make_square(row, column);
    *cursor += 2;
    return **cursor == ' ';
}

static int parse_number(const char **cursor, int *value) {
    char *end;
    long number;

    while (**cursor == ' ') {
        (*cursor)++;
    }

    number = strtol(*cursor, &end, 10);
    if (end == *cursor || number < 0) {
        return 0;
    }

    *value = (int)number;
    *cursor = end;
    return 1;
}

int position_from_fen(Position *position, const char *fen) {
    Position parsed;
    const char *cursor = fen;

    if (position == 0 || fen == 0) {
        return 0;
    }

    clear_position(&parsed);

    if (!parse_board(&parsed, &cursor) ||
        !parse_side_to_move(&parsed, &cursor) ||
        !parse_castling_rights(&parsed, &cursor) ||
        !parse_en_passant_square(&parsed, &cursor) ||
        !parse_number(&cursor, &parsed.halfmove_clock) ||
        !parse_number(&cursor, &parsed.fullmove_number)) {
        return 0;
    }

    while (*cursor == ' ') {
        cursor++;
    }

    if (*cursor != '\0' || parsed.fullmove_number < 1) {
        return 0;
    }

    *position = parsed;
    return 1;
}

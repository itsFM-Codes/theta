#ifndef BOARD_H
#define BOARD_H

// Board dimensions
#define BOARD_SIZE 8

// Piece types
#define EMPTY 0
#define PAWN 1
#define ROOK 2
#define KNIGHT 3
#define BISHOP 4
#define QUEEN 5
#define KING 6

// Colors
#define WHITE 0
#define BLACK 1

// Function declarations
void init_board();
void print_board();
int is_valid_position(int row, int col);
int get_piece(int row, int col);
void set_piece(int row, int col, int piece);
int get_color(int row, int col);
void set_color(int row, int col, int color);

#endif // BOARD_H
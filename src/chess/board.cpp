#include <stdio.h>
#include "src/chess/board.h"

// Internal board representation
static int board[BOARD_SIZE][BOARD_SIZE];
static int colors[BOARD_SIZE][BOARD_SIZE];

void init_board() {
    // Initialize the board with starting positions
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            board[i][j] = EMPTY;
            colors[i][j] = EMPTY;
        }
    }
}

void print_board() {
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            printf("%d ", board[i][j]);
        }
        printf("\n");
    }
}

int is_valid_position(int row, int col) {
    return (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE);
}

int get_piece(int row, int col) {
    if (!is_valid_position(row, col)) {
        return EMPTY;
    }
    return board[row][col];
}

void set_piece(int row, int col, int piece) {
    if (is_valid_position(row, col)) {
        board[row][col] = piece;
    }
}

int get_color(int row, int col) {
    if (!is_valid_position(row, col)) {
        return EMPTY;
    }
    return colors[row][col];
}

void set_color(int row, int col, int color) {
    if (is_valid_position(row, col)) {
        colors[row][col] = color;
    }
}
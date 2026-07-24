#ifndef ZOBRIST_H
#define ZOBRIST_H

#include <stdint.h>

#include "position.h"

uint64_t position_key(const Position *position);
uint64_t zobrist_piece_square_key(Piece piece, int square);
uint64_t zobrist_castling_rights_key(int castling_rights);
uint64_t zobrist_en_passant_key(int en_passant_index);
uint64_t zobrist_side_to_move_key(void);
int zobrist_en_passant_index(const Position *position);
void reset_zobrist_statistics(void);
void get_zobrist_statistics(uint64_t *cache_hits, uint64_t *rebuilds);

#endif // ZOBRIST_H

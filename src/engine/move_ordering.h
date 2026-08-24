#ifndef MOVE_ORDERING_H
#define MOVE_ORDERING_H

#include "search_internal.h"

typedef struct MovePicker {
    MoveList *moves;
    int scores[MAX_MOVES];
    int see_scores[MAX_MOVES];
    unsigned char see_valid[MAX_MOVES];
    unsigned char gives_check[MAX_MOVES];
    unsigned char check_valid[MAX_MOVES];
    uint64_t threat_by_lesser[PIECE_TYPE_KING + 1];
    int next_index;
} MovePicker;

void initialize_move_picker(
    MovePicker *picker,
    Position *position,
    MoveList *moves,
    int order_checks,
    const SearchContext *context,
    int ply,
    const Move *table_move
);
int move_picker_next(MovePicker *picker, Move *move);
int quiet_history_score(
    const SearchContext *context,
    const Position *position,
    Color color,
    int ply,
    Move move
);
int pawn_history_score(
    const SearchContext *context,
    const Position *position,
    Color color,
    PieceType moving_type,
    int target_square
);
int capture_history_score(
    const SearchContext *context,
    Color color,
    PieceType attacker_type,
    int target_square,
    PieceType captured_type
);

int correction_history_score(
    const SearchContext *context,
    const Position *position,
    int ply
);
void record_correction_history(
    SearchContext *context,
    const Position *position,
    int ply,
    int score_delta
);



void order_moves(
    Position *position,
    MoveList *moves,
    int order_checks,
    const SearchContext *context,
    int ply,
    const Move *table_move
);

void record_quiet_cutoff(
    SearchContext *context,
    const Position *position,
    Color color,
    int ply,
    int depth,
    Move move
);
void record_quiet_failures(
    SearchContext *context,
    const Position *position,
    Color color,
    int ply,
    int depth,
    const MoveList *moves,
    int count
);
void record_capture_cutoff(
    SearchContext *context,
    const Position *position,
    Color color,
    int depth,
    Move move
);
void record_capture_failures(
    SearchContext *context,
    const Position *position,
    Color color,
    int depth,
    const MoveList *moves,
    int count
);

#endif // MOVE_ORDERING_H

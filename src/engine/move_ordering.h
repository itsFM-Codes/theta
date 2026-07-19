#ifndef MOVE_ORDERING_H
#define MOVE_ORDERING_H

#include "search_internal.h"

typedef struct MovePicker {
    MoveList *moves;
    int scores[MAX_MOVES];
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
    Color color,
    int ply,
    int depth,
    Move move
);
void record_quiet_failures(
    SearchContext *context,
    Color color,
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

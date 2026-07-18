#include <assert.h>

#include "src/eval/evaluation.h"
#include "src/eval/king_safety.h"
#include "src/eval/mobility.h"
#include "src/eval/pawn_structure.h"
#include "src/eval/piece_activity.h"
#include "src/eval/piece_square_tables.h"

static void test_starting_position(void) {
    Position position;

    set_starting_position(&position);
    assert(evaluate_position(&position) == 0);
}

static void test_side_to_move_score(void) {
    Position position;
    int white_score;

    clear_position(&position);
    position_set_piece(&position, 0, PIECE_WHITE_PAWN);

    position.side_to_move = COLOR_WHITE;
    white_score = evaluate_position(&position);
    assert(white_score > PAWN_VALUE);

    position.side_to_move = COLOR_BLACK;
    assert(evaluate_position(&position) == -white_score);
}

static void test_material_difference(void) {
    Position position;

    clear_position(&position);
    position_set_piece(&position, 0, PIECE_WHITE_QUEEN);
    position_set_piece(&position, 1, PIECE_BLACK_ROOK);
    position_set_piece(&position, 2, PIECE_BLACK_PAWN);

    assert(evaluate_position(&position) > 200);
}

static void test_piece_square_tables(void) {
    Position corner_position;
    Position center_position;

    clear_position(&corner_position);
    position_set_piece(&corner_position, 56, PIECE_WHITE_KNIGHT);
    corner_position.side_to_move = COLOR_WHITE;

    clear_position(&center_position);
    position_set_piece(&center_position, 35, PIECE_WHITE_KNIGHT);
    center_position.side_to_move = COLOR_WHITE;

    assert(evaluate_position(&center_position) >
           evaluate_position(&corner_position));
}

static void test_endgame_king_table(void) {
    int center_value = piece_square_value(
        PIECE_WHITE_KING,
        make_square(3, 3),
        256
    );
    int corner_value = piece_square_value(
        PIECE_WHITE_KING,
        make_square(7, 0),
        256
    );

    assert(center_value > corner_value);
}

static void test_bishop_mobility(void) {
    Position corner_position;
    Position center_position;

    clear_position(&corner_position);
    position_set_piece(&corner_position, make_square(7, 0), PIECE_WHITE_BISHOP);

    clear_position(&center_position);
    position_set_piece(&center_position, make_square(4, 3), PIECE_WHITE_BISHOP);

    assert(mobility_score(&center_position) > mobility_score(&corner_position));
}

static void test_pawn_structure(void) {
    Position doubled_pawns;
    Position separate_pawns;
    Position passed_pawn;
    Position blocked_pawn;
    Position unsupported_passer;
    Position supported_passer;
    Position connected_passers;
    Position separate_passers;

    clear_position(&doubled_pawns);
    position_set_piece(&doubled_pawns, make_square(6, 3), PIECE_WHITE_PAWN);
    position_set_piece(&doubled_pawns, make_square(5, 3), PIECE_WHITE_PAWN);

    clear_position(&separate_pawns);
    position_set_piece(&separate_pawns, make_square(6, 2), PIECE_WHITE_PAWN);
    position_set_piece(&separate_pawns, make_square(6, 3), PIECE_WHITE_PAWN);

    assert(pawn_structure_score(&separate_pawns) >
           pawn_structure_score(&doubled_pawns));

    clear_position(&passed_pawn);
    position_set_piece(&passed_pawn, make_square(3, 3), PIECE_WHITE_PAWN);

    clear_position(&blocked_pawn);
    position_set_piece(&blocked_pawn, make_square(3, 3), PIECE_WHITE_PAWN);
    position_set_piece(&blocked_pawn, make_square(2, 3), PIECE_BLACK_PAWN);

    assert(pawn_structure_score(&passed_pawn) >
           pawn_structure_score(&blocked_pawn));

    clear_position(&unsupported_passer);
    position_set_piece(
        &unsupported_passer,
        make_square(3, 3),
        PIECE_WHITE_PAWN
    );

    clear_position(&supported_passer);
    position_set_piece(
        &supported_passer,
        make_square(3, 3),
        PIECE_WHITE_PAWN
    );
    position_set_piece(
        &supported_passer,
        make_square(4, 2),
        PIECE_WHITE_PAWN
    );

    assert(pawn_structure_score(&supported_passer) >
           pawn_structure_score(&unsupported_passer));

    clear_position(&connected_passers);
    position_set_piece(
        &connected_passers,
        make_square(3, 3),
        PIECE_WHITE_PAWN
    );
    position_set_piece(
        &connected_passers,
        make_square(3, 4),
        PIECE_WHITE_PAWN
    );

    clear_position(&separate_passers);
    position_set_piece(
        &separate_passers,
        make_square(3, 3),
        PIECE_WHITE_PAWN
    );
    position_set_piece(
        &separate_passers,
        make_square(3, 5),
        PIECE_WHITE_PAWN
    );

    assert(pawn_structure_score(&connected_passers) >
           pawn_structure_score(&separate_passers));
}

static void test_king_safety(void) {
    Position sheltered_king;
    Position exposed_king;

    clear_position(&sheltered_king);
    position_set_piece(&sheltered_king, make_square(7, 6), PIECE_WHITE_KING);
    position_set_piece(&sheltered_king, make_square(6, 5), PIECE_WHITE_PAWN);
    position_set_piece(&sheltered_king, make_square(6, 6), PIECE_WHITE_PAWN);
    position_set_piece(&sheltered_king, make_square(6, 7), PIECE_WHITE_PAWN);

    clear_position(&exposed_king);
    position_set_piece(&exposed_king, make_square(7, 6), PIECE_WHITE_KING);

    assert(king_safety_score(&sheltered_king, 0) >
           king_safety_score(&exposed_king, 0));
}

static void test_piece_activity(void) {
    Position bishop_pair;
    Position single_bishop;
    Position open_file_rook;
    Position blocked_rook;

    clear_position(&bishop_pair);
    position_set_piece(&bishop_pair, make_square(7, 2), PIECE_WHITE_BISHOP);
    position_set_piece(&bishop_pair, make_square(7, 5), PIECE_WHITE_BISHOP);

    clear_position(&single_bishop);
    position_set_piece(&single_bishop, make_square(7, 2), PIECE_WHITE_BISHOP);

    assert(piece_activity_score(&bishop_pair) >
           piece_activity_score(&single_bishop));

    clear_position(&open_file_rook);
    position_set_piece(&open_file_rook, make_square(7, 3), PIECE_WHITE_ROOK);

    clear_position(&blocked_rook);
    position_set_piece(&blocked_rook, make_square(7, 3), PIECE_WHITE_ROOK);
    position_set_piece(&blocked_rook, make_square(6, 3), PIECE_WHITE_PAWN);

    assert(piece_activity_score(&open_file_rook) >
           piece_activity_score(&blocked_rook));
}

int main(void) {
    test_starting_position();
    test_side_to_move_score();
    test_material_difference();
    test_piece_square_tables();
    test_endgame_king_table();
    test_bishop_mobility();
    test_pawn_structure();
    test_king_safety();
    test_piece_activity();
    return 0;
}

#include "uci.h"

#include <atomic>
#include <ctype.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <mutex>
#include <thread>
#endif

#include "src/chess/fen.h"
#include "src/chess/movegen.h"
#include "src/chess/zobrist.h"
#include "src/config/config.h"
#include "src/engine/search.h"

#define UCI_LINE_SIZE 4096
#define UCI_FEN_SIZE 256
#define UCI_MOVE_OVERHEAD_MS 10

#ifdef _WIN32
class UciMutex {
public:
    UciMutex() { InitializeCriticalSection(&section); }
    ~UciMutex() { DeleteCriticalSection(&section); }
    void lock() { EnterCriticalSection(&section); }
    void unlock() { LeaveCriticalSection(&section); }
private:
    CRITICAL_SECTION section;
};
#else
typedef std::mutex UciMutex;
#endif

class UciLock {
public:
    explicit UciLock(UciMutex &mutex) : mutex_(mutex) { mutex_.lock(); }
    ~UciLock() { mutex_.unlock(); }
private:
    UciMutex &mutex_;
};

static UciMutex uci_output_mutex;

static void print_uci_move(Move move) {
    char promotion = '\0';

    printf(
        "%c%c%c%c",
        'a' + square_column(move.from),
        '8' - square_row(move.from),
        'a' + square_column(move.to),
        '8' - square_row(move.to)
    );

    switch (piece_type(move.promotion)) {
        case PIECE_TYPE_QUEEN:
            promotion = 'q';
            break;
        case PIECE_TYPE_ROOK:
            promotion = 'r';
            break;
        case PIECE_TYPE_BISHOP:
            promotion = 'b';
            break;
        case PIECE_TYPE_KNIGHT:
            promotion = 'n';
            break;
        default:
            break;
    }

    if (promotion != '\0') {
        putchar(promotion);
    }
}

static int move_matches_uci(const Move *move, const char *text) {
    int from;
    int to;
    char promotion = '\0';

    if (move == 0 || text == 0 ||
        (strlen(text) != 4 && strlen(text) != 5) ||
        text[0] < 'a' || text[0] > 'h' ||
        text[1] < '1' || text[1] > '8' ||
        text[2] < 'a' || text[2] > 'h' ||
        text[3] < '1' || text[3] > '8') {
        return 0;
    }

    from = make_square('8' - text[1], text[0] - 'a');
    to = make_square('8' - text[3], text[2] - 'a');

    if (move->from != from || move->to != to) {
        return 0;
    }

    if (strlen(text) == 5) {
        promotion = (char)tolower((unsigned char)text[4]);
    }

    if ((move->flags & MOVE_FLAG_PROMOTION) == 0) {
        return promotion == '\0';
    }

    switch (piece_type(move->promotion)) {
        case PIECE_TYPE_QUEEN:
            return promotion == 'q';
        case PIECE_TYPE_ROOK:
            return promotion == 'r';
        case PIECE_TYPE_BISHOP:
            return promotion == 'b';
        case PIECE_TYPE_KNIGHT:
            return promotion == 'n';
        default:
            return 0;
    }
}

static int apply_uci_move(Position *position, const char *text) {
    MoveList moves;
    int index;

    if (position == 0 || text == 0) {
        return 0;
    }

    generate_legal_moves(position, &moves);

    for (index = 0; index < moves.count; ++index) {
        UndoState undo;

        if (move_matches_uci(&moves.moves[index], text)) {
            return make_move(position, moves.moves[index], &undo);
        }
    }

    return 0;
}

static char *next_token(char **cursor) {
    char *start;

    if (cursor == 0 || *cursor == 0) {
        return 0;
    }

    while (**cursor == ' ' || **cursor == '\t') {
        (*cursor)++;
    }

    if (**cursor == '\0') {
        return 0;
    }

    start = *cursor;

    while (**cursor != '\0' && **cursor != ' ' && **cursor != '\t') {
        (*cursor)++;
    }

    if (**cursor != '\0') {
        **cursor = '\0';
        (*cursor)++;
    }

    return start;
}

static int set_position_from_command(
    Position *position,
    std::vector<uint64_t> *history,
    char *arguments
) {
    char fen[UCI_FEN_SIZE];
    char *cursor = arguments;
    char *token = next_token(&cursor);
    int field;

    if (token == 0 || position == 0 || history == 0) {
        return 0;
    }

    if (strcmp(token, "startpos") == 0) {
        set_starting_position(position);
        token = next_token(&cursor);
    } else if (strcmp(token, "fen") == 0) {
        fen[0] = '\0';

        for (field = 0; field < 6; ++field) {
            token = next_token(&cursor);

            if (token == 0 ||
                strlen(fen) + strlen(token) + 2 >= sizeof(fen)) {
                return 0;
            }

            if (field > 0) {
                strcat(fen, " ");
            }

            strcat(fen, token);
        }

        if (!position_from_fen(position, fen)) {
            return 0;
        }

        token = next_token(&cursor);
    } else {
        return 0;
    }

    history->clear();
    history->push_back(position_key(position));

    if (token == 0) {
        return 1;
    }

    if (strcmp(token, "moves") != 0) {
        return 0;
    }

    while ((token = next_token(&cursor)) != 0) {
        if (!apply_uci_move(position, token)) {
            return 0;
        }
        history->push_back(position_key(position));
    }

    return 1;
}

static int parse_non_negative(const char *text, int *value) {
    char *end;
    long parsed;

    if (text == 0 || value == 0) {
        return 0;
    }

    parsed = strtol(text, &end, 10);

    if (*text == '\0' || *end != '\0' || parsed < 0 || parsed > 2147483647L) {
        return 0;
    }

    *value = (int)parsed;
    return 1;
}

static void print_search_info(
    int depth,
    int score,
    const PrincipalVariation *variation,
    const SearchStatistics *statistics,
    void *user_data
) {
    int index;
    uint64_t nps = 0;

    UciMutex *output_mutex = (UciMutex *)user_data;
    UciLock lock(
        output_mutex == 0 ? uci_output_mutex : *output_mutex
    );
    if (statistics != 0 && statistics->elapsed_ms > 0) {
        nps = statistics->nodes * 1000u / (uint64_t)statistics->elapsed_ms;
    }

    printf("info depth %d seldepth %d ", depth,
           statistics == 0 ? depth : statistics->selective_depth);
    if (score > SEARCH_CHECKMATE - MAX_PRINCIPAL_VARIATION) {
        printf("score mate %d ", (SEARCH_CHECKMATE - score + 1) / 2);
    } else if (score < -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION) {
        printf("score mate -%d ", (SEARCH_CHECKMATE + score + 1) / 2);
    } else {
        printf("score cp %d ", score);
    }
    printf("nodes ");
    std::cout << (statistics == 0 ? 0 : statistics->nodes)
              << " nps " << nps
              << " time " << (statistics == 0 ? 0 : statistics->elapsed_ms)
              << " hashfull " << (statistics == 0 ? 0 : statistics->hashfull)
              << " pv";

    for (index = 0; index < variation->count; ++index) {
        putchar(' ');
        print_uci_move(variation->moves[index]);
    }

    putchar('\n');
    fflush(stdout);
}

static void search_from_command(
    SearchSharedState *shared_state,
    Position position,
    const std::vector<uint64_t> &history,
    char *arguments,
    std::atomic<bool> *stop_requested
) {
    char *cursor = arguments;
    char *token;
    Move best_move;
    PrincipalVariation variation;
    int completed_depth;
    int depth = g_config.max_depth;
    int time_limit_ms = 0;
    int white_time = 0;
    int black_time = 0;
    int white_increment = 0;
    int black_increment = 0;
    int moves_to_go = 0;
    int infinite = 0;
    uint64_t node_limit = 0;
    int value;
    SearchLimits limits = {};

    while ((token = next_token(&cursor)) != 0) {
        if (strcmp(token, "depth") == 0) {
            token = next_token(&cursor);

            if (parse_non_negative(token, &value) && value > 0) {
                depth = value;
            }
        } else if (strcmp(token, "movetime") == 0) {
            token = next_token(&cursor);

            if (parse_non_negative(token, &value)) {
                time_limit_ms = value;
            }
        } else if (strcmp(token, "wtime") == 0) {
            token = next_token(&cursor);
            parse_non_negative(token, &white_time);
        } else if (strcmp(token, "btime") == 0) {
            token = next_token(&cursor);
            parse_non_negative(token, &black_time);
        } else if (strcmp(token, "winc") == 0) {
            token = next_token(&cursor);
            parse_non_negative(token, &white_increment);
        } else if (strcmp(token, "binc") == 0) {
            token = next_token(&cursor);
            parse_non_negative(token, &black_increment);
        } else if (strcmp(token, "movestogo") == 0) {
            token = next_token(&cursor);
            parse_non_negative(token, &moves_to_go);
        } else if (strcmp(token, "nodes") == 0) {
            token = next_token(&cursor);
            if (parse_non_negative(token, &value)) {
                node_limit = (uint64_t)value;
            }
        } else if (strcmp(token, "infinite") == 0) {
            infinite = 1;
        }
    }

    if (infinite) {
        depth = 128;
        time_limit_ms = 0;
    } else if (depth > g_config.max_depth) {
        depth = g_config.max_depth;
    }

    limits.soft_time_ms = time_limit_ms;
    limits.hard_time_ms = time_limit_ms;

    if (!infinite && time_limit_ms == 0) {
        int remaining_time = position.side_to_move == COLOR_WHITE
            ? white_time
            : black_time;
        int increment = position.side_to_move == COLOR_WHITE
            ? white_increment
            : black_increment;

        if (remaining_time > 0) {
            int divisor = moves_to_go > 0 ? moves_to_go : 30;
            int available_time = remaining_time - UCI_MOVE_OVERHEAD_MS;

            if (available_time < 1) {
                available_time = 1;
            }
            limits.soft_time_ms = remaining_time / divisor +
                increment * 3 / 4;
            if (limits.soft_time_ms < 1) {
                limits.soft_time_ms = 1;
            }
            if (limits.soft_time_ms > available_time) {
                limits.soft_time_ms = available_time;
            }
            limits.hard_time_ms = limits.soft_time_ms * 3;
            if (limits.hard_time_ms < limits.soft_time_ms + increment) {
                limits.hard_time_ms = limits.soft_time_ms + increment;
            }
            if (limits.hard_time_ms > available_time) {
                limits.hard_time_ms = available_time;
            }
        }
    }

    limits.node_limit = node_limit;
    limits.poll_interval = DEFAULT_SEARCH_POLL_INTERVAL;
    limits.stop_requested = stop_requested;
    limits.game_history = history.empty() ? 0 : history.data();
    limits.game_history_count = (int)history.size();

    search_iterative_with_state_and_limits(
        shared_state,
        &position,
        depth,
        &limits,
        &best_move,
        &variation,
        &completed_depth,
        print_search_info,
        &uci_output_mutex
    );

    UciLock lock(uci_output_mutex);
    printf("bestmove ");

    if (is_valid_square(best_move.from) && is_valid_square(best_move.to)) {
        print_uci_move(best_move);
    } else {
        printf("0000");
    }

    printf("\n");
    fflush(stdout);
}

typedef struct UciSearchTask {
    SearchSharedState *shared_state;
    Position position;
    std::vector<uint64_t> history;
    std::string arguments;
    std::atomic<bool> *stop_requested;
} UciSearchTask;

static void execute_search_task(UciSearchTask *task) {
    std::vector<char> mutable_arguments(
        task->arguments.begin(),
        task->arguments.end()
    );
    mutable_arguments.push_back('\0');
    search_from_command(
        task->shared_state,
        task->position,
        task->history,
        mutable_arguments.data(),
        task->stop_requested
    );
    delete task;
}

#ifdef _WIN32
static DWORD WINAPI uci_search_thread_proc(LPVOID data) {
    execute_search_task((UciSearchTask *)data);
    return 0;
}
#endif

int run_uci(void) {
    Position position;
    SearchSharedState shared_state;
    std::vector<uint64_t> position_history;
#ifdef _WIN32
    HANDLE search_thread = 0;
#else
    std::thread search_thread;
#endif
    std::atomic<bool> stop_requested(false);
    char line[UCI_LINE_SIZE];

    if (!initialize_search_shared_state(&shared_state)) {
        fprintf(stderr, "Error: Could not initialize shared search state\n");
        return 0;
    }

    set_starting_position(&position);
    position_history.push_back(position_key(&position));

    auto stop_search = [&]() {
        stop_requested.store(true, std::memory_order_relaxed);
#ifdef _WIN32
        if (search_thread != 0) {
            WaitForSingleObject(search_thread, INFINITE);
            CloseHandle(search_thread);
            search_thread = 0;
        }
#else
        if (search_thread.joinable()) {
            search_thread.join();
        }
#endif
    };

    while (fgets(line, sizeof(line), stdin) != 0) {
        char *arguments;
        size_t length = strlen(line);

        while (length > 0 &&
               (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[length - 1] = '\0';
            length--;
        }

        arguments = line;

        while (*arguments == ' ' || *arguments == '\t') {
            arguments++;
        }

        if (strcmp(arguments, "uci") == 0) {
            UciLock lock(uci_output_mutex);
            printf("id name Theta\n");
            printf("id author FM\n");
            printf("option name Threads type spin default 1 min 1 max 1\n");
            printf("option name Hash type spin default %d min 1 max 1024\n",
                   DEFAULT_TRANSPOSITION_TABLE_MB);
            printf("option name Clear Hash type button\n");
            printf("uciok\n");
            fflush(stdout);
        } else if (strcmp(arguments, "isready") == 0) {
            UciLock lock(uci_output_mutex);
            printf("readyok\n");
            fflush(stdout);
        } else if (strcmp(arguments, "ucinewgame") == 0) {
            stop_search();
            clear_search_shared_state(&shared_state);
            set_starting_position(&position);
            position_history.clear();
            position_history.push_back(position_key(&position));
        } else if (strncmp(arguments, "setoption name Hash value ", 26) == 0) {
            int hash_mb;

            stop_search();
            if (parse_non_negative(arguments + 26, &hash_mb) &&
                hash_mb >= 1 && hash_mb <= 1024) {
                if (!resize_search_shared_state(&shared_state, hash_mb)) {
                    fprintf(stderr, "Error: Could not resize hash table\n");
                }
            }
        } else if (strcmp(arguments, "setoption name Clear Hash") == 0) {
            stop_search();
            clear_search_shared_state(&shared_state);
        } else if (strncmp(arguments, "position ", 9) == 0) {
            stop_search();
            set_position_from_command(
                &position,
                &position_history,
                arguments + 9
            );
        } else if (strncmp(arguments, "go", 2) == 0 &&
                   (arguments[2] == '\0' || arguments[2] == ' ' ||
                    arguments[2] == '\t')) {
            stop_search();
            stop_requested.store(false, std::memory_order_relaxed);
            advance_transposition_table_generation(
                &shared_state.transposition_table
            );
            UciSearchTask *task = new UciSearchTask;
            task->shared_state = &shared_state;
            task->position = position;
            task->history = position_history;
            task->arguments = arguments + 2;
            task->stop_requested = &stop_requested;
#ifdef _WIN32
            search_thread = CreateThread(
                0,
                0,
                uci_search_thread_proc,
                task,
                0,
                0
            );
            if (search_thread == 0) {
                execute_search_task(task);
            }
#else
            search_thread = std::thread(execute_search_task, task);
#endif
        } else if (strcmp(arguments, "stop") == 0) {
            stop_search();
        } else if (strcmp(arguments, "quit") == 0) {
            stop_search();
            destroy_search_shared_state(&shared_state);
            return 1;
        }
    }

    stop_search();
    destroy_search_shared_state(&shared_state);
    return 1;
}

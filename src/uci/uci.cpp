#include "uci.h"

#include <atomic>
#include <ctype.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#endif

#include "src/chess/fen.h"
#include "src/chess/movegen.h"
#include "src/chess/zobrist.h"
#include "src/config/config.h"
#include "src/engine/search.h"
#include "src/eval/nnue.h"


#define UCI_LINE_SIZE 4096
#define UCI_FEN_SIZE 256
#define UCI_MOVE_OVERHEAD_MS 10
#define NO_DRAW_SCORE 1000

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

static int read_uci_line(char *line, size_t size) {
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD read_count;
    size_t length = 0;

    if (line == 0 || size == 0 || input == INVALID_HANDLE_VALUE ||
        input == 0) {
        return 0;
    }

    while (length + 1 < size) {
        char character;

        if (!ReadFile(input, &character, 1, &read_count, 0) ||
            read_count == 0) {
            if (length == 0) {
                return 0;
            }
            break;
        }

        if (character == '\n') {
            break;
        }
        if (character != '\r') {
            line[length++] = character;
        }
    }

    line[length] = '\0';
    return 1;
#else
    return line != 0 && size > 0 && fgets(line, (int)size, stdin) != 0;
#endif
}

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

static int text_equals_ignore_case(const char *first, const char *second) {
    while (*first != '\0' && *second != '\0') {
        if (tolower((unsigned char)*first) !=
            tolower((unsigned char)*second)) {
            return 0;
        }
        first++;
        second++;
    }
    return *first == '\0' && *second == '\0';
}

static int parse_boolean(const char *text, int *value) {
    if (text == 0 || value == 0) {
        return 0;
    }
    if (text_equals_ignore_case(text, "true") || strcmp(text, "1") == 0) {
        *value = 1;
        return 1;
    }
    if (text_equals_ignore_case(text, "false") || strcmp(text, "0") == 0) {
        *value = 0;
        return 1;
    }
    return 0;
}

static int set_search_option_value(
    const char *arguments,
    const char *prefix,
    int minimum,
    int maximum,
    int *target
) {
    size_t prefix_length;
    int value;

    if (arguments == 0 || prefix == 0 || target == 0 ||
        strncmp(arguments, prefix, strlen(prefix)) != 0) {
        return 0;
    }

    prefix_length = strlen(prefix);
    if (parse_non_negative(arguments + prefix_length, &value) &&
        value >= minimum && value <= maximum) {
        *target = value;
    }
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

typedef struct ParallelSearchWorker {
    SearchSharedState state;
    int initialized;
    Move best_move;
    PrincipalVariation variation;
    int completed_depth;
    int score;
} ParallelSearchWorker;

typedef struct ParallelSearchTask {
    SearchSharedState *state;
    Position position;
    int maximum_depth;
    SearchLimits limits;
    Move *best_move;
    PrincipalVariation *variation;
    int *completed_depth;
    int *score;
    SearchInfoCallback callback;
    void *user_data;
} ParallelSearchTask;

static void clear_uci_move(Move *move) {
    if (move == 0) {
        return;
    }
    move->from = NO_SQUARE;
    move->to = NO_SQUARE;
    move->promotion = PIECE_NONE;
    move->flags = MOVE_FLAG_NONE;
}

static void execute_parallel_search_task(ParallelSearchTask *task) {
    if (task == 0) {
        return;
    }
    *task->score = search_iterative_with_state_and_limits(
        task->state,
        &task->position,
        task->maximum_depth,
        &task->limits,
        task->best_move,
        task->variation,
        task->completed_depth,
        task->callback,
        task->user_data
    );
}

#ifdef _WIN32
static DWORD WINAPI parallel_search_thread_proc(LPVOID data) {
    execute_parallel_search_task((ParallelSearchTask *)data);
    return 0;
}
#endif

static int maximum_uci_threads(void) {
#ifdef _WIN32
    SYSTEM_INFO system_info;

    GetSystemInfo(&system_info);
    if (system_info.dwNumberOfProcessors == 0) {
        return 1;
    }
    return system_info.dwNumberOfProcessors > 64
        ? 64
        : (int)system_info.dwNumberOfProcessors;
#else
    unsigned int available = std::thread::hardware_concurrency();

    if (available == 0) {
        return 1;
    }
    return available > 64 ? 64 : (int)available;
#endif
}

static int search_parallel(
    SearchSharedState *base_state,
    const Position *position,
    int maximum_depth,
    const SearchLimits *base_limits,
    int thread_count,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
) {
    std::vector<ParallelSearchWorker> workers;
#ifdef _WIN32
    std::vector<HANDLE> search_threads;
#else
    std::vector<std::thread> search_threads;
#endif
    std::vector<ParallelSearchTask> tasks;
    std::atomic<bool> local_stop_requested(false);
    std::atomic<bool> *stop_requested = &local_stop_requested;
    int best_index = -1;
    int index;

    if (thread_count <= 1 || base_state == 0 || position == 0) {
        Position worker_position = position == 0 ? Position{} : *position;
        return search_iterative_with_state_and_limits(
            base_state,
            &worker_position,
            maximum_depth,
            base_limits,
            best_move,
            variation,
            completed_depth,
            callback,
            user_data
        );
    }

    if (base_limits != 0 && base_limits->stop_requested != 0) {
        stop_requested = base_limits->stop_requested;
    }

    workers.resize((size_t)thread_count);
    tasks.resize((size_t)thread_count);
    for (index = 0; index < thread_count; ++index) {
        memset(&workers[index], 0, sizeof(workers[index]));
        clear_uci_move(&workers[index].best_move);
        workers[index].state.transposition_table =
            base_state->transposition_table;
        workers[index].state.transposition_table.thread_safe = 1;
        workers[index].state.heuristics =
            (SearchHeuristicTables *)calloc(
                1,
                sizeof(SearchHeuristicTables)
            );
        if (workers[index].state.heuristics == 0) {
            for (int cleanup = 0; cleanup < index; ++cleanup) {
                if (workers[cleanup].initialized) {
                    free(workers[cleanup].state.heuristics);
                    workers[cleanup].state.heuristics = 0;
                }
            }
            {
                Position fallback_position = *position;
                return search_iterative_with_state_and_limits(
                    base_state,
                    &fallback_position,
                    maximum_depth,
                    base_limits,
                    best_move,
                    variation,
                    completed_depth,
                    callback,
                    user_data
                );
            }
        }
        workers[index].initialized = 1;
    }

    for (index = 0; index < thread_count; ++index) {
        tasks[index].state = &workers[index].state;
        tasks[index].position = *position;
        tasks[index].maximum_depth = maximum_depth;
        tasks[index].limits = base_limits == 0
            ? SearchLimits{}
            : *base_limits;
        tasks[index].limits.stop_requested = stop_requested;
        tasks[index].limits.root_move_offset = index == 0
            ? 0
            : index * 11 + 1;
        tasks[index].best_move = &workers[index].best_move;
        tasks[index].variation = &workers[index].variation;
        tasks[index].completed_depth = &workers[index].completed_depth;
        tasks[index].score = &workers[index].score;
        tasks[index].callback = index == 0 ? callback : 0;
        tasks[index].user_data = user_data;
#ifdef _WIN32
        {
            HANDLE handle = CreateThread(
                0,
                0,
                parallel_search_thread_proc,
                &tasks[index],
                0,
                0
            );
            if (handle != 0) {
                search_threads.push_back(handle);
            } else {
                execute_parallel_search_task(&tasks[index]);
            }
        }
#else
        search_threads.emplace_back([&tasks, index]() {
            execute_parallel_search_task(&tasks[index]);
        });
#endif
    }

#ifdef _WIN32
    for (HANDLE search_thread : search_threads) {
        WaitForSingleObject(search_thread, INFINITE);
        CloseHandle(search_thread);
    }
#else
    for (std::thread &search_thread : search_threads) {
        search_thread.join();
    }
#endif

    for (index = 0; index < thread_count; ++index) {
        if (best_index < 0 ||
            workers[index].completed_depth >
                workers[best_index].completed_depth ||
            (workers[index].completed_depth ==
                 workers[best_index].completed_depth &&
             workers[index].score > workers[best_index].score)) {
            best_index = index;
        }
    }

    if (best_index >= 0) {
        if (best_move != 0) {
            *best_move = workers[best_index].best_move;
        }
        if (variation != 0) {
            *variation = workers[best_index].variation;
        }
        if (completed_depth != 0) {
            *completed_depth = workers[best_index].completed_depth;
        }
    }

    index = best_index >= 0 ? workers[best_index].score : 0;
    for (ParallelSearchWorker &worker : workers) {
        if (worker.initialized) {
            free(worker.state.heuristics);
            worker.state.heuristics = 0;
        }
    }
    return index;
}

static void search_from_command(
    SearchSharedState *shared_state,
    Position position,
    const std::vector<uint64_t> &history,
    char *arguments,
    std::atomic<bool> *stop_requested,
    int draw_score,
    int thread_count
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
            int divisor = moves_to_go > 0 ? moves_to_go : 24;
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
    limits.draw_score = draw_score;

    search_parallel(
        shared_state,
        &position,
        depth,
        &limits,
        thread_count,
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
    int draw_score;
    int thread_count;
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
        task->stop_requested,
        task->draw_score,
        task->thread_count
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
    int allow_draws = g_config.allow_draws;
    int threads = g_config.threads;
    int maximum_threads = maximum_uci_threads();
    int use_nnue = 0;
    std::string nnue_path;
    char line[UCI_LINE_SIZE];

    if (threads < 1) {
        threads = 1;
    } else if (threads > maximum_threads) {
        threads = maximum_threads;
    }

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

    while (read_uci_line(line, sizeof(line))) {
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
            printf("option name Threads type spin default %d min 1 max %d\n",
                   threads, maximum_threads);
            printf("option name Hash type spin default %d min 1 max 1024\n",
                   DEFAULT_TRANSPOSITION_TABLE_MB);
            printf("option name Clear Hash type button\n");
            printf("option name Allow Draws type check default %s\n",
                   g_config.allow_draws ? "true" : "false");
            printf("option name NNUEFile type string default\n");
            printf("option name Use NNUE type check default false\n");
            printf("option name Search LMR Depth Start type spin default %d min 2 max 6\n",
                   g_config.search_lmr_depth_start);
            printf("option name Search LMR Move Start type spin default %d min 2 max 8\n",
                   g_config.search_lmr_move_start);
            printf("option name Search Null Move Base type spin default %d min 1 max 8\n",
                   g_config.search_null_move_base);
            printf("option name Search Static Futility Margin type spin default %d min 40 max 200\n",
                   g_config.search_static_futility_margin);
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
        } else if (set_search_option_value(
                arguments,
                "setoption name Threads value ",
                1,
                maximum_threads,
                &threads
            ) || set_search_option_value(
                arguments,
                "setoption name Search LMR Depth Start value ",
                2,
                6,
                &g_config.search_lmr_depth_start
            ) || set_search_option_value(
                arguments,
                "setoption name Search LMR Move Start value ",
                2,
                8,
                &g_config.search_lmr_move_start
            ) || set_search_option_value(
                arguments,
                "setoption name Search Null Move Base value ",
                1,
                8,
                &g_config.search_null_move_base
            ) || set_search_option_value(
                arguments,
                "setoption name Search Static Futility Margin value ",
                40,
                200,
                &g_config.search_static_futility_margin
            )) {
            stop_search();
            clear_search_shared_state(&shared_state);
        } else if (strncmp(
                arguments,
                "setoption name Allow Draws value ",
                33
            ) == 0) {
            int value;

            stop_search();
            if (parse_boolean(arguments + 33, &value)) {
                allow_draws = value;
                clear_search_shared_state(&shared_state);
            }
        } else if (strncmp(
                arguments,
                "setoption name NNUEFile value ",
                30
            ) == 0) {
            const char *path = arguments + 30;

            stop_search();
            if (*path == '\0') {
                nnue_unload();
                nnue_path.clear();
            } else if (nnue_load(path)) {
                nnue_path = path;
                nnue_set_enabled(use_nnue);
            } else {
                fprintf(stderr, "Error: Could not load Theta or Stockfish NNUE file\n");
            }
        } else if (strncmp(
                arguments,
                "setoption name Use NNUE value ",
                30
            ) == 0) {
            int value;

            stop_search();
            if (parse_boolean(arguments + 30, &value)) {
                if (value && !nnue_is_loaded() && !nnue_path.empty()) {
                    if (!nnue_load(nnue_path.c_str())) {
                        fprintf(stderr, "Error: Could not load Theta or Stockfish NNUE file\n");
                        value = 0;
                    }
                }
                use_nnue = value;
                nnue_set_enabled(value);
            }
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
            task->draw_score = allow_draws ? 0 : NO_DRAW_SCORE;
            task->thread_count = threads;
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
            nnue_unload();
            destroy_search_shared_state(&shared_state);
            return 1;
        }
    }

    stop_search();
    destroy_search_shared_state(&shared_state);
    return 1;
}

#define _POSIX_C_SOURCE 199309L
#include <bits/time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void log_err(const char *fmt, ...) {
    fprintf(stderr, "ERROR: ");

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}

int parse_args(int argc, const char *const *argv, const char **path_chunks, const char **path_sizes) {
    const char **target_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (target_path) {
            *target_path = argv[i];
            target_path = NULL;
            continue;
        }

        if (strcmp(argv[i], "-c") == 0) {
            target_path = path_chunks;
        } else if (strcmp(argv[i], "-s") == 0) {
            target_path = path_sizes;
        } else {
            log_err("Unknown option: '%s'", argv[i]);
            return 1;
        }
    }

    if (target_path != NULL) {
        log_err("'%s' must be followed by a path!", argv[argc - 1]);
        return 1;
    }

    return 0;
}

// free list node
typedef struct fl_node {
    int size;
    struct fl_node *next;
} fl_node_t;

// free list
typedef struct fl {
    fl_node_t *tail;
} fl_t;

fl_t fl_init(void) {
    return (fl_t){.tail = NULL};
}

void fl_free(fl_t *fl) {
    if (!fl->tail)
        return;

    fl_node_t *p = fl->tail->next; // initially head

    while (p != fl->tail) {
        fl_node_t *next = p->next;
        free(p);
        p = next;
    }

    free(fl->tail);
    fl->tail = NULL;
}

void fl_push_back(fl_t *fl, int size) {
    fl_node_t *new_node = malloc(sizeof(fl_node_t));
    new_node->size = size;

    if (!fl->tail) {
        new_node->next = new_node;
        fl->tail = new_node;
        return;
    }

    new_node->next = fl->tail->next;
    fl->tail->next = new_node;
    fl->tail = new_node;
}

fl_t fl_cp(const fl_t *fl) {
    fl_t ret = fl_init();

    if (!fl->tail) {
        return ret;
    }

    fl_node_t *p = fl->tail->next;

    while (p != fl->tail) {
        fl_push_back(&ret, p->size);
        p = p->next;
    }

    fl_push_back(&ret, fl->tail->size);

    return ret;
}

void fl_print(fl_t *fl) {
    if (!fl->tail) {
        printf("<empty>\n");
    }

    fl_node_t *p = fl->tail->next;

    while (p != fl->tail) {
        printf("%d, ", p->size);
        p = p->next;
    }

    printf("%d\n", fl->tail->size);
}

// fl must be initialized beforehand
int parse_chunks(fl_t *fl, const char *path_chunks) {
    FILE *fin = fopen(path_chunks, "r");
    if (!fin)
        return 1;

    int cur_chunk;
    int scanf_ret = 1;
    while (!feof(fin) && (scanf_ret = fscanf(fin, "%d ", &cur_chunk)) != 0) {
        fl_push_back(fl, cur_chunk);
    }

    if (scanf_ret == 0 && !feof(fin)) {
        log_err("%s: invalid format!", path_chunks);
        fclose(fin);
        return 1;
    }

    fclose(fin);
    return 0;
}

// if the function fails - sizes is NULL
// it is the caller's responsibility to free the sizes array if the function succeeds
int parse_sizes(int **sizes, int *sizes_size, const char *path_sizes) {
#define SIZES_INITIAL_CAPACITY 8
    int ret = 1;

    FILE *fin = fopen(path_sizes, "r");
    if (!fin)
        return 1;

    int sizes_cap = SIZES_INITIAL_CAPACITY;
    *sizes = malloc(sizes_cap * sizeof(int));
    if (!*sizes) {
        perror("malloc");
        goto out;
    }
    *sizes_size = 0;

    int cur_size;
    int scanf_ret = 1;
    while (!feof(fin) && (scanf_ret = fscanf(fin, "%d ", &cur_size)) != 0) {
        if (*sizes_size == sizes_cap) {
            sizes_cap *= 2;
            int *temp = realloc(*sizes, sizes_cap * sizeof(int));
            if (!temp) {
                perror("realloc");
                goto free;
            }

            *sizes = temp;
        }

        (*sizes)[*sizes_size] = cur_size;
        (*sizes_size)++;
    }

    if (scanf_ret == 0 && !feof(fin)) {
        log_err("%s: invalid format!", path_sizes);
        goto free;
    }

    ret = 0;

free:
    if (ret != 0) {
        free(*sizes);
        *sizes = NULL;
    }

out:
    fclose(fin);

    return ret;
#undef SIZES_INITIAL_CAPACITY
}

// 0 - ok, 1 - allocation failed
typedef int (*alloc_func_t)(fl_t *fl, int size);

typedef struct test_result {
    int total_allocs;
    int failed_allocs;

    int total_bytes;
    int bytes_not_allocated;

    int total_free_memory;
    int largest_free_chunk;
} test_result_t;

typedef struct bench_result {
    double avg_time; // how much it took to run all iterations
    int iterations;
} bench_result_t;

int max_int(int a, int b) {
    return a > b ? a : b;
}

int get_largest_chunk_size(const fl_t *fl) {
    if (!fl->tail) {
        return 0;
    }

    int res = 0;
    fl_node_t *p = fl->tail->next;

    while (p != fl->tail) {
        res = max_int(res, p->size);
        p = p->next;
    }

    res = max_int(res, p->size);

    return res;
}

int get_total_free(const fl_t *fl) {
    if (!fl->tail) {
        return 0;
    }

    int res = 0;
    fl_node_t *p = fl->tail->next;

    while (p != fl->tail) {
        res += p->size;
        p = p->next;
    }

    res += p->size;

    return res;
}

void test_alloc_funcs(const fl_t *fl, const int *sizes, int sizes_size, const alloc_func_t *funcs,
                      test_result_t *results, int func_count) {
    for (int i = 0; i < func_count; i++) {
        alloc_func_t f = funcs[i];
        test_result_t *cur_result = results + i;
        fl_t fl_copy = fl_cp(fl);

        for (int j = 0; j < sizes_size; j++) {
            int cur_size = sizes[i];

            int alloc_ret = f(&fl_copy, cur_size);
            if (!alloc_ret) {
                cur_result->failed_allocs++;
                cur_result->bytes_not_allocated += cur_size;
            }

            cur_result->total_bytes += cur_size;
        }

        cur_result->total_allocs = sizes_size;
        cur_result->total_free_memory = get_total_free(fl);
        cur_result->largest_free_chunk = get_largest_chunk_size(fl);

        fl_free(&fl_copy);
    }
}

void bench_alloc_funcs(const fl_t *fl, const int *sizes, int sizes_size, const alloc_func_t *funcs,
                       bench_result_t *results, int func_count, int iterations) {
    // JIC so that funcion calls are not optimized out
    volatile int sink = 0;

    for (int func_num = 0; func_num < func_count; func_num++) {
        alloc_func_t f = funcs[func_num];
        bench_result_t *cur_result = results + func_num;

        struct timespec start, end;

        clock_gettime(CLOCK_MONOTONIC, &start);
        for (int i = 0; i < iterations; i++) {
            fl_t fl_copy = fl_cp(fl);
            for (int j = 0; j < sizes_size; j++) {
                sink ^= f(&fl_copy, sizes[j]);
            }
            fl_free(&fl_copy);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);

        double time_total = (end.tv_sec - start.tv_sec) - (end.tv_nsec - start.tv_nsec) / 1e9;

        clock_gettime(CLOCK_MONOTONIC, &start);
        for (int i = 0; i < iterations; i++) {
            fl_t fl_copy = fl_cp(fl);
            sink ^= fl_copy.tail ? fl_copy.tail->next->size : 0; // JIC fl is empty
            fl_free(&fl_copy);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);

        double time_overhead = (end.tv_sec - start.tv_sec) - (end.tv_nsec - start.tv_nsec) / 1e9;

        cur_result->avg_time = (time_total - time_overhead) / iterations;
        cur_result->iterations = iterations;
    }
}

void print_results(const char *const *func_names, test_result_t *test_results, bench_result_t *bench_results,
                   int func_count) {
    for (int i = 0; i < func_count; i++) {
        test_result_t *cur_tres = test_results + i;
        bench_result_t *cur_bres = bench_results + i;

        printf("[Algorithm: %s]\n", func_names[i]);
        printf("Total allocations: %d\n", cur_tres->total_allocs);
        printf("Failed allocations: %d\n", cur_tres->failed_allocs);
        printf("Total bytes (tried to allocate): %d\n", cur_tres->total_bytes);
        printf("Bytes not allocated: %d\n", cur_tres->bytes_not_allocated);
        printf("Total free memory (at the end): %d\n", cur_tres->total_free_memory);
        printf("Largest free chunk (at the end): %d\n", cur_tres->largest_free_chunk);
        printf("Fragmented memory: %d\n", cur_tres->total_free_memory - cur_tres->largest_free_chunk);
        printf("Fragmented memory ratio: %f\n",
               (cur_tres->total_free_memory - cur_tres->largest_free_chunk) / (double)cur_tres->total_free_memory);
        printf("Execution time average: %f (%d iterations)\n", cur_bres->avg_time, cur_bres->iterations);
        printf("\n");
    }
}

int alloc_func_first_fit(fl_t *fl, int size) {
    if (!fl->tail) {
        return 1;
    }

    fl_node_t *prev = fl->tail;
    fl_node_t *p = fl->tail->next;

    do {
        if (p->size >= size)
            break;
        prev = p;
        p = p->next;
    } while (p != fl->tail->next);

    if (p->size < size)
        return 1;

    if (p->size == size) {
        if (p == prev) {
            free(fl->tail);
            fl->tail = NULL;
        } else {
            prev->next = p->next;
            free(p);
        }
        return 0;
    }

    p->size -= size;
    return 0;
}

#define ALLOC_FUNC_COUNT 1
const char *func_names[ALLOC_FUNC_COUNT] = {
    "FirstFit",
};

const alloc_func_t alloc_funcs[ALLOC_FUNC_COUNT] = {
    alloc_func_first_fit,
};

test_result_t test_results[ALLOC_FUNC_COUNT];
bench_result_t bench_results[ALLOC_FUNC_COUNT];

#define BENCH_ITERATIONS 100

int main(int argc, const char *const *argv) {
    const char *path_chunks = NULL;
    const char *path_sizes = NULL;

    if (parse_args(argc, argv, &path_chunks, &path_sizes) != 0) {
        log_err("Failed to parse arguments!");
        return 1;
    }

    if (!path_chunks || !path_sizes) {
        log_err("Both -c and -s options must be specified!");
        return 1;
    }

    int ret = 1;
    fl_t fl = fl_init();
    int *sizes = NULL;
    int sizes_size;

    if (parse_chunks(&fl, path_chunks) != 0) {
        log_err("Failed to parse chunks!");
        goto out;
    }

    if (parse_sizes(&sizes, &sizes_size, path_sizes) != 0) {
        log_err("Failed to parse sizes!");
        goto out;
    }

    printf("Chunks:\n");
    fl_print(&fl);
    printf("Sizes:\n");
    for (int i = 0; i < sizes_size; i++) {
        printf("%d%s", sizes[i], (i == sizes_size - 1) ? "\n" : ", ");
    }

    test_alloc_funcs(&fl, sizes, sizes_size, alloc_funcs, test_results, ALLOC_FUNC_COUNT);
    bench_alloc_funcs(&fl, sizes, sizes_size, alloc_funcs, bench_results, ALLOC_FUNC_COUNT, BENCH_ITERATIONS);

    printf("\n");
    print_results(func_names, test_results, bench_results, ALLOC_FUNC_COUNT);

    ret = 0;

out:
    fl_free(&fl);
    if (sizes) {
        free(sizes);
    }

    return ret;
}

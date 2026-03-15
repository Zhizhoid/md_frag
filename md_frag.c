#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    ret = 0;

out:
    fl_free(&fl);
    if (sizes) {
        free(sizes);
    }

    return ret;
}

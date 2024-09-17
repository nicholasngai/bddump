#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libbluray/bluray.h>

#define DEFAULT_AACS_KEYDB_PATH_HOME_SUFFIX "/.config/aacs/KEYDB.cfg"

enum bddump_operation {
    BDDUMP_OP_LIST_TITLES = 1,
    BDDUMP_OP_DUMP = 2,
};

struct bddump_options {
    enum bddump_operation op;
    const char *input_device_path;
    char *aacs_keydb_path;
    bool aacs_keydb_path_is_alloced;
    uint32_t title_index;
    const char *out_path;
};

static void usage(char **argv) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "%s -d <device> [-a AACS <KEYDB.cfg path>] -l\n", argv[0]);
    fprintf(stderr, "%s -d <device> [-a AACS <KEYDB.cfg path>] -t <title> -o <out file>\n", argv[0]);
    fprintf(stderr, "%s -d <device> [-a AACS <KEYDB.cfg path>] -t <title> -o -\n", argv[0]);
}

/* Parses arguments from the command line. */
static int parse_options(int argc, char **argv, struct bddump_options *options) {
    int ret;

    /* Clear options and assign defaults. */
    *options = (struct bddump_options) {
        .op = BDDUMP_OP_DUMP,
        .title_index = UINT32_MAX,
    };

    for (;;) {
        int opt = getopt(argc, argv, "d:a:lt:o:h");
        if (opt == -1) {
            break;
        }

        switch (opt) {
        case 'd':
            options->input_device_path = optarg;
            break;
        case 'l':
            options->op = BDDUMP_OP_LIST_TITLES;
            break;
        case 't': {
            char *endptr;
            unsigned long title_index = strtoul(optarg, &endptr, 10);
            if (*endptr) {
                fprintf(stderr, "Invalid title index: %s\n", optarg);
                ret = -1;
                goto exit;
            }
            if (title_index == ULONG_MAX) {
                perror("strtoul");
                ret = -1;
                goto exit;
            }
            options->title_index = title_index;
            break;
        }
        case 'o':
            options->out_path = optarg;
            break;
        case 'a':
            options->aacs_keydb_path = optarg;
            break;
        case 'h':
        default:
            usage(argv);
            exit(EXIT_FAILURE);
        }
    }

    /* If no aacs_keydb_path specified, allocate a buffer for
     * $HOME/.config/aacs/KEYDB.cfg. */
    if (!options->aacs_keydb_path) {
        /* Get HOME env variable. */
        const char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "HOME variable is unset; cannot infer default AACS KEYDB.cfg path!\n");
            ret = -1;
            goto exit;
        }

        /* Alloc buffer for $HOME/.config/aacs/KEYDB.cfg. */
        char *home_keydb = (char *) malloc(strlen(home) + strlen(DEFAULT_AACS_KEYDB_PATH_HOME_SUFFIX) + 1);
        if (!home_keydb) {
            perror("malloc home_keydb");
            ret = -1;
            goto exit;
        }
        sprintf(home_keydb, "%s" DEFAULT_AACS_KEYDB_PATH_HOME_SUFFIX, home);

        options->aacs_keydb_path = home_keydb;
        options->aacs_keydb_path_is_alloced = true;
    }

    ret = 0;

exit:
    return ret;
}

static void free_options(struct bddump_options *options) {
    if (options->aacs_keydb_path_is_alloced) {
        free(options->aacs_keydb_path);
    }
}

int list_titles(BLURAY *bd) {
    int ret;

    /* Count titles. */
    uint32_t num_titles = bd_get_titles(bd, TITLES_RELEVANT, 0);
    if (num_titles == 0) {
        fprintf(stderr, "No titles found!\n");
        ret = 0;
        goto exit;
    }

    /* List titles. */
    printf("Listing titles:\n");
    for (uint32_t i = 0; i < num_titles; i++) {
        /* Get title info. */
        BLURAY_TITLE_INFO* title_info = bd_get_title_info(bd, i, 0);
        if (!title_info) {
            fprintf(stderr, "bd_get_title_info failed\n");
            ret = -1;
            goto exit;
        }

        printf("index: %" PRIu32 " duration: %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 " chapters: %d\n",
                i,
                (title_info->duration / 90000) / (3600),
                ((title_info->duration / 90000) % 3600) / 60,
                ((title_info->duration / 90000) % 60),
                title_info->chapter_count);

        bd_free_title_info(title_info);
    }

    ret = 0;

exit:
    return ret;
}

int dump_bluray(BLURAY *bd, uint32_t title_index, const char *out_path) {
    int ret;

    /* Count titles and validate num titles. */
    uint32_t num_titles = bd_get_titles(bd, TITLES_RELEVANT, 0);
    if (title_index >= num_titles) {
        fprintf(stderr, "Disc has only %" PRIu32 " titles, but index %" PRIu32 " was specified\n", num_titles, title_index);
        ret = 1;
        goto exit;
    }

    /* Select title. */
    if (bd_select_title(bd, title_index) != 1) {
        fprintf(stderr, "bd_select_title failed\n");
        ret = -1;
        goto exit;
    }

    /* Open file for dumping. */
    FILE *outfile;
    if (strcmp(out_path, "-") == 0) {
        outfile = stdout;
    } else {
        outfile = fopen(out_path, "w");
        if (!outfile) {
            perror("fopen");
            ret = -1;
            goto exit;
        }
    }

    /* Dump. */
    for (;;) {
        /* Read. */
        unsigned char buf[4096];
        int bytes_read = bd_read(bd, buf, sizeof(buf));
        if (bytes_read < 0) {
            fprintf(stderr, "bd_read failed\n");
            ret = -1;
            goto exit_close_outfile;
        }
        if (bytes_read == 0) {
            break;
        }

        /* Write. */
        size_t bytes_written = fwrite(buf, 1, bytes_read, outfile);
        if (bytes_written != (size_t) bytes_read) {
            perror("fwrite");
            ret = -1;
            goto exit_close_outfile;
        }
    }

    ret = 0;

exit_close_outfile:
    if (outfile != stdout) {
        fclose(outfile);
    }
exit:
    return ret;
}

int main(int argc, char **argv) {
    int ret;

    /* Parse args. */
    struct bddump_options options;
    if (parse_options(argc, argv, &options)) {
        ret = EXIT_FAILURE;
        goto exit;
    }
    if (!options.input_device_path) {
        fprintf(stderr, "Error: input device is required\n\n");
        usage(argv);
        ret = EXIT_FAILURE;
        goto exit_free_options;
    }

    /* Open BD disc. */
    BLURAY *bd = bd_open(options.input_device_path, options.aacs_keydb_path);
    if (!bd) {
        fprintf(stderr, "bd_open failed\n");
        ret = EXIT_FAILURE;
        goto exit_free_options;
    }

    switch (options.op) {
    case BDDUMP_OP_LIST_TITLES:
        if (list_titles(bd)) {
            ret = EXIT_FAILURE;
            goto exit_close_bd;
        }
        break;

    case BDDUMP_OP_DUMP:
        /* Validate inputs. */
        if (options.title_index == UINT32_MAX) {
            fprintf(stderr, "Error: No title index specified\n\n");
            usage(argv);
            ret = EXIT_FAILURE;
            goto exit_close_bd;
        }
        if (!options.out_path) {
            fprintf(stderr, "Error: No output file specified\n\n");
            usage(argv);
            ret = EXIT_FAILURE;
            goto exit_close_bd;
        }

        if (dump_bluray(bd, options.title_index, options.out_path)) {
            ret = EXIT_FAILURE;
            goto exit_close_bd;
        }

        break;

    default:
        fprintf(stderr, "Internal error: Unhandled operation\n");
        ret = EXIT_FAILURE;
        goto exit_close_bd;
    }

    ret = 0;

exit_close_bd:
    bd_close(bd);
exit_free_options:
    free_options(&options);
exit:
    return ret;
}

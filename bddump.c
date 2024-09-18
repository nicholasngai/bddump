#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libbluray/bluray.h>

#define DEFAULT_AACS_KEYDB_PATH_HOME_SUFFIX "/.config/aacs/KEYDB.cfg"

#define REMUXING_BUF_SIZE 4096

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

static int bddump_read(void *bd_, unsigned char *buf, int count) {
    BLURAY *bd = (BLURAY *) bd_;

    int bytes_read = bd_read((BLURAY *) bd, buf, count);
    if (bytes_read < 0) {
        fprintf(stderr, "bd_read failed\n");
        return AVERROR_EXTERNAL;
    }
    if (bytes_read == 0) {
        return AVERROR_EOF;
    }
    return bytes_read;
}

static int64_t bddump_seek(void *bd_, int64_t pos, int whence) {
    BLURAY *bd = (BLURAY *) bd_;

    if (whence != SEEK_SET) {
        fprintf(stderr, "bddump_seek called with unsupported whence %d\n", whence);
        return AVERROR_EXTERNAL;
    }
    int64_t new_pos = bd_seek(bd, (uint64_t) pos);
    if (new_pos < 0) {
        fprintf(stderr, "bd_seek failed\n");
        return AVERROR_EXTERNAL;
    }

    return new_pos;
}

static int fd_write(void *fd_, unsigned char *buf, int count) {
    int *fd = (int *) fd_;
    int bytes_written = write(*fd, buf, count);
    if (bytes_written < 0) {
        perror("write");
        return AVERROR_EXTERNAL;
    }
    return bytes_written;
}

/* Largely borrowed from
 * https://ffmpeg.org/doxygen/4.0/remuxing_8c-example.html. */
static int remux(AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx) {
    int ret;

    av_dump_format(ifmt_ctx, 0, NULL, 0);

    int stream_mapping_size = ifmt_ctx->nb_streams;
    int *stream_mapping = (int *) av_mallocz(stream_mapping_size * sizeof(*stream_mapping));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    int stream_index = 0;
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }
        stream_mapping[i] = stream_index++;
        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy codec parameters\n");
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
    }
    av_dump_format(ofmt_ctx, 0, NULL, 1);

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    AVPacket pkt;
    while (1) {
        AVStream *in_stream, *out_stream;
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;
        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }
        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_packet_unref(&pkt);
    }
    av_write_trailer(ofmt_ctx);
end:

    av_free(stream_mapping);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}

static int dump_bluray(BLURAY *bd, uint32_t title_index, const char *out_path) {
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

    /* Get title info and clip info. */
    BLURAY_TITLE_INFO *title_info = bd_get_title_info(bd, title_index, 0);
    if (!title_info) {
        fprintf(stderr, "bd_get_title_info failed\n");
        ret = -1;
        goto exit;
    }
    if (title_info->clip_count != 1) {
        fprintf(stderr, "Title %" PRIu32 " has %" PRIu32 " clips; only single-clip titles are supported at this time\n", title_index, title_info->clip_count);
        ret = -1;
        goto exit_free_title_info;
    }

    /* Allocate input I/O context attached to libbluray for remuxing. */
    unsigned char *input_buf = (unsigned char *) av_malloc(REMUXING_BUF_SIZE);
    if (!input_buf) {
        fprintf(stderr, "av_malloc failed\n");
        ret = -1;
        goto exit_free_title_info;
    }
    AVIOContext *input_io_ctx = avio_alloc_context(input_buf, REMUXING_BUF_SIZE, 0, bd, bddump_read, NULL, bddump_seek);
    if (!input_io_ctx) {
        fprintf(stderr, "avio_alloc_context failed\n");
        ret = -1;
        goto exit_free_avio_input_buf;
    }
    input_buf = NULL;
    AVFormatContext *input_ctx = avformat_alloc_context();
    if (!input_ctx) {
        fprintf(stderr, "avformat_alloc_context failed\n");
        ret = -1;
        goto exit_free_input_io_ctx;
    }
    input_ctx->pb = input_io_ctx;
    if ((ret = avformat_open_input(&input_ctx, "", NULL, NULL)) < 0) {
        fprintf(stderr, "avformat_open_input: %s\n", av_err2str(ret));
        goto exit_close_input_ctx;
    }
    if ((ret = avformat_find_stream_info(input_ctx, NULL)) < 0) {
        fprintf(stderr, "avformat_find_stream_info: %s\n", av_err2str(ret));
        goto exit_close_input_ctx;
    }

    /* Open file for dumping. */
    int out_fd;
    if (strcmp(out_path, "-") == 0) {
        out_fd = STDOUT_FILENO;
    } else {
        out_fd = open(out_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (out_fd < 0) {
            perror("open");
            ret = -1;
            goto exit_close_input_ctx;
        }
    }

    /* Allocate output remuxing context. */
    unsigned char *output_buf = (unsigned char *) av_malloc(REMUXING_BUF_SIZE);
    if (!output_buf) {
        fprintf(stderr, "av_malloc failed\n");
        ret = -1;
        goto exit_close_out_fd;
    }
    AVIOContext *output_io_ctx = avio_alloc_context(output_buf, REMUXING_BUF_SIZE, 1, &out_fd, NULL, fd_write, NULL);
    if (!output_io_ctx) {
        fprintf(stderr, "avio_alloc_context failed\n");
        goto exit_free_output_buf;
    }
    output_buf = NULL;
    AVFormatContext *output_ctx;
    if ((ret = avformat_alloc_output_context2(&output_ctx, NULL, "matroska", NULL)) < 0) {
        fprintf(stderr, "avformat_alloc_output_context2: %s\n", av_err2str(ret));
        goto exit_free_output_io_ctx;
    }
    output_ctx->pb = output_io_ctx;

    /* Remux. */
    if ((ret = remux(input_ctx, output_ctx))) {
        goto exit_free_output_ctx;
    }

exit_free_output_ctx:
    avformat_free_context(output_ctx);
exit_free_output_io_ctx:
    av_free(output_io_ctx->buffer);
    avio_context_free(&output_io_ctx);
exit_free_output_buf:
    if (output_buf) {
        av_free(output_buf);
    }
exit_close_out_fd:
    if (out_fd != STDOUT_FILENO) {
        close(out_fd);
    }
exit_close_input_ctx:
    avformat_close_input(&input_ctx);
exit_free_input_io_ctx:
    av_free(input_io_ctx->buffer);
    avio_context_free(&input_io_ctx);
exit_free_avio_input_buf:
    if (input_buf) {
        av_free(input_buf);
    }
exit_free_title_info:
    bd_free_title_info(title_info);
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

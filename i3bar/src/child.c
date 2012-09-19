/*
 * vim:ts=4:sw=4:expandtab
 *
 * i3bar - an xcb-based status- and ws-bar for i3
 * © 2010-2012 Axel Wagner and contributors (see also: LICENSE)
 *
 * child.c: Getting Input for the statusline
 *
 */
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <ev.h>
#include <yajl/yajl_common.h>
#include <yajl/yajl_parse.h>
#include <yajl/yajl_version.h>

#include "common.h"

/* Global variables for child_*() */
pid_t child_pid;

/* stdin- and sigchild-watchers */
ev_io    *stdin_io;
ev_child *child_sig;

/* JSON parser for stdin */
bool first_line = true;
bool plaintext = false;
yajl_callbacks callbacks;
yajl_handle parser;

typedef struct parser_ctx {
    /* A copy of the last JSON map key. */
    char *last_map_key;

    /* The current block. Will be filled, then copied and put into the list of
     * blocks. */
    struct status_block block;
} parser_ctx;

parser_ctx parser_context;

/* The buffer statusline points to */
struct statusline_head statusline_head = TAILQ_HEAD_INITIALIZER(statusline_head);
char *statusline_buffer = NULL;

/*
 * Stop and free() the stdin- and sigchild-watchers
 *
 */
void cleanup(void) {
    if (stdin_io != NULL) {
        ev_io_stop(main_loop, stdin_io);
        FREE(stdin_io);
        FREE(statusline_buffer);
        /* statusline pointed to memory within statusline_buffer */
        statusline = NULL;
    }

    if (child_sig != NULL) {
        ev_child_stop(main_loop, child_sig);
        FREE(child_sig);
    }
}

/*
 * The start of a new array is the start of a new status line, so we clear all
 * previous entries.
 *
 */
static int stdin_start_array(void *context) {
    struct status_block *first;
    while (!TAILQ_EMPTY(&statusline_head)) {
        first = TAILQ_FIRST(&statusline_head);
        I3STRING_FREE(first->full_text);
        FREE(first->color);
        TAILQ_REMOVE(&statusline_head, first, blocks);
        free(first);
    }
    return 1;
}

/*
 * The start of a map is the start of a single block of the status line.
 *
 */
static int stdin_start_map(void *context) {
    parser_ctx *ctx = context;
    memset(&(ctx->block), '\0', sizeof(struct status_block));
    return 1;
}

#if YAJL_MAJOR >= 2
static int stdin_map_key(void *context, const unsigned char *key, size_t len) {
#else
static int stdin_map_key(void *context, const unsigned char *key, unsigned int len) {
#endif
    parser_ctx *ctx = context;
    FREE(ctx->last_map_key);
    sasprintf(&(ctx->last_map_key), "%.*s", len, key);
    return 1;
}

#if YAJL_MAJOR >= 2
static int stdin_string(void *context, const unsigned char *val, size_t len) {
#else
static int stdin_string(void *context, const unsigned char *val, unsigned int len) {
#endif
    parser_ctx *ctx = context;
    if (strcasecmp(ctx->last_map_key, "full_text") == 0) {
        ctx->block.full_text = i3string_from_utf8_with_length((const char *)val, len);
    }
    if (strcasecmp(ctx->last_map_key, "color") == 0) {
        sasprintf(&(ctx->block.color), "%.*s", len, val);
    }
    return 1;
}

static int stdin_end_map(void *context) {
    parser_ctx *ctx = context;
    struct status_block *new_block = smalloc(sizeof(struct status_block));
    memcpy(new_block, &(ctx->block), sizeof(struct status_block));
    /* Ensure we have a full_text set, so that when it is missing (or null),
     * i3bar doesn’t crash and the user gets an annoying message. */
    if (!new_block->full_text)
        new_block->full_text = i3string_from_utf8("SPEC VIOLATION (null)");
    TAILQ_INSERT_TAIL(&statusline_head, new_block, blocks);
    return 1;
}

static int stdin_end_array(void *context) {
    DLOG("dumping statusline:\n");
    struct status_block *current;
    TAILQ_FOREACH(current, &statusline_head, blocks) {
        DLOG("full_text = %s\n", i3string_as_utf8(current->full_text));
        DLOG("color = %s\n", current->color);
    }
    DLOG("end of dump\n");
    return 1;
}

/*
 * Callbalk for stdin. We read a line from stdin and store the result
 * in statusline
 *
 */
void stdin_io_cb(struct ev_loop *loop, ev_io *watcher, int revents) {
    int fd = watcher->fd;
    int n = 0;
    int rec = 0;
    int buffer_len = STDIN_CHUNK_SIZE;
    unsigned char *buffer = smalloc(buffer_len+1);
    buffer[0] = '\0';
    while(1) {
        n = read(fd, buffer + rec, buffer_len - rec);
        if (n == -1) {
            if (errno == EAGAIN) {
                /* finish up */
                break;
            }
            ELOG("read() failed!: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (n == 0) {
            /* end of file, kill the watcher */
            ELOG("stdin: received EOF\n");
            cleanup();
            draw_bars();
            return;
        }
        rec += n;

        if (rec == buffer_len) {
            buffer_len += STDIN_CHUNK_SIZE;
            buffer = srealloc(buffer, buffer_len);
        }
    }
    if (*buffer == '\0') {
        FREE(buffer);
        return;
    }

    unsigned char *json_input = buffer;
    if (first_line) {
        DLOG("Detecting input type based on buffer *%.*s*\n", rec, buffer);
        /* Detect whether this is JSON or plain text. */
        unsigned int consumed = 0;
        /* At the moment, we don’t care for the version. This might change
         * in the future, but for now, we just discard it. */
        plaintext = (determine_json_version(buffer, buffer_len, &consumed) == -1);
        if (plaintext) {
            /* In case of plaintext, we just add a single block and change its
             * full_text pointer later. */
            struct status_block *new_block = scalloc(sizeof(struct status_block));
            TAILQ_INSERT_TAIL(&statusline_head, new_block, blocks);
        } else {
            json_input += consumed;
            rec -= consumed;
        }
        first_line = false;
    }
    if (!plaintext) {
        yajl_status status = yajl_parse(parser, json_input, rec);
#if YAJL_MAJOR >= 2
        if (status != yajl_status_ok) {
#else
        if (status != yajl_status_ok && status != yajl_status_insufficient_data) {
#endif
            fprintf(stderr, "[i3bar] Could not parse JSON input (code %d): %.*s\n",
                    status, rec, json_input);
        }
    } else {
        struct status_block *first = TAILQ_FIRST(&statusline_head);
        /* Clear the old buffer if any. */
        I3STRING_FREE(first->full_text);
        /* Remove the trailing newline and terminate the string at the same
         * time. */
        if (buffer[rec-1] == '\n' || buffer[rec-1] == '\r')
            buffer[rec-1] = '\0';
        else buffer[rec] = '\0';
        first->full_text = i3string_from_utf8((const char *)buffer);
    }
    free(buffer);
    draw_bars();
}

/*
 * We received a sigchild, meaning, that the child-process terminated.
 * We simply free the respective data-structures and don't care for input
 * anymore
 *
 */
void child_sig_cb(struct ev_loop *loop, ev_child *watcher, int revents) {
    ELOG("Child (pid: %d) unexpectedly exited with status %d\n",
           child_pid,
           watcher->rstatus);
    cleanup();
}

/*
 * Start a child-process with the specified command and reroute stdin.
 * We actually start a $SHELL to execute the command so we don't have to care
 * about arguments and such
 *
 */
void start_child(char *command) {
    /* Allocate a yajl parser which will be used to parse stdin. */
    memset(&callbacks, '\0', sizeof(yajl_callbacks));
    callbacks.yajl_map_key = stdin_map_key;
    callbacks.yajl_string = stdin_string;
    callbacks.yajl_start_array = stdin_start_array;
    callbacks.yajl_end_array = stdin_end_array;
    callbacks.yajl_start_map = stdin_start_map;
    callbacks.yajl_end_map = stdin_end_map;
#if YAJL_MAJOR < 2
    yajl_parser_config parse_conf = { 0, 0 };

    parser = yajl_alloc(&callbacks, &parse_conf, NULL, (void*)&parser_context);
#else
    parser = yajl_alloc(&callbacks, NULL, &parser_context);
#endif

    child_pid = 0;
    if (command != NULL) {
        int fd[2];
        if (pipe(fd) == -1)
            err(EXIT_FAILURE, "pipe(fd)");

        child_pid = fork();
        switch (child_pid) {
            case -1:
                ELOG("Couldn't fork(): %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            case 0:
                /* Child-process. Reroute stdout and start shell */
                close(fd[0]);

                dup2(fd[1], STDOUT_FILENO);

                static const char *shell = NULL;

                if ((shell = getenv("SHELL")) == NULL)
                    shell = "/bin/sh";

                execl(shell, shell, "-c", command, (char*) NULL);
                return;
            default:
                /* Parent-process. Rerout stdin */
                close(fd[1]);

                dup2(fd[0], STDIN_FILENO);

                /* If hide-on-modifier is set, we start of by sending the
                 * child a SIGSTOP, because the bars aren't mapped at start */
                if (config.hide_on_modifier) {
                    stop_child();
                }

                break;
        }
    }

    /* We set O_NONBLOCK because blocking is evil in event-driven software */
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    stdin_io = smalloc(sizeof(ev_io));
    ev_io_init(stdin_io, &stdin_io_cb, STDIN_FILENO, EV_READ);
    ev_io_start(main_loop, stdin_io);

    /* We must cleanup, if the child unexpectedly terminates */
    child_sig = smalloc(sizeof(ev_child));
    ev_child_init(child_sig, &child_sig_cb, child_pid, 0);
    ev_child_start(main_loop, child_sig);

    atexit(kill_child_at_exit);
}

/*
 * kill()s the child-process (if any). Called when exit()ing.
 *
 */
void kill_child_at_exit(void) {
    if (child_pid != 0) {
        kill(child_pid, SIGCONT);
        kill(child_pid, SIGTERM);
    }
}

/*
 * kill()s the child-process (if existent) and closes and
 * free()s the stdin- and sigchild-watchers
 *
 */
void kill_child(void) {
    if (child_pid != 0) {
        kill(child_pid, SIGCONT);
        kill(child_pid, SIGTERM);
        int status;
        waitpid(child_pid, &status, 0);
        child_pid = 0;
        cleanup();
    }
}

/*
 * Sends a SIGSTOP to the child-process (if existent)
 *
 */
void stop_child(void) {
    if (child_pid != 0) {
        kill(child_pid, SIGSTOP);
    }
}

/*
 * Sends a SIGCONT to the child-process (if existent)
 *
 */
void cont_child(void) {
    if (child_pid != 0) {
        kill(child_pid, SIGCONT);
    }
}

/*
 * vim:ts=4:sw=4:expandtab
 *
 * i3 - an improved dynamic tiling window manager
 * © 2009-2011 Michael Stapelberg and contributors (see also: LICENSE)
 *
 * main.c: Initialization, main loop
 *
 */
#include <ev.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "all.h"

#include "sd-daemon.h"

/* The original value of RLIMIT_CORE when i3 was started. We need to restore
 * this before starting any other process, since we set RLIMIT_CORE to
 * RLIM_INFINITY for i3 debugging versions. */
struct rlimit original_rlimit_core;

static int xkb_event_base;

int xkb_current_group;

extern Con *focused;

char **start_argv;

xcb_connection_t *conn;
/* The screen (0 when you are using DISPLAY=:0) of the connection 'conn' */
int conn_screen;

/* Display handle for libstartup-notification */
SnDisplay *sndisplay;

/* The last timestamp we got from X11 (timestamps are included in some events
 * and are used for some things, like determining a unique ID in startup
 * notification). */
xcb_timestamp_t last_timestamp = XCB_CURRENT_TIME;

xcb_screen_t *root_screen;
xcb_window_t root;
uint8_t root_depth;

struct ev_loop *main_loop;

xcb_key_symbols_t *keysyms;

/* Those are our connections to X11 for use with libXcursor and XKB */
Display *xlibdpy, *xkbdpy;

/* The list of key bindings */
struct bindings_head *bindings;

/* The list of exec-lines */
struct autostarts_head autostarts = TAILQ_HEAD_INITIALIZER(autostarts);

/* The list of exec_always lines */
struct autostarts_always_head autostarts_always = TAILQ_HEAD_INITIALIZER(autostarts_always);

/* The list of assignments */
struct assignments_head assignments = TAILQ_HEAD_INITIALIZER(assignments);

/* The list of workspace assignments (which workspace should end up on which
 * output) */
struct ws_assignments_head ws_assignments = TAILQ_HEAD_INITIALIZER(ws_assignments);

/* We hope that those are supported and set them to true */
bool xcursor_supported = true;
bool xkb_supported = true;

/* This will be set to true when -C is used so that functions can behave
 * slightly differently. We don’t want i3-nagbar to be started when validating
 * the config, for example. */
bool only_check_config = false;

/*
 * This callback is only a dummy, see xcb_prepare_cb and xcb_check_cb.
 * See also man libev(3): "ev_prepare" and "ev_check" - customise your event loop
 *
 */
static void xcb_got_event(EV_P_ struct ev_io *w, int revents) {
    /* empty, because xcb_prepare_cb and xcb_check_cb are used */
}

/*
 * Flush before blocking (and waiting for new events)
 *
 */
static void xcb_prepare_cb(EV_P_ ev_prepare *w, int revents) {
    xcb_flush(conn);
}

/*
 * Instead of polling the X connection socket we leave this to
 * xcb_poll_for_event() which knows better than we can ever know.
 *
 */
static void xcb_check_cb(EV_P_ ev_check *w, int revents) {
    xcb_generic_event_t *event;

    while ((event = xcb_poll_for_event(conn)) != NULL) {
        if (event->response_type == 0) {
            if (event_is_ignored(event->sequence, 0))
                DLOG("Expected X11 Error received for sequence %x\n", event->sequence);
            else {
                xcb_generic_error_t *error = (xcb_generic_error_t*)event;
                ELOG("X11 Error received! sequence 0x%x, error_code = %d\n",
                     error->sequence, error->error_code);
            }
            free(event);
            continue;
        }

        /* Strip off the highest bit (set if the event is generated) */
        int type = (event->response_type & 0x7F);

        handle_event(type, event);

        free(event);
    }
}


/*
 * When using xmodmap to change the keyboard mapping, this event
 * is only sent via XKB. Therefore, we need this special handler.
 *
 */
static void xkb_got_event(EV_P_ struct ev_io *w, int revents) {
    DLOG("Handling XKB event\n");
    XkbEvent ev;

    /* When using xmodmap, every change (!) gets an own event.
     * Therefore, we just read all events and only handle the
     * mapping_notify once. */
    bool mapping_changed = false;
    while (XPending(xkbdpy)) {
        XNextEvent(xkbdpy, (XEvent*)&ev);
        /* While we should never receive a non-XKB event,
         * better do sanity checking */
        if (ev.type != xkb_event_base)
            continue;

        if (ev.any.xkb_type == XkbMapNotify) {
            mapping_changed = true;
            continue;
        }

        if (ev.any.xkb_type != XkbStateNotify) {
            ELOG("Unknown XKB event received (type %d)\n", ev.any.xkb_type);
            continue;
        }

        /* See The XKB Extension: Library Specification, section 14.1 */
        /* We check if the current group (each group contains
         * two levels) has been changed. Mode_switch activates
         * group XkbGroup2Index */
        if (xkb_current_group == ev.state.group)
            continue;

        xkb_current_group = ev.state.group;

        if (ev.state.group == XkbGroup2Index) {
            DLOG("Mode_switch enabled\n");
            grab_all_keys(conn, true);
        }

        if (ev.state.group == XkbGroup1Index) {
            DLOG("Mode_switch disabled\n");
            ungrab_all_keys(conn);
            grab_all_keys(conn, false);
        }
    }

    if (!mapping_changed)
        return;

    DLOG("Keyboard mapping changed, updating keybindings\n");
    xcb_key_symbols_free(keysyms);
    keysyms = xcb_key_symbols_alloc(conn);

    xcb_numlock_mask = aio_get_mod_mask_for(XCB_NUM_LOCK, keysyms);

    ungrab_all_keys(conn);
    DLOG("Re-grabbing...\n");
    translate_keysyms();
    grab_all_keys(conn, (xkb_current_group == XkbGroup2Index));
    DLOG("Done\n");
}

/*
 * Exit handler which destroys the main_loop. Will trigger cleanup handlers.
 *
 */
static void i3_exit() {
/* We need ev >= 4 for the following code. Since it is not *that* important (it
 * only makes sure that there are no i3-nagbar instances left behind) we still
 * support old systems with libev 3. */
#if EV_VERSION_MAJOR >= 4
    ev_loop_destroy(main_loop);
#endif
}

int main(int argc, char *argv[]) {
    char *override_configpath = NULL;
    bool autostart = true;
    char *layout_path = NULL;
    bool delete_layout_path = false;
    bool force_xinerama = false;
    bool disable_signalhandler = false;
    static struct option long_options[] = {
        {"no-autostart", no_argument, 0, 'a'},
        {"config", required_argument, 0, 'c'},
        {"version", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {"layout", required_argument, 0, 'L'},
        {"restart", required_argument, 0, 0},
        {"force-xinerama", no_argument, 0, 0},
        {"force_xinerama", no_argument, 0, 0},
        {"disable-signalhandler", no_argument, 0, 0},
        {"get-socketpath", no_argument, 0, 0},
        {"get_socketpath", no_argument, 0, 0},
        {0, 0, 0, 0}
    };
    int option_index = 0, opt;

    setlocale(LC_ALL, "");

    /* Get the RLIMIT_CORE limit at startup time to restore this before
     * starting processes. */
    getrlimit(RLIMIT_CORE, &original_rlimit_core);

    /* Disable output buffering to make redirects in .xsession actually useful for debugging */
    if (!isatty(fileno(stdout)))
        setbuf(stdout, NULL);

    srand(time(NULL));

    init_logging();

    start_argv = argv;

    while ((opt = getopt_long(argc, argv, "c:CvaL:hld:V", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'a':
                LOG("Autostart disabled using -a\n");
                autostart = false;
                break;
            case 'L':
                FREE(layout_path);
                layout_path = sstrdup(optarg);
                delete_layout_path = false;
                break;
            case 'c':
                FREE(override_configpath);
                override_configpath = sstrdup(optarg);
                break;
            case 'C':
                LOG("Checking configuration file only (-C)\n");
                only_check_config = true;
                break;
            case 'v':
                printf("i3 version " I3_VERSION " © 2009-2011 Michael Stapelberg and contributors\n");
                exit(EXIT_SUCCESS);
            case 'V':
                set_verbosity(true);
                break;
            case 'd':
                LOG("Enabling debug loglevel %s\n", optarg);
                add_loglevel(optarg);
                break;
            case 'l':
                /* DEPRECATED, ignored for the next 3 versions (3.e, 3.f, 3.g) */
                break;
            case 0:
                if (strcmp(long_options[option_index].name, "force-xinerama") == 0 ||
                    strcmp(long_options[option_index].name, "force_xinerama") == 0) {
                    force_xinerama = true;
                    ELOG("Using Xinerama instead of RandR. This option should be "
                         "avoided at all cost because it does not refresh the list "
                         "of screens, so you cannot configure displays at runtime. "
                         "Please check if your driver really does not support RandR "
                         "and disable this option as soon as you can.\n");
                    break;
                } else if (strcmp(long_options[option_index].name, "disable-signalhandler") == 0) {
                    disable_signalhandler = true;
                    break;
                } else if (strcmp(long_options[option_index].name, "get-socketpath") == 0 ||
                           strcmp(long_options[option_index].name, "get_socketpath") == 0) {
                    char *socket_path = socket_path_from_x11();
                    if (socket_path) {
                        printf("%s\n", socket_path);
                        return 0;
                    }

                    return 1;
                } else if (strcmp(long_options[option_index].name, "restart") == 0) {
                    FREE(layout_path);
                    layout_path = sstrdup(optarg);
                    delete_layout_path = true;
                    break;
                }
                /* fall-through */
            default:
                fprintf(stderr, "Usage: %s [-c configfile] [-d loglevel] [-a] [-v] [-V] [-C]\n", argv[0]);
                fprintf(stderr, "\n");
                fprintf(stderr, "\t-a          disable autostart ('exec' lines in config)\n");
                fprintf(stderr, "\t-c <file>   use the provided configfile instead\n");
                fprintf(stderr, "\t-C          validate configuration file and exit\n");
                fprintf(stderr, "\t-d <level>  enable debug output with the specified loglevel\n");
                fprintf(stderr, "\t-L <file>   path to the serialized layout during restarts\n");
                fprintf(stderr, "\t-v          display version and exit\n");
                fprintf(stderr, "\t-V          enable verbose mode\n");
                fprintf(stderr, "\n");
                fprintf(stderr, "\t--force-xinerama\n"
                                "\tUse Xinerama instead of RandR.\n"
                                "\tThis option should only be used if you are stuck with the\n"
                                "\tnvidia closed source driver which does not support RandR.\n");
                fprintf(stderr, "\n");
                fprintf(stderr, "\t--get-socketpath\n"
                                "\tRetrieve the i3 IPC socket path from X11, print it, then exit.\n");
                fprintf(stderr, "\n");
                fprintf(stderr, "If you pass plain text arguments, i3 will interpret them as a command\n"
                                "to send to a currently running i3 (like i3-msg). This allows you to\n"
                                "use nice and logical commands, such as:\n"
                                "\n"
                                "\ti3 border none\n"
                                "\ti3 floating toggle\n"
                                "\ti3 kill window\n"
                                "\n");
                exit(EXIT_FAILURE);
        }
    }

    /* If the user passes more arguments, we act like i3-msg would: Just send
     * the arguments as an IPC message to i3. This allows for nice semantic
     * commands such as 'i3 border none'. */
    if (optind < argc) {
        /* We enable verbose mode so that the user knows what’s going on.
         * This should make it easier to find mistakes when the user passes
         * arguments by mistake. */
        set_verbosity(true);

        LOG("Additional arguments passed. Sending them as a command to i3.\n");
        char *payload = NULL;
        while (optind < argc) {
            if (!payload) {
                payload = sstrdup(argv[optind]);
            } else {
                char *both;
                sasprintf(&both, "%s %s", payload, argv[optind]);
                free(payload);
                payload = both;
            }
            optind++;
        }
        LOG("Command is: %s (%d bytes)\n", payload, strlen(payload));
        char *socket_path = socket_path_from_x11();
        if (!socket_path) {
            ELOG("Could not get i3 IPC socket path\n");
            return 1;
        }

        int sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
        if (sockfd == -1)
            err(EXIT_FAILURE, "Could not create socket");

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(struct sockaddr_un));
        addr.sun_family = AF_LOCAL;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        if (connect(sockfd, (const struct sockaddr*)&addr, sizeof(struct sockaddr_un)) < 0)
            err(EXIT_FAILURE, "Could not connect to i3");

        if (ipc_send_message(sockfd, strlen(payload), I3_IPC_MESSAGE_TYPE_COMMAND,
                             (uint8_t*)payload) == -1)
            err(EXIT_FAILURE, "IPC: write()");

        uint32_t reply_length;
        uint8_t *reply;
        int ret;
        if ((ret = ipc_recv_message(sockfd, I3_IPC_MESSAGE_TYPE_COMMAND,
                                    &reply_length, &reply)) != 0) {
            if (ret == -1)
                err(EXIT_FAILURE, "IPC: read()");
            return 1;
        }
        printf("%.*s\n", reply_length, reply);
        return 0;
    }

    /* I3_VERSION contains either something like this:
     *     "4.0.2 (2011-11-11, branch "release")".
     * or: "4.0.2-123-gCOFFEEBABE (2011-11-11, branch "next")".
     *
     * So we check for the offset of the first opening round bracket to
     * determine whether this is a git version or a release version. */
    if ((strchr(I3_VERSION, '(') - I3_VERSION) > 10) {
        struct rlimit limit = { RLIM_INFINITY, RLIM_INFINITY };
        setrlimit(RLIMIT_CORE, &limit);

        /* The following code is helpful, but not required. We thus don’t pay
         * much attention to error handling, non-linux or other edge cases. */
        char cwd[PATH_MAX];
        LOG("CORE DUMPS: You are running a development version of i3, so coredumps were automatically enabled (ulimit -c unlimited).\n");
        if (getcwd(cwd, sizeof(cwd)) != NULL)
            LOG("CORE DUMPS: Your current working directory is \"%s\".\n", cwd);
        int patternfd;
        if ((patternfd = open("/proc/sys/kernel/core_pattern", O_RDONLY)) >= 0) {
            memset(cwd, '\0', sizeof(cwd));
            if (read(patternfd, cwd, sizeof(cwd)) > 0)
                /* a trailing newline is included in cwd */
                LOG("CORE DUMPS: Your core_pattern is: %s", cwd);
            close(patternfd);
        }
    }

    LOG("i3 (tree) version " I3_VERSION " starting\n");

    conn = xcb_connect(NULL, &conn_screen);
    if (xcb_connection_has_error(conn))
        errx(EXIT_FAILURE, "Cannot open display\n");

    sndisplay = sn_xcb_display_new(conn, NULL, NULL);

    /* Initialize the libev event loop. This needs to be done before loading
     * the config file because the parser will install an ev_child watcher
     * for the nagbar when config errors are found. */
    main_loop = EV_DEFAULT;
    if (main_loop == NULL)
            die("Could not initialize libev. Bad LIBEV_FLAGS?\n");

    root_screen = xcb_aux_get_screen(conn, conn_screen);
    root = root_screen->root;
    root_depth = root_screen->root_depth;
    xcb_get_geometry_cookie_t gcookie = xcb_get_geometry(conn, root);
    xcb_query_pointer_cookie_t pointercookie = xcb_query_pointer(conn, root);

    load_configuration(conn, override_configpath, false);
    if (only_check_config) {
        LOG("Done checking configuration file. Exiting.\n");
        exit(0);
    }

    if (config.ipc_socket_path == NULL) {
        /* Fall back to a file name in /tmp/ based on the PID */
        if ((config.ipc_socket_path = getenv("I3SOCK")) == NULL)
            config.ipc_socket_path = get_process_filename("ipc-socket");
        else
            config.ipc_socket_path = sstrdup(config.ipc_socket_path);
    }

    uint32_t mask = XCB_CW_EVENT_MASK;
    uint32_t values[] = { XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                          XCB_EVENT_MASK_STRUCTURE_NOTIFY |         /* when the user adds a screen (e.g. video
                                                                           projector), the root window gets a
                                                                           ConfigureNotify */
                          XCB_EVENT_MASK_POINTER_MOTION |
                          XCB_EVENT_MASK_PROPERTY_CHANGE |
                          XCB_EVENT_MASK_ENTER_WINDOW };
    xcb_void_cookie_t cookie;
    cookie = xcb_change_window_attributes_checked(conn, root, mask, values);
    check_error(conn, cookie, "Another window manager seems to be running");

    xcb_get_geometry_reply_t *greply = xcb_get_geometry_reply(conn, gcookie, NULL);
    if (greply == NULL) {
        ELOG("Could not get geometry of the root window, exiting\n");
        return 1;
    }
    DLOG("root geometry reply: (%d, %d) %d x %d\n", greply->x, greply->y, greply->width, greply->height);

    /* Place requests for the atoms we need as soon as possible */
    #define xmacro(atom) \
        xcb_intern_atom_cookie_t atom ## _cookie = xcb_intern_atom(conn, 0, strlen(#atom), #atom);
    #include "atoms.xmacro"
    #undef xmacro

    /* Initialize the Xlib connection */
    xlibdpy = xkbdpy = XOpenDisplay(NULL);

    /* Try to load the X cursors and initialize the XKB extension */
    if (xlibdpy == NULL) {
        ELOG("ERROR: XOpenDisplay() failed, disabling libXcursor/XKB support\n");
        xcursor_supported = false;
        xkb_supported = false;
    } else if (fcntl(ConnectionNumber(xlibdpy), F_SETFD, FD_CLOEXEC) == -1) {
        ELOG("Could not set FD_CLOEXEC on xkbdpy\n");
        return 1;
    } else {
        xcursor_load_cursors();
        /*init_xkb();*/
    }

    /* Set a cursor for the root window (otherwise the root window will show no
       cursor until the first client is launched). */
    if (xcursor_supported)
        xcursor_set_root_cursor(XCURSOR_CURSOR_POINTER);
    else xcb_set_root_cursor(XCURSOR_CURSOR_POINTER);

    if (xkb_supported) {
        int errBase,
            major = XkbMajorVersion,
            minor = XkbMinorVersion;

        if (fcntl(ConnectionNumber(xkbdpy), F_SETFD, FD_CLOEXEC) == -1) {
            fprintf(stderr, "Could not set FD_CLOEXEC on xkbdpy\n");
            return 1;
        }

        int i1;
        if (!XkbQueryExtension(xkbdpy,&i1,&xkb_event_base,&errBase,&major,&minor)) {
            fprintf(stderr, "XKB not supported by X-server\n");
            return 1;
        }
        /* end of ugliness */

        if (!XkbSelectEvents(xkbdpy, XkbUseCoreKbd,
                             XkbMapNotifyMask | XkbStateNotifyMask,
                             XkbMapNotifyMask | XkbStateNotifyMask)) {
            fprintf(stderr, "Could not set XKB event mask\n");
            return 1;
        }
    }

    /* Setup NetWM atoms */
    #define xmacro(name) \
        do { \
            xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, name ## _cookie, NULL); \
            if (!reply) { \
                ELOG("Could not get atom " #name "\n"); \
                exit(-1); \
            } \
            A_ ## name = reply->atom; \
            free(reply); \
        } while (0);
    #include "atoms.xmacro"
    #undef xmacro

    property_handlers_init();

    /* Set up the atoms we support */
    xcb_atom_t supported_atoms[] = {
#define xmacro(atom) A_ ## atom,
#include "atoms.xmacro"
#undef xmacro
    };
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, A__NET_SUPPORTED, XCB_ATOM_ATOM, 32, 16, supported_atoms);
    /* Set up the window manager’s name */
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, A__NET_SUPPORTING_WM_CHECK, XCB_ATOM_WINDOW, 32, 1, &root);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, A__NET_WM_NAME, A_UTF8_STRING, 8, strlen("i3"), "i3");

    keysyms = xcb_key_symbols_alloc(conn);

    xcb_numlock_mask = aio_get_mod_mask_for(XCB_NUM_LOCK, keysyms);

    translate_keysyms();
    grab_all_keys(conn, false);

    bool needs_tree_init = true;
    if (layout_path) {
        LOG("Trying to restore the layout from %s...", layout_path);
        needs_tree_init = !tree_restore(layout_path, greply);
        if (delete_layout_path)
            unlink(layout_path);
        free(layout_path);
    }
    if (needs_tree_init)
        tree_init(greply);

    free(greply);

    /* Force Xinerama (for drivers which don't support RandR yet, esp. the
     * nVidia binary graphics driver), when specified either in the config
     * file or on command-line */
    if (force_xinerama || config.force_xinerama) {
        xinerama_init();
    } else {
        DLOG("Checking for XRandR...\n");
        randr_init(&randr_base);
    }

    xcb_query_pointer_reply_t *pointerreply;
    Output *output = NULL;
    if (!(pointerreply = xcb_query_pointer_reply(conn, pointercookie, NULL))) {
        ELOG("Could not query pointer position, using first screen\n");
        output = get_first_output();
    } else {
        DLOG("Pointer at %d, %d\n", pointerreply->root_x, pointerreply->root_y);
        output = get_output_containing(pointerreply->root_x, pointerreply->root_y);
        if (!output) {
            ELOG("ERROR: No screen at (%d, %d), starting on the first screen\n",
                 pointerreply->root_x, pointerreply->root_y);
            output = get_first_output();
        }

        con_focus(con_descend_focused(output_get_content(output->con)));
    }

    tree_render();

    /* Create the UNIX domain socket for IPC */
    int ipc_socket = ipc_create_socket(config.ipc_socket_path);
    if (ipc_socket == -1) {
        ELOG("Could not create the IPC socket, IPC disabled\n");
    } else {
        free(config.ipc_socket_path);
        struct ev_io *ipc_io = scalloc(sizeof(struct ev_io));
        ev_io_init(ipc_io, ipc_new_client, ipc_socket, EV_READ);
        ev_io_start(main_loop, ipc_io);
    }

    /* Also handle the UNIX domain sockets passed via socket activation */
    int fds = sd_listen_fds(1);
    if (fds < 0)
        ELOG("socket activation: Error in sd_listen_fds\n");
    else if (fds == 0)
        DLOG("socket activation: no sockets passed\n");
    else {
        for (int fd = SD_LISTEN_FDS_START; fd < (SD_LISTEN_FDS_START + fds); fd++) {
            DLOG("socket activation: also listening on fd %d\n", fd);
            struct ev_io *ipc_io = scalloc(sizeof(struct ev_io));
            ev_io_init(ipc_io, ipc_new_client, fd, EV_READ);
            ev_io_start(main_loop, ipc_io);
        }
    }

    /* Set up i3 specific atoms like I3_SOCKET_PATH and I3_CONFIG_PATH */
    x_set_i3_atoms();

    struct ev_io *xcb_watcher = scalloc(sizeof(struct ev_io));
    struct ev_io *xkb = scalloc(sizeof(struct ev_io));
    struct ev_check *xcb_check = scalloc(sizeof(struct ev_check));
    struct ev_prepare *xcb_prepare = scalloc(sizeof(struct ev_prepare));

    ev_io_init(xcb_watcher, xcb_got_event, xcb_get_file_descriptor(conn), EV_READ);
    ev_io_start(main_loop, xcb_watcher);


    if (xkb_supported) {
        ev_io_init(xkb, xkb_got_event, ConnectionNumber(xkbdpy), EV_READ);
        ev_io_start(main_loop, xkb);

        /* Flush the buffer so that libev can properly get new events */
        XFlush(xkbdpy);
    }

    ev_check_init(xcb_check, xcb_check_cb);
    ev_check_start(main_loop, xcb_check);

    ev_prepare_init(xcb_prepare, xcb_prepare_cb);
    ev_prepare_start(main_loop, xcb_prepare);

    xcb_flush(conn);

    manage_existing_windows(root);

    if (!disable_signalhandler)
        setup_signal_handler();

    /* Ignore SIGPIPE to survive errors when an IPC client disconnects
     * while we are sending him a message */
    signal(SIGPIPE, SIG_IGN);

    /* Autostarting exec-lines */
    if (autostart) {
        struct Autostart *exec;
        TAILQ_FOREACH(exec, &autostarts, autostarts) {
            LOG("auto-starting %s\n", exec->command);
            start_application(exec->command, exec->no_startup_id);
        }
    }

    /* Autostarting exec_always-lines */
    struct Autostart *exec_always;
    TAILQ_FOREACH(exec_always, &autostarts_always, autostarts_always) {
        LOG("auto-starting (always!) %s\n", exec_always->command);
        start_application(exec_always->command, exec_always->no_startup_id);
    }

    /* Start i3bar processes for all configured bars */
    Barconfig *barconfig;
    TAILQ_FOREACH(barconfig, &barconfigs, configs) {
        char *command = NULL;
        sasprintf(&command, "i3bar --bar_id=%s --socket=\"%s\"",
                  barconfig->id, current_socketpath);
        LOG("Starting bar process: %s\n", command);
        start_application(command, true);
        free(command);
    }

    /* Make sure to destroy the event loop to invoke the cleeanup callbacks
     * when calling exit() */
    atexit(i3_exit);

    ev_loop(main_loop, 0);
}

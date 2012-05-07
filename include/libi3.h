/*
 * vim:ts=4:sw=4:expandtab
 *
 * i3 - an improved dynamic tiling window manager
 * © 2009-2011 Michael Stapelberg and contributors (see also: LICENSE)
 *
 * libi3: contains functions which are used by i3 *and* accompanying tools such
 * as i3-msg, i3-config-wizard, …
 *
 */
#ifndef _LIBI3_H
#define _LIBI3_H

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>

typedef struct Font i3Font;

/**
 * Data structure for cached font information:
 * - font id in X11 (load it once)
 * - font height (multiple calls needed to get it)
 *
 */
struct Font {
    /** The xcb-id for the font */
    xcb_font_t id;

    /** Font information gathered from the server */
    xcb_query_font_reply_t *info;

    /** Font table for this font (may be NULL) */
    xcb_charinfo_t *table;

    /** The height of the font, built from font_ascent + font_descent */
    int height;
};

/* Since this file also gets included by utilities which don’t use the i3 log
 * infrastructure, we define a fallback. */
#if !defined(ELOG)
#define ELOG(fmt, ...) fprintf(stderr, "ERROR: " fmt, ##__VA_ARGS__)
#endif

/**
 * Try to get the contents of the given atom (for example I3_SOCKET_PATH) from
 * the X11 root window and return NULL if it doesn’t work.
 *
 * The memory for the contents is dynamically allocated and has to be
 * free()d by the caller.
 *
 */
char *root_atom_contents(const char *atomname);

/**
 * Safe-wrapper around malloc which exits if malloc returns NULL (meaning that
 * there is no more memory available)
 *
 */
void *smalloc(size_t size);

/**
 * Safe-wrapper around calloc which exits if malloc returns NULL (meaning that
 * there is no more memory available)
 *
 */
void *scalloc(size_t size);

/**
 * Safe-wrapper around realloc which exits if realloc returns NULL (meaning
 * that there is no more memory available).
 *
 */
void *srealloc(void *ptr, size_t size);

/**
 * Safe-wrapper around strdup which exits if malloc returns NULL (meaning that
 * there is no more memory available)
 *
 */
char *sstrdup(const char *str);

/**
 * Safe-wrapper around asprintf which exits if it returns -1 (meaning that
 * there is no more memory available)
 *
 */
int sasprintf(char **strp, const char *fmt, ...);

/**
 * Connects to the i3 IPC socket and returns the file descriptor for the
 * socket. die()s if anything goes wrong.
 *
 */
int ipc_connect(const char *socket_path);

/**
 * Formats a message (payload) of the given size and type and sends it to i3 via
 * the given socket file descriptor.
 *
 * Returns -1 when write() fails, errno will remain.
 * Returns 0 on success.
 *
 */
int ipc_send_message(int sockfd, uint32_t message_size,
                     uint32_t message_type, const uint8_t *payload);

/**
 * Reads a message from the given socket file descriptor and stores its length
 * (reply_length) as well as a pointer to its contents (reply).
 *
 * Returns -1 when read() fails, errno will remain.
 * Returns -2 when the IPC protocol is violated (invalid magic, unexpected
 * message type, EOF instead of a message). Additionally, the error will be
 * printed to stderr.
 * Returns 0 on success.
 *
 */
int ipc_recv_message(int sockfd, uint32_t message_type,
                     uint32_t *reply_length, uint8_t **reply);

/**
 * Generates a configure_notify event and sends it to the given window
 * Applications need this to think they’ve configured themselves correctly.
 * The truth is, however, that we will manage them.
 *
 */
void fake_configure_notify(xcb_connection_t *conn, xcb_rectangle_t r, xcb_window_t window, int border_width);

/**
 * Returns the colorpixel to use for the given hex color (think of HTML). Only
 * works for true-color (vast majority of cases) at the moment, avoiding a
 * roundtrip to X11.
 *
 * The hex_color has to start with #, for example #FF00FF.
 *
 * NOTE that get_colorpixel() does _NOT_ check the given color code for validity.
 * This has to be done by the caller.
 *
 * NOTE that this function may in the future rely on a global xcb_connection_t
 * variable called 'conn' to be present.
 *
 */
uint32_t get_colorpixel(const char *hex) __attribute__((const));

#if defined(__APPLE__)

/*
 * Taken from FreeBSD
 * Returns a pointer to a new string which is a duplicate of the
 * string, but only copies at most n characters.
 *
 */
char *strndup(const char *str, size_t n);

#endif

/**
 * All-in-one function which returns the modifier mask (XCB_MOD_MASK_*) for the
 * given keysymbol, for example for XCB_NUM_LOCK (usually configured to mod2).
 *
 * This function initiates one round-trip. Use get_mod_mask_for() directly if
 * you already have the modifier mapping and key symbols.
 *
 */
uint32_t aio_get_mod_mask_for(uint32_t keysym, xcb_key_symbols_t *symbols);

/**
 * Returns the modifier mask (XCB_MOD_MASK_*) for the given keysymbol, for
 * example for XCB_NUM_LOCK (usually configured to mod2).
 *
 * This function does not initiate any round-trips.
 *
 */
uint32_t get_mod_mask_for(uint32_t keysym,
                           xcb_key_symbols_t *symbols,
                           xcb_get_modifier_mapping_reply_t *modmap_reply);

/**
 * Loads a font for usage, also getting its height. If fallback is true,
 * the fonts 'fixed' or '-misc-*' will be loaded instead of exiting.
 *
 */
i3Font load_font(const char *pattern, const bool fallback);

/**
 * Defines the font to be used for the forthcoming calls.
 *
 */
void set_font(i3Font *font);

/**
 * Frees the resources taken by the current font.
 *
 */
void free_font(void);

/**
 * Converts the given string to UTF-8 from UCS-2 big endian. The return value
 * must be freed after use.
 *
 */
char *convert_ucs2_to_utf8(xcb_char2b_t *text, size_t num_glyphs);

/**
 * Converts the given string to UCS-2 big endian for use with
 * xcb_image_text_16(). The amount of real glyphs is stored in real_strlen,
 * a buffer containing the UCS-2 encoded string (16 bit per glyph) is
 * returned. It has to be freed when done.
 *
 */
xcb_char2b_t *convert_utf8_to_ucs2(char *input, size_t *real_strlen);

/**
 * Defines the colors to be used for the forthcoming draw_text calls.
 *
 */
void set_font_colors(xcb_gcontext_t gc, uint32_t foreground, uint32_t background);

/**
 * Draws text onto the specified X drawable (normally a pixmap) at the
 * specified coordinates (from the top left corner of the leftmost, uppermost
 * glyph) and using the provided gc.
 *
 * Text can be specified as UCS-2 or UTF-8. If it's specified as UCS-2, then
 * text_len must be the number of glyphs in the string. If it's specified as
 * UTF-8, then text_len must be the number of bytes in the string (not counting
 * the null terminator).
 *
 */
void draw_text(char *text, size_t text_len, bool is_ucs2, xcb_drawable_t drawable,
        xcb_gcontext_t gc, int x, int y, int max_width);

/**
 * Predict the text width in pixels for the given text. Text can be specified
 * as UCS-2 or UTF-8.
 *
 */
int predict_text_width(char *text, size_t text_len, bool is_ucs2);

#endif

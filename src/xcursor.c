/*
 * vim:ts=4:sw=4:expandtab
 */
#include <assert.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/cursorfont.h>

#include "i3.h"
#include "xcb.h"
#include "xcursor.h"

static Cursor cursors[XCURSOR_CURSOR_MAX];

static const int xcb_cursors[XCURSOR_CURSOR_MAX] = {
    XCB_CURSOR_LEFT_PTR,
    XCB_CURSOR_SB_H_DOUBLE_ARROW,
    XCB_CURSOR_SB_V_DOUBLE_ARROW
};

static Cursor load_cursor(const char *name) {
    Cursor c = XcursorLibraryLoadCursor(xlibdpy, name);
    if (c == None)
        xcursor_supported = false;
    return c;
}

void xcursor_load_cursors() {
    cursors[XCURSOR_CURSOR_POINTER] = load_cursor("left_ptr");
    cursors[XCURSOR_CURSOR_RESIZE_HORIZONTAL] = load_cursor("sb_h_double_arrow");
    cursors[XCURSOR_CURSOR_RESIZE_VERTICAL] = load_cursor("sb_v_double_arrow");
}

/*
 * Sets the cursor of the root window to the 'pointer' cursor.
 *
 * This function is called when i3 is initialized, because with some login
 * managers, the root window will not have a cursor otherwise.
 *
 * We have a separate xcursor function to use the same X11 connection as the
 * xcursor_load_cursors() function. If we mix the Xlib and the XCB connection,
 * races might occur (even though we flush the Xlib connection).
 *
 */
void xcursor_set_root_cursor() {
    XSetWindowAttributes attributes;
    attributes.cursor = xcursor_get_cursor(XCURSOR_CURSOR_POINTER);
    XChangeWindowAttributes(xlibdpy, DefaultRootWindow(xlibdpy), CWCursor, &attributes);
    XFlush(xlibdpy);
}

Cursor xcursor_get_cursor(enum xcursor_cursor_t c) {
    assert(c >= 0 && c < XCURSOR_CURSOR_MAX);
    return cursors[c];
}

int xcursor_get_xcb_cursor(enum xcursor_cursor_t c) {
    assert(c >= 0 && c < XCURSOR_CURSOR_MAX);
    return xcb_cursors[c];
}

/*
 * vim:ts=4:sw=4:expandtab
 *
 * i3 - an improved dynamic tiling window manager
 * © 2009-2012 Michael Stapelberg and contributors (see also: LICENSE)
 *
 * key_press.c: key press handler
 *
 */
#ifndef I3_KEY_PRESS_H
#define I3_KEY_PRESS_H

/**
 * There was a key press. We compare this key code with our bindings table and pass
 * the bound action to parse_command().
 *
 */
void handle_key_press(xcb_key_press_event_t *event);

/**
 * Kills the commanderror i3-nagbar process, if any.
 *
 * Called when reloading/restarting, since the user probably fixed his wrong
 * keybindings.
 *
 * If wait_for_it is set (restarting), this function will waitpid(), otherwise,
 * ev is assumed to handle it (reloading).
 *
 */
void kill_commanderror_nagbar(bool wait_for_it);

#endif

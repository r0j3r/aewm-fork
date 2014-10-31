/*-
 * aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <sys/types.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include "util.h"

Display *dpy;
Window root;
int screen;
Atom utf8_string;
Atom wm_state;
Atom wm_change_state;
Atom wm_protos;
Atom wm_delete;
Atom net_supported;
Atom net_client_list;
Atom net_client_stack;
Atom net_active_window;
Atom net_close_window;
Atom net_cur_desk;
Atom net_num_desks;
Atom net_wm_name;
Atom net_wm_desk;
Atom net_wm_state;
Atom net_wm_state_shaded;
Atom net_wm_state_mv;
Atom net_wm_state_mh;
Atom net_wm_state_fs;
Atom net_wm_state_skipt;
Atom net_wm_state_skipp;
Atom net_wm_strut;
Atom net_wm_strut_partial;
Atom net_wm_wintype;
Atom net_wm_type_desk;
Atom net_wm_type_dock;
Atom net_wm_type_menu;
Atom net_wm_type_splash;

static char *atom_str_get(Window, Atom, Atom);

void fork_exec(char *cmd)
{
    pid_t pid = fork();

    switch (pid) {
        case 0:
            setsid();
            execlp("/bin/sh", "sh", "-c", cmd, NULL);
            fprintf(stderr, "exec failed, cleaning up child\n");
            exit(1);
        case -1:
            fprintf(stderr, "can't fork\n");
    }
}

/* If the user specifies an rc file, return NULL immediately if it's not
 * found; otherwise, search for the usual suspects. */

FILE *rc_open(const char *rcfile, const char *def)
{
    FILE *rc;
    char buf[BUF_SIZE];

    if (rcfile) {
        return fopen(rcfile, "r");
    } else {
        snprintf(buf, sizeof buf, "%s/.aewm/%s", getenv("HOME"), def);
        if ((rc = fopen(buf, "r"))) {
            return rc;
        } else {
            snprintf(buf, sizeof buf, "%s/%s", SYS_RC_DIR, def);
            return fopen(buf, "r");
        }
    }
}

char *rc_getl(char *s, int size, FILE *stream)
{
    while (fgets(s, size, stream)) {
        if (s[0] == '#' || s[0] == '\n')
            continue;
        else
            return s;
    }
    return NULL;
}

/* Our crappy parser. A token is either a whitespace-delimited word, or a
 * bunch of words in double quotes (backslashes are permitted in either case).
 * src points to somewhere in a buffer -- the caller must save the location of
 * this buffer, because we update src to point past all the tokens found so
 * far. If we find a token, we write it into dest (caller is responsible for
 * allocating storage) and return 1. Otherwise return 0. */

int tok_next(char **src, char *dest)
{
    int quoted = 0, nchars = 0;

    while (**src && isspace(**src)) (*src)++;

    if (**src == '"') {
        quoted = 1;
        (*src)++;
    }

    while (**src) {
        if (quoted) {
            if (**src == '"') {
                (*src)++;
                break;
            }
        } else {
            if (isspace(**src))
                break;
        }
        if (**src == '\\') (*src)++;
        *dest++ = *(*src)++;
        nchars++;
    }

    *dest = '\0';
    return nchars || quoted;
}

int win_send_msg(Window w, Atom a, unsigned long x, unsigned long mask)
{
    XClientMessageEvent e;

    e.type = ClientMessage;
    e.window = w;
    e.message_type = a;
    e.format = 32;
    e.data.l[0] = x;
    e.data.l[1] = CurrentTime;

    return XSendEvent(dpy, w, False, mask, (XEvent *)&e);
}

/* Despite the fact that all these are 32 bits on the wire, libX11 really does
 * stuff an array of longs into *data, so you get 64 bits on 64-bit archs. So
 * we gotta be careful here. */

unsigned long atom_get(Window w, Atom a, Atom type, unsigned long off,
    unsigned long *ret, unsigned long nitems, unsigned long *left)
{
    Atom real_type;
    int i, real_format = 0;
    unsigned long items_read = 0;
    unsigned long bytes_left = 0;
    unsigned char *data;
    CARD32 *p;

    XGetWindowProperty(dpy, w, a, off, nitems, False, type,
        &real_type, &real_format, &items_read, &bytes_left, &data);

    if (real_format == 32 && items_read) {
        p = (CARD32 *)data;
        for (i = 0; i < items_read; i++) ret[i] = p[i];
        XFree(data);
        if (left) *left = bytes_left;
        return items_read;
    } else {
        return 0;
    }
}

unsigned long atom_set(Window w, Atom a, Atom type, unsigned long *val,
    unsigned long nitems)
{
    return (XChangeProperty(dpy, w, a, type, 32, PropModeReplace,
        (unsigned char *)val, nitems) == Success);
}

unsigned long atom_add(Window w, Atom a, Atom type, unsigned long *val,
    unsigned long nitems)
{
    return (XChangeProperty(dpy, w, a, type, 32, PropModeAppend,
        (unsigned char *)val, nitems) == Success);
}

void atom_del(Window w, Atom a, Atom type, unsigned long val)
{
    unsigned long tmp, read, left, *new;
    int i, j = 0;

    read = atom_get(w, a, type, 0, &tmp, 1, &left);
    if (!read) return;

    new = malloc((read + left) * sizeof *new);
    if (read && tmp != val)
        new[j++] = tmp;

    for (i = 1, read = left = 1; read && left; i += read) {
        read = atom_get(w, a, type, i, &tmp, 1, &left);
        if (!read)
            break;
        else if (tmp != val)
            new[j++] = tmp;
    }

    if (j)
        XChangeProperty(dpy, w, a, type, 32, PropModeReplace,
            (unsigned char *)new, j);
    else
        XDeleteProperty(dpy, w, a);
}

/* Get the window-manager name (aka human-readable "title") for a given
 * window. There are two ways a client can set this:
 *
 * 1. _NET_WM_STRING, which has type UTF8_STRING. This is preferred and
 *    is always used if available.
 * 2. WM_NAME, which has type COMPOUND_STRING or STRING. This is the old
 *    ICCCM way, which we fall back to in the absence of _NET_WM_STRING.
 *    In this case, we ask X to convert the value of the property to
 *    UTF-8 for us. N.b.: STRING is Latin-1 whatever the locale.
 *    COMPOUND_STRING is the most hideous abomination ever created.
 *    Thankfully we do not have to worry about any of this.
 *
 * If UTF-8 conversion is not available (XFree86 < 4.0.2, or any older X
 * implementation), only WM_NAME will be checked, and, at least for
 * XFree86 and X.Org, it will only be returned if it has type STRING.
 * This is due to an inherent limitation in their implementation of
 * XFetchName. If you have a different X vendor, YMMV.
 *
 * In all cases, this function asks X to allocate the returned string,
 * so it must be freed with XFree. */

char *win_name_get(Window w)
{
#ifdef X_HAVE_UTF8_STRING
    XTextProperty name_prop;
    XTextProperty name_prop_converted;
    char *name, **name_list;
    int nitems;

    if ((name = atom_str_get(w, net_wm_name, utf8_string))) {
        return name;
    } else if (XGetWMName(dpy, w, &name_prop)) {
        if (Xutf8TextPropertyToTextList(dpy, &name_prop, &name_list,
                &nitems) == Success) {
            /* Now we've got a freshly allocated XTextList. Since it might
             * have multiple items that need to be joined, and we need to
             * return something that can be freed by XFree, we roll it back up
             * into an XTextProperty. */
            if (Xutf8TextListToTextProperty(dpy, name_list, nitems,
                    XUTF8StringStyle, &name_prop_converted) == Success) {
                XFreeStringList(name_list);
                return (char *)name_prop_converted.value;
            } else {
                /* Not much we can do here. This should never happen anyway.
                 * Famous last words. */
                XFreeStringList(name_list);
                return NULL;
            }
        } else {
            return (char *)name_prop.value;
        }
    } else {
        /* There is no prop. There is only NULL! */
        return NULL;
    }
#else
    XFetchName(dpy, w, &name);
    return name;
#endif
}

/* I give up on trying to do this the right way. We'll just request as many
 * elements as possible. If that's not the entire string, we're fucked. In
 * reality this should never happen. (That's the second time I get to say
 * ``this should never happen'' in this file!)
 *
 * Standard gripe about casting nonsense applies. */

static char *atom_str_get(Window w, Atom a, Atom type)
{
    Atom real_type;
    int real_format = 0;
    unsigned long items_read = 0;
    unsigned long bytes_left = 0;
    unsigned char *data;

    XGetWindowProperty(dpy, w, a, 0, LONG_MAX, False, type,
        &real_type, &real_format, &items_read, &bytes_left, &data);

    /* XXX: should check bytes_left here and bail if nonzero, in case someone
     * wants to store a >4gb string on the server */

    if (real_format == 8 && items_read >= 1)
        return (char *)data;
    else
        return NULL;
}

unsigned long win_state_get(Window w)
{
    unsigned long state;

    if (atom_get(w, wm_state, wm_state, 0, &state, 1, NULL))
        return state;
    else
        return WithdrawnState;
}

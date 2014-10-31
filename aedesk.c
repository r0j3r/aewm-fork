/*-
 * aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "util.h"

#define UMOD(x, y) ((((long)(x) % (long)(y)) + (y)) % (y))

Display *dpy;
Window root;
Atom net_cur_desk, net_num_desks;

static long parse_desk(char *spec);

int main(int argc, char **argv)
{
    unsigned long ndesks, desk;
    int i;

    if (argc < 2) {
        fprintf(stderr, "usage: aedesk [+-]<integer>|-n <integer>\n");
        exit(2);
    }

    dpy = XOpenDisplay(NULL);
    root = DefaultRootWindow(dpy);
    net_cur_desk = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    net_num_desks = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);

    if (!dpy) {
        fprintf(stderr, "aedesk: can't open display %s\n", getenv("DISPLAY"));
        exit(1);
    }

    for (i = 1; i < argc; i++) {
        if ARG("setn", "n", 1) {
            ndesks = atol(argv[++i]);
            atom_set(root, net_num_desks, XA_CARDINAL, &ndesks, 1);
        } else {
            desk = parse_desk(argv[i]);
            win_send_msg(root, net_cur_desk, desk, SubstructureNotifyMask);
        }
    }

    XCloseDisplay(dpy);
    return 0;
}

static long parse_desk(char *spec)
{
    unsigned long ndesks, cur_desk;

    if (strchr("+-", spec[0]) && atom_get(root, net_cur_desk, XA_CARDINAL, 0,
            &cur_desk, 1, NULL) && atom_get(root, net_num_desks, XA_CARDINAL,
            0, &ndesks, 1, NULL))
        return UMOD(cur_desk + atol(spec), ndesks);
    else
        return atol(spec);
}

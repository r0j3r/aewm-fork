/*-
 * aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.
 */

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include "aewm.h"

static void do_set_iconified(Client *, long);
static void do_sweep(Client *, SweepFunc);
static Bool crossed(int a, int pa, int ref, int w);
static void cli_draw_outline(Client *);

void cli_pressed(Client *c, int x, int y, int button)
{
    if (x >= c->geom.w - GH(c) && y <= GH(c)) {
        switch (button) {
            case Button1: cli_set_iconified(c, IconicState); break;
            case Button2: cli_resize(c); break;
            case Button3: cli_req_close(c); break;
            case Button4: cli_grow(c); break;
            case Button5: cli_shrink(c); break;
        }
    } else {
        switch (button) {
            case Button1: cli_raise(c); break;
            case Button2: cli_move(c); break;
            case Button3: cli_lower(c); break;
            case Button4: cli_shade(c); break;
            case Button5: cli_unshade(c); break;
        }
    }
}

void cli_raise(Client *c)
{
    XRaiseWindow(dpy, c->frame);
    win_list_update();
}

void cli_lower(Client *c)
{
    XLowerWindow(dpy, c->frame);
    win_list_update();
}

void cli_show(Client *c)
{
    XMapWindow(dpy, c->win);
    XMapWindow(dpy, c->frame);
}

void cli_hide(Client *c)
{
    XWindowAttributes attr;

    XGetWindowAttributes(dpy, c->win, &attr);
    c->ign_unmap = True;
    XUnmapWindow(dpy, c->frame);
    XUnmapWindow(dpy, c->win);
}

void cli_focus(Client *c)
{
    atom_set(root, net_active_window, XA_WINDOW, &c->win, 1);
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XInstallColormap(dpy, c->cmap);
}

void cli_move(Client *c)
{
    if (!c->zoomed) {
        cli_sweep(c, crs_move, calc_move);
        cli_map_apply(c);
    }
}

/* If we are resizing a client that was zoomed, we have to put it in an
 * unzoomed state, but we need to start sweeping from the effective geometry
 * rather than the "real" geometry that unzooming will restore. We get around
 * this by blatantly cheating. */

void cli_resize(Client *c)
{
    if (c->zoomed) c->save = c->geom;
    cli_shrink(c); /* FIXME */

    cli_sweep(c, crs_size, calc_resize);
    cli_map_apply(c);
}

/* Transients will be iconified when their owner is iconified. */

void cli_set_iconified(Client *c, long state)
{
    Client *t;
    unsigned int i;

    do_set_iconified(c, state);
    for (i = 0; i < nwins; i++)
        if (FIND_CTX(wins[i], frame_tab, &t) && t->trans == c->win)
            do_set_iconified(t, state);
}

static void do_set_iconified(Client *c, long state)
{
    cli_state_set(c, state);

    if (state == IconicState)
        cli_hide(c);
    else
        cli_show(c);
}

void cli_shade(Client *c)
{
    if (!c->shaded) {
        atom_add(c->win, net_wm_state, XA_ATOM, &net_wm_state_shaded, 1);
        c->shaded = True;
        cli_map_apply(c);
    }
}

void cli_unshade(Client *c)
{
    if (c->shaded) {
        atom_del(c->win, net_wm_state, XA_ATOM, net_wm_state_shaded);
        c->shaded = False;
        cli_map_apply(c);
    }
}

/* When zooming a window, the old geom gets stuffed into c->save. Once we
 * unzoom, this should be considered garbage. Despite the existence of
 * vertical and horizontal hints, we only provide both at once.
 *
 * Zooming implies unshading, but the inverse is not true. */

void cli_grow(Client *c)
{
    Brace b;

    if (!c->zoomed) {
        atom_del(c->win, net_wm_state, XA_ATOM, net_wm_state_shaded);
        atom_add(c->win, net_wm_state, XA_ATOM, &net_wm_state_mv, 1);
        atom_add(c->win, net_wm_state, XA_ATOM, &net_wm_state_mh, 1);
        c->save = c->geom;
        c->shaded = False;
        c->zoomed = True;
        b = desk_braces_sum(c->desk);
        c->geom.x = b.l + BW(c);
        c->geom.y = b.t + BW(c);
        c->geom.w = b.r - b.l - 2 * BW(c);
        c->geom.h = b.b - b.t - 2 * BW(c) - GH(c);
        cli_geom_fixup(c);
        cli_map_apply(c);
        cli_frame_redraw(c);
    }
}

void cli_shrink(Client *c)
{
    if (c->zoomed) {
        atom_del(c->win, net_wm_state, XA_ATOM, net_wm_state_mv);
        atom_del(c->win, net_wm_state, XA_ATOM, net_wm_state_mh);
        c->geom = c->save;
        c->zoomed = False;
        cli_map_apply(c);
        cli_frame_redraw(c);
    }
}

void cli_req_close(Client *c)
{
    int i, n, found = 0;
    Atom *protocols;

    if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
        for (i=0; i<n; i++) if (protocols[i] == wm_delete) found++;
        XFree(protocols);
    }

    if (found)
        win_send_msg(c->win, wm_protos, wm_delete, NoEventMask);
    else
        XKillClient(dpy, c->win);
}

void cli_sweep(Client *c, Cursor curs, SweepFunc cb)
{
    XGrabServer(dpy);
    if (XGrabPointer(dpy, root, False, MOUSE_MASK, GrabModeAsync,
            GrabModeAsync, None, curs, CurrentTime) == GrabSuccess &&
            XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync,
            CurrentTime) == GrabSuccess)
    {
        do_sweep(c, cb);
        XUngrabKeyboard(dpy, CurrentTime);
        XUngrabPointer(dpy, CurrentTime);
    }
    XUngrabServer(dpy);

    alarm(0);
    timed_out = 0;
}

/*
 * When we start a sweep, the the server is most likely not grabbed, and
 * the client outline is definitely not drawn. The loop draws it as soon
 * as the grab starts, before blocking in event_get_next.
 *
 * If that fails to return an event, it was interrupted by a signal, and
 * either we timed out or were killed. In either case, we're done.
 */

static void do_sweep(Client *c, SweepFunc cb)
{
    Geom orig = c->geom, motion = {0, 0, 0, 0};
    Brace b = desk_braces_sum(c->desk), hold = {0, 0, 0, 0};
    XEvent ev;
    long dummy_x, dummy_y;

    pointer_get(&motion.x, &motion.y);
    cb(c, &orig, &motion, &b, &hold);

    for (cli_draw_outline(c); !timed_out; cli_draw_outline(c)) {
        if (!event_get_next(MOUSE_MASK|KEY_MASK, &ev))
            return;
        cli_draw_outline(c); /* clear */
        switch (ev.type) {
            case MotionNotify:
                motion.x += motion.w = ev.xmotion.x - motion.x;
                motion.y += motion.h = ev.xmotion.y - motion.y;
                cb(c, &orig, &motion, &b, &hold);
                if (ev.xmotion.is_hint)
                    pointer_get(&dummy_x, &dummy_y); /* trigger next hint */
                break;
            case ButtonPress:
                alarm(0);
                if (ev.xbutton.button == Button2) {
                    if (motion.w || motion.h) {
                        if (cb == calc_move) cli_resize(c);
                        else /* cb == calc_resize */ cli_move(c);
                        return;
                    } else {
                        motion.w = motion.h = 1; /* fake it */
                    }
                }
                break;
            case ButtonRelease:
                switch (ev.xbutton.button) {
                    case Button1: cli_raise(c); break;
                    case Button2: if (!motion.w && !motion.h) continue; break;
                    case Button3: cli_lower(c); break;
                    case Button4: cli_grow(c); break;
                    case Button5: cli_set_iconified(c, IconicState); break;
                }
                return;
            case KeyRelease:
                return;
        }
    }
}

/* XXX REWRITE THIS COMMENT */

void calc_move(Client *c, Geom *orig, Geom *m, Brace *b, Brace *h)
{
    Geom f = cli_frame_geom(c, c->geom);
    int px = m->x - m->w;
    int py = m->y - m->h;

    if (px > L(f, c) && px < R(f, c) && py > T(f, c) && py < B(f, c)) {
        c->geom.x += m->w;
        c->geom.y += m->h;
    }
}

static Bool crossed(int a, int pa, int ref, int w)
{
    return (pa <= ref && a > ref) || (pa >= ref + w && a < ref + w);
}

void calc_resize(Client *c, Geom *orig, Geom *m, Brace *b, Brace *h)
{
    Geom f = cli_frame_geom(c, *orig);
    int px = m->x - m->w;
    int py = m->y - m->h;

    if (m->w == 0 && m->h == 0)
        return;

    if (crossed(m->x, px, L(f, c), BW(c)) || (m->x == 0)) {
        h->l = 1;
        h->r = 0;
    } else if (crossed(m->x, px, R(f, c) - BW(c), BW(c)) || (m->x == rw-1)) {
        h->l = 0;
        h->r = 1;
    }
    if (crossed(m->y, py, T(f, c), BW(c)) || (m->y == 0)) {
        h->t = 1;
        h->b = 0;
    } else if (crossed(m->y, py, B(f, c) - BW(c), BW(c)) || (m->y == rh-1)) {
        h->t = 0;
        h->b = 1;
    }

    if (h->l) {
        c->geom.x = m->x - BW(c);
        c->geom.w = orig->x + orig->w - m->x + BW(c);
    } else if (h->r) {
        c->geom.x = orig->x;
        c->geom.w = m->x - orig->x - BW(c);
    }
    if (h->t) {
        c->geom.y = m->y - BW(c);
        c->geom.h = orig->y + orig->h - m->y + BW(c);
    } else if (h->b) {
        c->geom.y = orig->y;
        c->geom.h = m->y - orig->y - GH(c) - BW(c);
    }

    cli_geom_fixup(c);
    if (h->l) c->geom.x = orig->x + orig->w - c->geom.w;
    if (h->r) c->geom.x = orig->x;
    if (h->t) c->geom.y = orig->y + orig->h - c->geom.h;
    if (h->b) c->geom.y = orig->y;
}

/* Match the calculations we use to draw the frames, and also the spacing
 * of text from the opposite corner. */

static void cli_draw_outline(Client *c)
{
    Geom adj, f = cli_frame_geom(c, c->geom);
    char buf[BUF_SMALL];
    int len;

    XDrawRectangle(dpy, root, inv_gc, f.x + BW(c)/2, f.y + BW(c)/2,
        f.w + BW(c), f.h + BW(c));
    if (!c->shaded)
        XDrawLine(dpy, root, inv_gc,
            L(f, c) + BW(c), T(f, c) + GH(c) + BW(c)/2,
            R(f, c) - BW(c), T(f, c) + GH(c) + BW(c)/2);

    adj = cli_geom_fixup(c);
    len = snprintf(buf, sizeof buf, "%ldx%ld%+ld%+ld", adj.w, adj.h,
        c->geom.x, c->geom.y);
    XDrawString(dpy, root, inv_gc,
        R(f, c) - opt_pad - font->descent/2 - XTextWidth(font, buf, len),
        B(f, c) - BW(c) - opt_pad - font->descent, buf, len);
}

/* If the window in question has a ResizeInc hint, then it wants to be resized
 * in multiples of some (x,y). We constrain the values in c->geom based on
 * that and any min/max size hints, and put the ``human readable'' values back
 * in lw_ret and lh_ret (80x25 for xterm, etc). */

Geom cli_geom_fixup(Client *c)
{
    int width_inc, height_inc, base_width, base_height;
    int min_w = GH(c) + BW(c);
    int min_h = 5 * BW(c);
    Geom adj = {0, 0, 0, 0};

    if (c->geom.w < min_w) {
        c->geom.x -= min_w - c->geom.w;
        c->geom.w = min_w;
    }
    if (c->geom.h < min_h) {
        c->geom.y -= min_h - c->geom.h;
        c->geom.h = min_h;
    }

    if (c->size.flags & PMinSize) {
        if (c->geom.w < c->size.min_width) c->geom.w = c->size.min_width;
        if (c->geom.h < c->size.min_height) c->geom.h = c->size.min_height;
    }
    if (c->size.flags & PMaxSize) {
        if (c->geom.w > c->size.max_width) c->geom.w = c->size.max_width;
        if (c->geom.h > c->size.max_height) c->geom.h = c->size.max_height;
    }

    if (c->size.flags & PResizeInc) {
        width_inc = c->size.width_inc ? c->size.width_inc : 1;
        height_inc = c->size.height_inc ? c->size.height_inc : 1;
        base_width = (c->size.flags & PBaseSize) ? c->size.base_width :
            (c->size.flags & PMinSize) ? c->size.min_width : 0;
        base_height = (c->size.flags & PBaseSize) ? c->size.base_height :
            (c->size.flags & PMinSize) ? c->size.min_height : 0;
        c->geom.w -= (c->geom.w - base_width) % width_inc;
        c->geom.h -= (c->geom.h - base_height) % height_inc;
        adj.w = (c->geom.w - base_width) / width_inc;
        adj.h = (c->geom.h - base_height) / height_inc;
    } else {
        adj.w = c->geom.w;
        adj.h = c->geom.h;
    }

    return adj;
}

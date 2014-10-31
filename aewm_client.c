/*-
 * aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.
 */

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <X11/Xatom.h>
#ifdef SHAPE
#include <X11/extensions/shape.h>
#endif
#include "aewm.h"

static Bool cli_geom_init(Client *);
static void cli_reparent(Client *);

/* Set up a client structure for the new (not-yet-mapped) window. */

Client *cli_new(Window w)
{
    Client *c;
    XWindowAttributes attr;
    long supplied;
    Atom win_type;

    c = malloc(sizeof *c);
    XSaveContext(dpy, w, cli_tab, (XPointer)c);

    c->win = w;
    c->frame = c->trans = None;
    c->name = NULL;
    c->desk = cur_desk;
#ifdef XFT
    c->xftdraw = NULL;
#endif
    c->shaded = False;
    c->zoomed = False;
    c->decor = True;
    c->cfg_lock = True;
    c->ign_unmap = False;

    XGetTransientForHint(dpy, c->win, &c->trans);
    c->size.flags = 0;
    XGetWMNormalHints(dpy, c->win, &c->size, &supplied);
    XGetWindowAttributes(dpy, c->win, &attr);

    c->geom.x = attr.x;
    c->geom.y = attr.y;
    c->geom.w = attr.width;
    c->geom.h = attr.height;
    c->cmap = attr.colormap;

    if (atom_get(c->win, net_wm_wintype, XA_ATOM, 0, &win_type, 1, NULL) && (
            win_type == net_wm_type_desk || win_type == net_wm_type_dock ||
            win_type == net_wm_type_menu || win_type == net_wm_type_splash)) {
        c->decor = False;
    }

    if (!atom_get(c->win, net_wm_desk, XA_CARDINAL, 0, &c->desk, 1, NULL) ||
            !(c->desk >= ndesks && c->desk != DESK_ALL)) {
        atom_set(c->win, net_wm_desk, XA_CARDINAL, &cur_desk, 1);
        c->desk = cur_desk;
    }

    c->name = win_name_get(c->win);
    return c;
}

void cli_withdraw(Client *c)
{
    cli_state_set(c, WithdrawnState);
    IF_DEBUG(cli_print(c, "<wdr>"));
    cli_free(c);
    win_list_update();
}

void cli_free(Client *c)
{
    destroying = c->win;

    XSetWindowBorderWidth(dpy, c->win, 1);
    XReparentWindow(dpy, c->win, root, c->geom.x, c->geom.y);
    XRemoveFromSaveSet(dpy, c->win);
#ifdef XFT
    if (c->xftdraw) XftDrawDestroy(c->xftdraw);
#endif
    XDestroyWindow(dpy, c->frame);

    atom_del(root, net_client_list, XA_WINDOW, c->win);
    XDeleteContext(dpy, c->win, cli_tab);
    XDeleteContext(dpy, c->frame, frame_tab);

    if (c->name) XFree(c->name);
    free(c);
}

void cli_map(Client *c)
{
    XWindowAttributes attr;
    XWMHints *hints;

    cli_state_apply(c);
    XGetWindowAttributes(dpy, c->win, &attr);

    if (attr.map_state == IsViewable) {
        if (win_state_get(c->win) == WithdrawnState)
            cli_state_set(c, NormalState);
        cli_reparent(c);
    } else {
        cli_state_set(c, NormalState);
        if ((hints = XGetWMHints(dpy, c->win))) {
            if (hints->flags & StateHint)
                cli_state_set(c, hints->initial_state);
            XFree(hints);
        }
        cli_reparent(c);
        if (!cli_geom_init(c) && opt_mt) {
            if (opt_mt > 0) {
                sig_set(SIGALRM, sig_handle);
                alarm(opt_mt);
            }
            cli_sweep(c, crs_move, calc_move);
        }
    }

    IF_DEBUG(cli_print(c, "<map>"));
    cli_map_apply(c);
    atom_add(root, net_client_list, XA_WINDOW, &c->win, 1);
}

/* When we're ready to map, we have two things to consider: the literal
 * geometry of the window (what the client passed to XCreateWindow), and the
 * size hints (what they set with XSetWMSizeHints, if anything). Generally,
 * the client doesn't care, and leaves the literal geometry at +0+0. If the
 * client wants to be mapped in a particular place, though, they either set
 * this geometry to something different or set a size hint. The size hint
 * is the recommended method, and takes precedence. If there is already
 * something in c->geom, though, we just leave it.  */

static Bool cli_geom_init(Client *c)
{
    Atom state;
    long px, py;
    Brace b = desk_braces_sum(c->desk);
    Geom f;

    /* We decide the geometry for these types of windows, so we can just
     * ignore everything and return right away. If c->zoomed is set, that
     * means we've already set things up, but otherwise, we do it here. */
    if (c->zoomed)
        return True;
    if (atom_get(c->win, net_wm_state, XA_ATOM, 0, &state, 1, NULL) &&
            state == net_wm_state_fs) {
        c->geom.x = 0;
        c->geom.y = 0;
        c->geom.w = rw;
        c->geom.h = rh;
        return True;
    }

    /* Here, we merely set the values; they're in the same place regardless
     * of whether the user or the program specified them. We'll distinguish
     * between the two cases later, if we need to. */
    if (c->size.flags & (USSize|PSize)) {
        if (c->size.width > 0) c->geom.w = c->size.width;
        if (c->size.height > 0) c->geom.h = c->size.height;
    }
    if (c->size.flags & (USPosition|PPosition)) {
        if (c->size.x > 0) c->geom.x = c->size.x;
        if (c->size.y > 0) c->geom.y = c->size.y;
    }

    /* No need to go further if it's not decorated */

    if (!c->decor)
        return True;

    /* At this point, maybe nothing was set, or something went horribly wrong
     * and the values are garbage. So, try to center it on the pointer. */
    cli_geom_fixup(c);
    pointer_get(&px, &py);
    if (c->geom.x <= 0) c->geom.x = px - c->geom.w / 2;
    if (c->geom.y <= 0) c->geom.y = py - c->geom.h / 2;

    /* In any case, if we got this far, we need to do a further sanity check
     * and make sure that the window isn't overlapping any braces. */
    f = cli_frame_geom(c, c->geom);
    if (R(f, c) > b.r) c->geom.x -= R(f, c) - b.r;
    if (B(f, c) > b.b) c->geom.y -= B(f, c) - b.b;
    f = cli_frame_geom(c, c->geom);
    if (L(f, c) < b.l) c->geom.x += b.l - L(f, c);
    if (T(f, c) < b.t) c->geom.y += b.t - T(f, c);

    /* Finally, we decide if we were ultimately satisfied with the position
     * given, or if we had to make something up, so that the caller can
     * consider using some other method. */
    return c->trans || c->size.flags & USPosition;
}

/* The frame window is not created until we actually do the reparenting here,
 * and thus the Xft surface cannot exist until this runs. Anything that has to
 * manipulate the client before we are called must make sure not to attempt to
 * use either. */

static void cli_reparent(Client *c)
{
    XSetWindowAttributes pattr, cattr;
    Geom f = cli_frame_geom(c, c->geom);

    pattr.override_redirect = True;
    pattr.background_pixel = bg.pixel;
    pattr.border_pixel = bd.pixel;
    pattr.event_mask = SUB_MASK|BTN_MASK|FRAME_MASK;
    pattr.cursor = crs_frame;
    c->frame = XCreateWindow(dpy, root, 0, 0, f.w, f.h, BW(c),
        DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWEventMask|CWCursor,
        &pattr);

    cattr.cursor = crs_win;
    XChangeWindowAttributes(dpy, c->win, CWCursor, &cattr);

#ifdef SHAPE
    if (shape) {
        XShapeSelectInput(dpy, c->win, ShapeNotifyMask);
        cli_shape_set(c);
    }
#endif

#ifdef XFT
    c->xftdraw = XftDrawCreate(dpy, c->frame, DefaultVisual(dpy,
        DefaultScreen(dpy)), DefaultColormap(dpy, DefaultScreen(dpy)));
#endif

    XSaveContext(dpy, c->frame, frame_tab, (XPointer)c);
    XAddToSaveSet(dpy, c->win);
    XSetWindowBorderWidth(dpy, c->win, 0);
    XReparentWindow(dpy, c->win, c->frame, CX(c), CY(c));
    XMapWindow(dpy, c->win);
    XSelectInput(dpy, c->win, PropertyChangeMask);
}

void cli_map_apply(Client *c)
{
    Geom f = cli_frame_geom(c, c->geom);

    if (!c->frame) return;

    XMoveResizeWindow(dpy, c->frame, f.x, f.y, f.w, f.h);
    XMoveResizeWindow(dpy, c->win, CX(c), CY(c), c->geom.w, c->geom.h);
    cli_send_cfg(c);

    if (CLI_ON_CUR_DESK(c) && win_state_get(c->win) == NormalState)
        cli_show(c);
    else
        cli_hide(c);
}

int cli_state_set(Client *c, unsigned long state)
{
    return atom_set(c->win, wm_state, wm_state, &state, 1);
}

void cli_state_apply(Client *c)
{
    Atom state;
    unsigned long na, nr;
    int i;

    for (i = 0, nr = 1; nr; i += na) {
        na = atom_get(c->win, net_wm_state, XA_ATOM, i, &state, 1, &nr);
        if (na) {
            if (state == net_wm_state_shaded)
                cli_shade(c);
            else if (state == net_wm_state_mh || state == net_wm_state_mv)
                cli_grow(c);
        } else {
            break;
        }
    }
}

/* If we frob the geom for some reason, we need to inform the client. */

void cli_send_cfg(Client *c)
{
    XConfigureEvent ce;

    ce.type = ConfigureNotify;
    ce.event = c->win;
    ce.window = c->win;
    ce.x = c->geom.x;
    ce.y = c->geom.y;
    ce.width = c->geom.w;
    ce.height = c->geom.h;
    ce.border_width = 0;
    ce.above = None;
    ce.override_redirect = 0;
    XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

/* I've changed this to just clear the window every time. The amount of
 * ``flicker'' is basically imperceptable. Also, we might be drawing an
 * anti-aliased font with Xft, in which case we always have to clear to draw
 * the text properly. This allows us to simplify ev_prop_change as
 * well.
 *
 * Unfortunately some fussing with pixels is always necessary. The integer
 * division here should match X's line algorithms so that proportions are
 * correct at all border widths. For text, I have subjectively chosen a
 * horizontal space of 1/2 the descender. Vertically, the decender is part of
 * the font; it is in addition to opt_pad. */

void cli_frame_redraw(Client *c)
{
    int x, y;

    if (c->frame && c->decor) {
        XClearWindow(dpy, c->frame);

        /* horizontal separator*/
        if (!c->shaded)
            XDrawLine(dpy, c->frame, bord_gc,
                0, GH(c) - BW(c) + BW(c)/2,
                c->geom.w, GH(c) - BW(c) + BW(c)/2);

        /* box */
        XDrawLine(dpy, c->frame, bord_gc,
            c->geom.w - GH(c) + BW(c)/2, 0,
            c->geom.w - GH(c) + BW(c)/2, GH(c));

        if (c->name && !c->trans) {
            x = opt_pad + DESCENT/2;
            y = opt_pad + ASCENT;
#ifdef XFT
#ifdef X_HAVE_UTF8_STRING
            XftDrawStringUtf8(c->xftdraw, &xft_fg, xftfont, x, y,
                (unsigned char *)c->name, strlen(c->name));
#else
            XftDrawString8(c->xftdraw, &xft_fg, xftfont, x, y,
                (unsigned char *)c->name, strlen(c->name));
#endif
#else
#ifdef X_HAVE_UTF8_STRING
            Xutf8DrawString(dpy, c->frame, font_set, text_gc, x, y,
                c->name, strlen(c->name));
#else
            XDrawString(dpy, c->frame, text_gc, x, y,
                c->name, strlen(c->name));
#endif
#endif
        }
    }
}

/* The frame is bigger than the client window. Which direction it extends
 * outside of the theoretical client geom is determined by the window gravity.
 * The default is NorthWest, which means that the top left corner of the frame
 * stays where the top left corner of the client window would have been, and
 * the client window moves down. For SouthEast, etc, the frame moves up. For
 * Static the client window must not move (same result as South), and for
 * Center the center point of the frame goes where the center point of the
 * unmanaged client window was. */

Geom cli_frame_geom(Client *c, Geom f)
{
    /* everything else is the same */
    f.h = GH(c) + (c->shaded ? -BW(c) : f.h);
    /* except for border compensation */
    f.x -= BW(c);
    f.y -= BW(c);

    switch (GRAV(c)) {
        case NorthWestGravity:
        case NorthGravity:
        case NorthEastGravity:
            break;
        case WestGravity:
        case CenterGravity:
        case EastGravity:
            f.y -= GH(c) / 2;
            break;
        case SouthWestGravity:
        case SouthGravity:
        case SouthEastGravity:
        case StaticGravity:
            f.y -= GH(c);
            break;
    }

    return f;
}

/*
 * This gets called when the client adds or removes shaping, and on
 * startup. XShapeGetRectangles tells us how many shapes the client has.
 * 
 * If the client window has any shapes, we need to set a shape on the
 * frame to match. It should be the union of the shaped child and the
 * grip portion of our frame. If the client has no shapes, but used to,
 * we reset the frame's shape to normal.
 */

#ifdef SHAPE
void cli_shape_set(Client *c)
{
    int n, order;
    XRectangle grip, *cli_shapes;

    cli_shapes = XShapeGetRectangles(dpy, c->win, ShapeBounding, &n, &order);
    if (n > 1) {
        XShapeCombineShape(dpy, c->frame, ShapeBounding, CX(c), CY(c), c->win,
            ShapeBounding, ShapeSet);
        grip.x = -BW(c);
        grip.y = -BW(c);
        grip.width = c->geom.w + 2 * BW(c);
        grip.height = GH(c) + BW(c);
        XShapeCombineRectangles(dpy, c->frame, ShapeBounding, 0, 0, &grip, 1,
            ShapeUnion, YXBanded);
    } else {
        XShapeCombineMask(dpy, c->frame, ShapeBounding, 0, 0, None, ShapeSet);
    }
    XFree(cli_shapes);
}
#endif

/*-
 * aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>
#include <X11/Xatom.h>
#ifdef SHAPE
#include <X11/extensions/shape.h>
#endif
#include "aewm.h"

static void ev_btn_press(XButtonEvent *);
static void ev_btn_release(XButtonEvent *);
static void ev_cfg_req(XConfigureRequestEvent *);
static void ev_circ_req(XCirculateRequestEvent *);
static void ev_map_req(XMapRequestEvent *);
static void ev_unmap(XUnmapEvent *);
static void ev_destroy(XDestroyWindowEvent *);
static void ev_message(XClientMessageEvent *);
static void ev_prop_change(XPropertyEvent *);
static void ev_enter(XCrossingEvent *);
static void ev_cmap_change(XColormapEvent *);
static void ev_expose(XExposeEvent *);
#ifdef SHAPE
static void ev_shape_change(XShapeEvent *);
#endif
static void desk_switch_to(int new_desk);

/*
 * This is a big hack to avoid blocking in Xlib, where we cannot safely
 * handle signals. We use only the nonblocking function XCheckMaskEvent
 * and do our own select() on the connection before waking up to hopefully
 * grab the event that came in.
 *
 * If some other client barfed or the X server has a bug and invalid
 * data is available on our connection, when Xlib processes the
 * outstanding data on the connection, it will tell us no events are
 * available, and we'll go back to sleep.
 *
 * If we do recieve a signal, we will wake up from select() and
 * immediately fail. The signal handler will have set flags, and our
 * caller will handle that error by aborting an operation or exiting
 * entirely. (This is the reason for this function -- Xlib will restart
 * a blocking call, leaving us unable to do anything about the signal
 * flags).
 *
 * Because Xlib sucks, we can't use a nonblocking call without a mask,
 * and so we simulate it by checking XPending before using XNextEvent
 * (which blocks).
 *
 * For obvious reasons, this really ought to call pselect(), but
 * it's not portable. Even if we did that, the signal could still be
 * delivered while we're in Xlib. Basically, we are screwed until I port
 * this to XCB. (In the meantime, I have at least never actually seen
 * this race condition happen in real life.)
 */

Bool event_get_next(long mask, XEvent *ev)
{
    int fd = ConnectionNumber(dpy);
    fd_set rd;

    while (!(mask ? XCheckMaskEvent(dpy, mask, ev)
                  : (XPending(dpy) && XNextEvent(dpy, ev) == Success))) {
        FD_ZERO(&rd);
        FD_SET(fd, &rd);
        select(fd + 1, &rd, NULL, NULL, NULL);
        if (timed_out || killed)
            return False;
    }
    return True;
}

/* By the time we get an event, there is no guarantee the window still
 * exists. Therefore ev_print might cause errors. We'll just live with it. */

void ev_loop(void)
{
    XEvent ev;

    while (event_get_next(NoEventMask, &ev)) {
        IF_DEBUG(ev_print(ev));
        switch (ev.type) {
            case ButtonPress: ev_btn_press(&ev.xbutton); break;
            case ButtonRelease: ev_btn_release(&ev.xbutton); break;
            case ConfigureRequest: ev_cfg_req(&ev.xconfigurerequest); break;
            case CirculateRequest: ev_circ_req(&ev.xcirculaterequest); break;
            case MapRequest: ev_map_req(&ev.xmaprequest); break;
            case UnmapNotify: ev_unmap(&ev.xunmap); break;
            case DestroyNotify: ev_destroy(&ev.xdestroywindow); break;
            case ClientMessage: ev_message(&ev.xclient); break;
            case ColormapNotify: ev_cmap_change(&ev.xcolormap); break;
            case PropertyNotify: ev_prop_change(&ev.xproperty); break;
            case EnterNotify: ev_enter(&ev.xcrossing); break;
            case Expose: ev_expose(&ev.xexpose); break;
#ifdef SHAPE
            default:
                if (shape && ev.type == shape_event)
                    ev_shape_change((XShapeEvent *)&ev);
                break;
#endif
        }
    }
}

/* Someone clicked a button. If they clicked on a window, we want the button
 * press, but if they clicked on the root, we're only interested in the button
 * release. Thus, two functions.
 *
 * If it was on the root, we get the click by default. If it's on a window
 * frame, we get it as well. If it's on a client window, it may still fall
 * through to us if the client doesn't select for mouse-click events. The
 * upshot of this is that you should be able to click on the blank part of a
 * GTK window with Button2 to move it. */

static void ev_btn_press(XButtonEvent *e)
{
    Client *c;

    pressed = e->window;
    if (FIND_CTX(e->window, frame_tab, &c))
        cli_pressed(c, e->x, e->y, e->button);
}

static void ev_btn_release(XButtonEvent *e)
{
    if (pressed == root && e->window == root) {
        IF_DEBUG(cli_list());
        switch (e->button) {
            case Button1: fork_exec(opt_new[0]); break;
            case Button2: fork_exec(opt_new[1]); break;
            case Button3: fork_exec(opt_new[2]); break;
            case Button4: fork_exec(opt_new[3]); break;
            case Button5: fork_exec(opt_new[4]); break;
        }
        pressed = None;
    }
}

/* Because we are redirecting the root window, we get ConfigureRequest events
 * from both clients we're handling and ones that we aren't. For clients we
 * manage, we need to adjust the frame and the client window, and for
 * unmanaged windows we have to pass along everything unchanged.
 *
 * Most of the assignments here are going to be garbage, but only the ones
 * that are masked in by e->value_mask will be looked at by the X server. */

static void ev_cfg_req(XConfigureRequestEvent *e)
{
    Client *c;
    Geom f;
    XWindowChanges wc;

    wc.x = e->x;
    wc.y = e->y;
    wc.width = e->width;
    wc.height = e->height;
    wc.sibling = e->above;
    wc.stack_mode = e->detail;

    if (FIND_CTX(e->window, cli_tab, &c)) {
        if (!c->cfg_lock) {
            if (c->zoomed && e->value_mask & (CWX|CWY|CWWidth|CWHeight)) {
                c->zoomed = False;
                atom_del(c->win, net_wm_state, XA_ATOM, net_wm_state_mv);
                atom_del(c->win, net_wm_state, XA_ATOM, net_wm_state_mh);
            }
            if (e->value_mask & CWX) c->geom.x = e->x;
            if (e->value_mask & CWY) c->geom.y = e->y;
            if (e->value_mask & CWWidth) c->geom.w = e->width;
            if (e->value_mask & CWHeight) c->geom.h = e->height;
            IF_DEBUG(cli_print(c, "<cfg>"));
        }
#ifdef SHAPE
        if (e->value_mask & (CWWidth|CWHeight)) cli_shape_set(c);
#endif
        cli_send_cfg(c);

        wc.x = CX(c);
        wc.y = CY(c);
        XConfigureWindow(dpy, e->window, e->value_mask, &wc);

        f = cli_frame_geom(c, c->geom);
        wc.x = f.x;
        wc.y = f.y;
        wc.width = f.w;
        wc.height = f.h;
        wc.border_width = BW(c);
        XConfigureWindow(dpy, c->frame, e->value_mask, &wc);
    } else {
        XConfigureWindow(dpy, e->window, e->value_mask, &wc);
    }
}

/* The only window that we will circulate children for is the root (because
 * nothing else would make sense). After a client requests that the root's
 * children be circulated, the server will determine which window needs to be
 * raised or lowered, and so all we have to do is make it so. */

static void ev_circ_req(XCirculateRequestEvent *e)
{
    if (e->parent == root) {
        if (e->place == PlaceOnBottom)
            XLowerWindow(dpy, e->window);
        else
            XRaiseWindow(dpy, e->window);
    }
}

/* Two possibilities if a client is asking to be mapped. One is that it's a new
 * window, so we handle that if it isn't in our clients list anywhere. The
 * other is that it already exists and wants to de-iconify, which is simple to
 * take care of. */

static void ev_map_req(XMapRequestEvent *e)
{
    Client *c;

    if (FIND_CTX(e->window, cli_tab, &c)) {
        cli_set_iconified(c, NormalState);
    } else {
        c = cli_new(e->window);
        cli_map(c);
        win_list_update();
    }
}

/* We don't get to intercept Unmap events, so this is post mortem. If we
 * caused the unmap ourselves earlier (explictly or by remapping), we will
 * have set c->ign_unmap. If not, time to destroy the client.
 *
 * Because most clients unmap and destroy themselves at once, they're gone
 * before we even get the Unmap event, never mind the Destroy one. Therefore
 * we must be extra careful in do_remove. */

static void ev_unmap(XUnmapEvent *e)
{
    Client *c;

    if (FIND_CTX(e->window, cli_tab, &c)) {
        if (c->ign_unmap) c->ign_unmap = False;
        else cli_withdraw(c);
    }
}

/* But a window can also go away when it's not mapped, in which case there is
 * no Unmap event. */

static void ev_destroy(XDestroyWindowEvent *e)
{
    Client *c;

    if (FIND_CTX(e->window, cli_tab, &c))
        cli_withdraw(c);
}

/* If a client wants to manipulate itself or another window it must send a
 * special kind of ClientMessage. As of right now, this only responds to the
 * ICCCM iconify message, but there are more in the EWMH that will be added
 * later. */

static void ev_message(XClientMessageEvent *e)
{
    Client *c;

    if (e->window == root) {
        if (e->message_type == net_cur_desk && e->format == 32)
            desk_switch_to(e->data.l[0]);
        else if (e->message_type == net_num_desks && e->format == 32)
            ndesks = e->data.l[0];
    } else if (FIND_CTX(e->window, cli_tab, &c)) {
        if (e->message_type == wm_change_state && e->format == 32 &&
                e->data.l[0] == IconicState) {
            cli_set_iconified(c, IconicState);
        } else if (e->message_type == net_active_window && e->format == 32) {
            c->desk = cur_desk;
            cli_set_iconified(c, NormalState);
            cli_raise(c);
        } else if (e->message_type == net_close_window && e->format == 32) {
            cli_req_close(c);
        }
    }
}

/* If we have something copied to a variable, or displayed on the screen, make
 * sure it is up to date. If redrawing the name is necessary, clear the window
 * because Xft uses alpha rendering. */

static void ev_prop_change(XPropertyEvent *e)
{
    Client *c;
    long supplied;

    if (FIND_CTX(e->window, cli_tab, &c)) {
        if (e->atom == XA_WM_NAME || e->atom == net_wm_name) {
            if (c->name) XFree(c->name);
            c->name = win_name_get(c->win);
            cli_frame_redraw(c);
        } else if (e->atom == XA_WM_NORMAL_HINTS) {
            XGetWMNormalHints(dpy, c->win, &c->size, &supplied);
        } else if (e->atom == net_wm_state) {
            cli_state_apply(c);
        } else if (e->atom == net_wm_desk) {
            if (atom_get(c->win, net_wm_desk, XA_CARDINAL, 0, &c->desk, 1,
                    NULL))
                cli_map_apply(c);
        }
    }
}

/* Lazy focus-follows-mouse and colormap-follows-mouse policy. This does not,
 * however, prevent focus stealing (it's lazy). It is not very efficient
 * either; we can get a lot of enter events at once when flipping through a
 * window stack on startup/desktop change. */

static void ev_enter(XCrossingEvent *e)
{
    Client *c;

    if (FIND_CTX(e->window, frame_tab, &c))
        cli_focus(c);
}

/* More colormap policy: when a client installs a new colormap on itself, set
 * the display's colormap to that. We do this even if it's not focused. */

static void ev_cmap_change(XColormapEvent *e)
{
    Client *c;

    if (e->new && FIND_CTX(e->window, cli_tab, &c)) {
        c->cmap = e->colormap;
        XInstallColormap(dpy, c->cmap);
    }
}

/*
 * We care about frame exposes for two reasons: redrawing the grip,
 * and deciding when a client has finished mapping.
 *
 * Before the frame is initially exposed, we ignore ConfigureRequests.
 * These are almost always from poorly-behaved clients that attempt
 * to override the user's placement. Once the frame has appeared, it
 * is generally safe to let clients move themselves. So we set a
 * flag here for that.
 *
 * We will usually get multiple events at once (for each obscured
 * region), so we don't do anything unless the count of remaining
 * exposes is 0.
 */

static void ev_expose(XExposeEvent *e)
{
    Client *c;

    if (e->count == 0 && FIND_CTX(e->window, frame_tab, &c)) {
        cli_frame_redraw(c);
        c->cfg_lock = False;
    }
}

#ifdef SHAPE
static void ev_shape_change(XShapeEvent *e)
{
    Client *c;

    if (FIND_CTX(e->window, cli_tab, &c))
        cli_shape_set(c);
}
#endif

static void desk_switch_to(int new_desk)
{
    unsigned int i;
    Client *c;

    cur_desk = new_desk;
    atom_set(root, net_cur_desk, XA_CARDINAL, &cur_desk, 1);

    for (i = 0; i < nwins; i++)
        if (FIND_CTX(wins[i], frame_tab, &c) && !CLI_ON_CUR_DESK(c))
            cli_hide(c);
    while (i--)
        if (FIND_CTX(wins[i], frame_tab, &c) && CLI_ON_CUR_DESK(c)
                && win_state_get(c->win) == NormalState)
            cli_show(c);
}

#ifdef DEBUG
void ev_print(XEvent e)
{
    Window w;
    char *n;

    switch (e.type) {
        case ButtonPress: n = "BtnPress"; w = e.xbutton.window; break;
        case ButtonRelease: n = "BtnRel"; w = e.xbutton.window; break;
        case ClientMessage: n = "CliMsg"; w = e.xclient.window; break;
        case ColormapNotify: n = "CmapNfy"; w = e.xcolormap.window; break;
        case ConfigureNotify: n = "CfgNfy"; w = e.xconfigure.window; break;
        case ConfigureRequest: n = "CfgReq"; w = e.xconfigurerequest.window;
            break;
        case CirculateRequest: n = "CircReq"; w = e.xcirculaterequest.window;
            break;
        case CreateNotify: n = "CreatNfy"; w = e.xcreatewindow.window; break;
        case DestroyNotify: n = "DestrNfy"; w = e.xdestroywindow.window; break;
        case EnterNotify: n = "EnterNfy"; w = e.xcrossing.window; break;
        case Expose: n = "Expose"; w = e.xexpose.window; break;
        case MapNotify: n = "MapNfy"; w = e.xmap.window; break;
        case MapRequest: n = "MapReq"; w = e.xmaprequest.window; break;
        case MappingNotify: n = "MapnNfy"; w = e.xmapping.window; break;
        case MotionNotify: n = "MotiNfy"; w = e.xmotion.window; break;
        case PropertyNotify: n = "PropNfy"; w = e.xproperty.window; break;
        case ReparentNotify: n = "ParNfy"; w = e.xreparent.window; break;
        case ResizeRequest: n = "ResizReq"; w = e.xresizerequest.window; break;
        case UnmapNotify: n = "UnmapNfy"; w = e.xunmap.window; break;
        default:
#ifdef SHAPE
            if (shape && e.type == shape_event) {
                n = "ShapeNfy"; w = ((XShapeEvent *)&e)->window;
                break;
            }
#endif
            n = "?"; w = None; break;
    }

    win_print(w, n);
}

const char *cli_grav_str(Client *c)
{
    if (!(c->size.flags & PWinGravity))
        return "nw";
    switch (c->size.win_gravity) {
        case UnmapGravity: return "U";
        case NorthWestGravity: return "NW";
        case NorthGravity: return "N";
        case NorthEastGravity: return "NE";
        case WestGravity: return "W";
        case CenterGravity: return "C";
        case EastGravity: return "E";
        case SouthWestGravity: return "SW";
        case SouthGravity: return "S";
        case SouthEastGravity: return "SE";
        case StaticGravity: return "X";
        default: return "?";
    }
}

const char *cli_state_str(Client *c)
{
    switch (win_state_get(c->win)) {
        case NormalState: return "Nrm";
        case IconicState: return "Ico";
        case WithdrawnState: return "Wdr";
        default: return "?";
    }
}

void cli_print(Client *c, const char *label)
{
    const char *s = cli_state_str(c);

    printf("%9.9s: %#010lx [ ] %-32.32s ", label, c->win, c->name);
    if (c->desk == DESK_ALL) printf("*");
    else printf("%ld", c->desk);
    printf(" %ldx%ld %s", c->geom.w, c->geom.h, s);
    if (c->trans) printf(" tr");
    if (c->shaded) printf(" sh");
    if (c->zoomed) printf(" zm");
    if (c->ign_unmap) printf(" ig");
    printf("\n");
}

void win_print(Window w, const char *label)
{
    Client *c;

    if (w == root)
        printf("%9.9s: %#010lx [r]\n", label, w);
    else if (FIND_CTX(w, cli_tab, &c))
        cli_print(c, label);
    else if (FIND_CTX(w, frame_tab, &c))
        printf("%9.9s: %#010lx [f] %-32.32s +%ld+%ld %s\n", label, w, c->name,
            c->geom.x, c->geom.y, cli_grav_str(c));
    else
        printf("%9.9s: %#010lx [?]\n", label, w);
}

void cli_list(void)
{
    Client *c;
    unsigned int i;

    for (i = 0; i < nwins; i++) {
        if (FIND_CTX(wins[i], frame_tab, &c)) {
            cli_print(c, "<list>");
            win_print(c->frame, "<list>");
        }
    }
}
#endif

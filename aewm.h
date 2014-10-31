/*-
 * aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.
 */

#ifndef AEWM_WM_H
#define AEWM_WM_H

#define VERSION "2.0.0pre"

#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#ifdef XFT
#include <X11/Xft/Xft.h>
#endif
#include "util.h"
#include "opts.h"

#ifdef XFT
#define XFT_USAGE "            [--xftfont|-fa <font>]\n"
#else
#define XFT_USAGE ""
#endif
#define USAGE \
    "usage: aewm [--config|-rc <file>]\n" \
    "            [--font|-fn <font>]\n" \
                 XFT_USAGE \
    "            [--fgcolor|-fg <color>]\n" \
    "            [--bgcolor|-bg <color>]\n" \
    "            [--bdcolor|-bd <color>]\n" \
    "            [--bdwidth|-bw <pixels>]\n" \
    "            [--padding|-p <pixels>]\n" \
    "            [--maptime|-mt <seconds>]\n" \
    "            [--new1|-1 <cmd>]\n" \
    "            [--new2|-2 <cmd>]\n" \
    "            [--new3|-3 <cmd>]\n" \
    "            [--new4|-4 <cmd>]\n" \
    "            [--new5|-5 <cmd>]\n" \
    "            [--help|-h]\n" \
    "            [--version|-v]\n"

#define SUB_MASK (SubstructureRedirectMask|SubstructureNotifyMask)
#define BTN_MASK (ButtonPressMask|ButtonReleaseMask)
#define KEY_MASK (KeyPressMask|KeyReleaseMask)
#define MOUSE_MASK (BTN_MASK|PointerMotionMask|PointerMotionHintMask)
#define FRAME_MASK (ExposureMask|EnterWindowMask)

#ifdef DEBUG
#define IF_DEBUG(e) e
#else
#define IF_DEBUG(e)
#endif

#define CLI_ON_CUR_DESK(c) ON_DESK((c)->desk, cur_desk)
#define FIND_CTX(w, ctx, r) (XFindContext(dpy, w, ctx, (XPointer *)r) == \
    Success)
#define GRAV(c) ((c->size.flags & PWinGravity) ? c->size.win_gravity : \
    NorthWestGravity)

/*
 * Accessors for child x/y (relative to frame) and grip height/x/y. The
 * x/y are trivial but if you want to change the child position (e.g. to
 * draw your own frame) it's good to have them abstracted out.
 */

#define IF_D(c, e) ((c)->decor ? e : 0)

#define BW(c) IF_D(c, opt_bw)
#define CX(c) 0
#define CY(c) GH(c)
#define GH(c) IF_D(c, (c->trans ? 0 : ASCENT) + DESCENT + 2*opt_pad + opt_bw)
#define GX(c) 0
#define GY(c) 0

/*
 * The outer bounds of the frame, for drawing outlines, checking if something
 * crossed a margin, etc.
 */

#define L(f, c) ((f).x)
#define R(f, c) ((f).x + (f).w + 2 * BW(c))
#define T(f, c) ((f).y)
#define B(f, c) ((f).y + (f).h + 2 * BW(c))

#ifdef XFT
#define ASCENT (xftfont->ascent)
#define DESCENT (xftfont->descent)
#else
#define ASCENT (font->ascent)
#define DESCENT (font->descent)
#endif

/*
 * A 'brace' denotes the edges of the usable desk (after accounting for
 * struts. The coordinates are from the origin, so for the trivial case
 * right and bottom are the root width and height, not zero.
 *
 * Why not strut? (a) it's easier to check against B and R, and (b)
 * 'str' is a reserved prefix in ANSI C. No, really.
 */

typedef struct { long x; long y; long w; long h; } Geom;
typedef struct { long l; long r; long t; long b; } Brace;

typedef struct {
    Window win;          /* client's window, our "child" */
    Window trans;        /* if it's transient, some other win, else None */
    Window frame;        /* our win that we reparent it into */
    char *name;          /* WM_NAME */
    XSizeHints size;     /* WM_NORMAL_HINTS */
    Colormap cmap;       /* WM_COLORMAP */
    Geom geom;           /* current geometry */
    Geom save;           /* hack to save real geometry if zoomed */
    unsigned long desk;  /* current EWMH "virtual desktop" */
#ifdef XFT
    XftDraw *xftdraw;    /* graphics context for antialiased fonts */
#endif
    Bool shaded;         /* user "rolled up", only display grip */
    Bool zoomed;         /* user expanded to full screen */
    Bool decor;          /* client wants grip and border to be drawn */
    Bool cfg_lock;       /* we don't let the client configure itself yet */
    Bool ign_unmap;      /* we unmapped child, so ignore the next unmap */
} Client;

typedef void SweepFunc(Client *, Geom *, Geom *, Brace *, Brace *);

/* aewm_init.c */
extern Window *wins, pressed, destroying;
extern XContext cli_tab, frame_tab;
extern int rw, rh;
extern unsigned int nwins;
extern unsigned long ndesks;
extern unsigned long cur_desk;
#ifdef SHAPE
extern Bool shape;
extern int shape_event;
#endif
extern XFontStruct *font;
#ifdef X_HAVE_UTF8_STRING
extern XFontSet font_set;
#endif
#ifdef XFT
extern XftFont *xftfont;
extern XftColor xft_fg;
#endif
extern Colormap def_cmap;
extern XColor fg;
extern XColor bg;
extern XColor bd;
extern GC inv_gc;
extern GC text_gc;
extern GC bord_gc;
extern Cursor crs_move;
extern Cursor crs_size;
extern Cursor crs_frame;
extern Cursor crs_win;
extern char *opt_font;
#ifdef XFT
extern char *opt_xftfont;
#endif
extern char *opt_new[];
extern char *opt_fg;
extern char *opt_bg;
extern char *opt_bd;
extern int opt_bw;
extern int opt_pad;
extern int opt_mt;
extern sig_atomic_t timed_out;
extern sig_atomic_t killed;
extern int sig_set(int signum, void (*handler)(int));
extern void sig_handle(int signum);
extern int err_handle(Display *d, XErrorEvent *e);
extern void win_list_update(void);
extern int pointer_get(long *x, long *y);
extern Brace desk_braces_sum(unsigned long desk);
/* aewm_client.c */
extern Client *cli_new(Window w);
extern void cli_withdraw(Client *c);
extern void cli_free(Client *c);
extern void cli_map(Client *c);
extern void cli_map_apply(Client *c);
extern int cli_state_set(Client *c, unsigned long state);
extern void cli_state_apply(Client *c);
extern void cli_send_cfg(Client *c);
extern void cli_frame_redraw(Client *c);
extern Geom cli_frame_geom(Client *c, Geom f);
#ifdef SHAPE
extern void cli_shape_set(Client *c);
#endif
/* aewm_event.c */
extern Bool event_get_next(long mask, XEvent *ev);
extern void ev_loop(void);
#ifdef DEBUG
extern void ev_print(XEvent e);
extern const char *cli_grav_str(Client *c);
extern const char *cli_state_str(Client *c);
extern void cli_print(Client *c, const char *label);
extern void win_print(Window w, const char *label);
extern void cli_list(void);
#endif
/* aewm_manip.c */
extern void cli_pressed(Client *c, int x, int y, int button);
extern void cli_raise(Client *c);
extern void cli_lower(Client *c);
extern void cli_show(Client *c);
extern void cli_hide(Client *c);
extern void cli_focus(Client *c);
extern void cli_move(Client *c);
extern void cli_resize(Client *c);
extern void cli_set_iconified(Client *c, long state);
extern void cli_shade(Client *c);
extern void cli_unshade(Client *c);
extern void cli_grow(Client *c);
extern void cli_shrink(Client *c);
extern void cli_req_close(Client *c);
extern void cli_sweep(Client *c, Cursor curs, SweepFunc cb);
extern void calc_move(Client *c, Geom *orig, Geom *m, Brace *b, Brace *h);
extern void calc_resize(Client *c, Geom *orig, Geom *m, Brace *b, Brace *h);
extern Geom cli_geom_fixup(Client *c);
#endif /* AEWM_WM_H */

/*-
 * aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <sys/wait.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#ifdef SHAPE
#include <X11/extensions/shape.h>
#endif
#include "aewm.h"

Window *wins, pressed = None, destroying = None;
XContext cli_tab, frame_tab;
int rw, rh;
unsigned int nwins = 0;
unsigned long ndesks = 1;
unsigned long cur_desk = 0;
#ifdef SHAPE
Bool shape;
int shape_event;
#endif
XFontStruct *font;
#ifdef X_HAVE_UTF8_STRING
XFontSet font_set;
#endif
#ifdef XFT
XftFont *xftfont;
XftColor xft_fg;
#endif
Colormap def_cmap;
XColor fg;
XColor bg;
XColor bd;
GC inv_gc;
GC text_gc;
GC bord_gc;
Cursor crs_move;
Cursor crs_size;
Cursor crs_frame;
Cursor crs_win;
char *opt_font = DEF_FONT;
#ifdef XFT
char *opt_xftfont = DEF_XFTFONT;
#endif
char *opt_new[] = {DEF_NEW1, DEF_NEW2, DEF_NEW3, DEF_NEW4, DEF_NEW5};
char *opt_fg = DEF_FG;
char *opt_bg = DEF_BG;
char *opt_bd = DEF_BD;
int opt_bw = DEF_BW;
int opt_pad = DEF_PAD;
int opt_mt = DEF_MT;
sig_atomic_t timed_out = 0;
sig_atomic_t killed = 0;

static void conf_read(char *);
static void dpy_init(void);
static void shutdown(void);
static Brace win_brace_get(Window w);

int main(int argc, char **argv)
{
    int i;

    setlocale(LC_ALL, "");
    conf_read(NULL);

    for (i = 1; i < argc; i++) {
        if ARG("font", "fn", 1) opt_font = argv[++i];
#ifdef XFT
        else if ARG("xftfont", "fa", 1) opt_xftfont = argv[++i];
#endif
        else if ARG("fgcolor", "fg", 1) opt_fg = argv[++i];
        else if ARG("bgcolor", "bg", 1) opt_bg = argv[++i];
        else if ARG("bdcolor", "bd", 1) opt_bd = argv[++i];
        else if ARG("bdwidth", "bw", 1) opt_bw = atoi(argv[++i]);
        else if ARG("padding", "p", 1) opt_pad = atoi(argv[++i]);
        else if ARG("maptime", "mt", 1) opt_mt = atoi(argv[++i]);
        else if ARG("new1", "1", 1) opt_new[0] = argv[++i];
        else if ARG("new2", "2", 1) opt_new[1] = argv[++i];
        else if ARG("new3", "3", 1) opt_new[2] = argv[++i];
        else if ARG("new4", "4", 1) opt_new[3] = argv[++i];
        else if ARG("new5", "5", 1) opt_new[4] = argv[++i];
        else if ARG("config", "rc", 1) conf_read(argv[++i]);
        else if ARG("version", "v",0) {
            printf("aewm: version " VERSION "\n");
            exit(0);
        } else if ARG("help", "h",0) {
            printf(USAGE);
            exit(0);
        } else {
            fprintf(stderr, "aewm: unknown option: '%s'\n" USAGE, argv[i]);
            exit(2);
        }
    }

    sig_set(SIGTERM, sig_handle);
    sig_set(SIGINT, sig_handle);
    sig_set(SIGHUP, sig_handle);
    sig_set(SIGCHLD, sig_handle);

    dpy_init();
    ev_loop();
    shutdown();
    return 0;
}

static void conf_read(char *rcfile)
{
    FILE *rc;
    char buf[BUF_SIZE], token[BUF_SIZE], *p;

    if (!(rc = rc_open(rcfile, "aewmrc"))) {
        if (rcfile) fprintf(stderr, "aewm: rc file '%s' not found\n", rcfile);
        return;
    }

    while (rc_getl(buf, sizeof buf, rc)) {
        p = buf;
        while (tok_next(&p, token)) {
            if (RC_OPT("font")) opt_font = strdup(token);
#ifdef XFT
            else if (RC_OPT("xftfont")) opt_xftfont = strdup(token);
#endif
            else if (RC_OPT("fgcolor")) opt_fg = strdup(token);
            else if (RC_OPT("bgcolor")) opt_bg = strdup(token);
            else if (RC_OPT("bdcolor")) opt_bd = strdup(token);
            else if (RC_OPT("bdwidth")) opt_bw = atoi(token);
            else if (RC_OPT("padding")) opt_pad = atoi(token);
            else if (RC_OPT("maptime")) opt_mt = atoi(token);
            else if (RC_OPT("button1")) opt_new[0] = strdup(token);
            else if (RC_OPT("button2")) opt_new[1] = strdup(token);
            else if (RC_OPT("button3")) opt_new[2] = strdup(token);
            else if (RC_OPT("button4")) opt_new[3] = strdup(token);
            else if (RC_OPT("button5")) opt_new[4] = strdup(token);
        }
    }
    fclose(rc);
}

static void dpy_init(void)
{
#ifdef X_HAVE_UTF8_STRING
    char **missing;
    char *def_str;
    int nmissing;
#endif
    XGCValues gv;
    XColor exact;
    XWindowAttributes attr;
    XSetWindowAttributes sattr;
#ifdef SHAPE
    int shape_err;
#endif
    unsigned int i;
    Client *c;

    if (!(dpy = XOpenDisplay(NULL))) {
        fprintf(stderr, "aewm: can't open display %s\n", getenv("DISPLAY"));
        exit(1);
    }

    XSetErrorHandler(err_handle);
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    rw = DisplayWidth(dpy, screen);
    rh = DisplayHeight(dpy, screen);
    pressed = None;
    cli_tab = XUniqueContext();
    frame_tab = XUniqueContext();

    crs_move = XCreateFontCursor(dpy, XC_fleur);
    crs_size = XCreateFontCursor(dpy, XC_sizing);
    crs_frame = XCreateFontCursor(dpy, XC_right_ptr);
    crs_win = XCreateFontCursor(dpy, XC_left_ptr);

    def_cmap = DefaultColormap(dpy, screen);
    XAllocNamedColor(dpy, def_cmap, opt_fg, &fg, &exact);
    XAllocNamedColor(dpy, def_cmap, opt_bg, &bg, &exact);
    XAllocNamedColor(dpy, def_cmap, opt_bd, &bd, &exact);

    if (!(font = XLoadQueryFont(dpy, opt_font))) {
        fprintf(stderr, "aewm: font '%s' not found\n", opt_font);
        exit(1);
    }
#ifdef X_HAVE_UTF8_STRING
    font_set = XCreateFontSet(dpy, opt_font, &missing, &nmissing, &def_str);
#endif

#ifdef XFT
    xft_fg.color.red = fg.red;
    xft_fg.color.green = fg.green;
    xft_fg.color.blue = fg.blue;
    xft_fg.color.alpha = 0xffff;
    xft_fg.pixel = fg.pixel;

    if (!(xftfont = XftFontOpenName(dpy, DefaultScreen(dpy), opt_xftfont))) {
        fprintf(stderr, "aewm: Xft font '%s' not found\n", opt_font);
        exit(1);
    }
#endif

#ifdef SHAPE
    shape = XShapeQueryExtension(dpy, &shape_event, &shape_err);
#endif

    gv.function = GXcopy;
    gv.foreground = fg.pixel;
    gv.font = font->fid;
    text_gc = XCreateGC(dpy, root, GCFunction|GCForeground|GCFont, &gv);

    gv.foreground = bd.pixel;
    gv.line_width = opt_bw;
    bord_gc = XCreateGC(dpy, root, GCFunction|GCForeground|GCLineWidth, &gv);

    gv.function = GXinvert;
    gv.subwindow_mode = IncludeInferiors;
    inv_gc = XCreateGC(dpy, root,
        GCFunction|GCSubwindowMode|GCLineWidth|GCFont, &gv);

    utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
    wm_protos = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wm_state = XInternAtom(dpy, "WM_STATE", False);
    wm_change_state = XInternAtom(dpy, "WM_CHANGE_STATE", False);
    net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
    net_cur_desk = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    net_num_desks = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    net_client_stack = XInternAtom(dpy, "_NET_CLIENT_LIST_STACKING", False);
    net_active_window = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    net_close_window = XInternAtom(dpy, "_NET_CLOSE_WINDOW", False);
    net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    net_wm_desk = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
    net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    net_wm_state_shaded = XInternAtom(dpy, "_NET_WM_STATE_SHADED", False);
    net_wm_state_mv = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    net_wm_state_mh = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    net_wm_state_fs = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    net_wm_strut = XInternAtom(dpy, "_NET_WM_STRUT", False);
    net_wm_strut_partial = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
    net_wm_wintype = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    net_wm_type_desk = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    net_wm_type_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    net_wm_type_menu = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
    net_wm_type_splash = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH",
        False);

    atom_add(root, net_supported, XA_ATOM, &net_cur_desk, 1);
    atom_add(root, net_supported, XA_ATOM, &net_num_desks, 1);
    atom_add(root, net_supported, XA_ATOM, &net_client_list, 1);
    atom_add(root, net_supported, XA_ATOM, &net_client_stack, 1);
    atom_add(root, net_supported, XA_ATOM, &net_active_window, 1);
    atom_add(root, net_supported, XA_ATOM, &net_close_window, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_name, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_desk, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_state, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_state_shaded, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_state_mv, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_state_mh, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_state_fs, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_strut, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_strut_partial, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_wintype, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_type_dock, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_type_menu, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_type_splash, 1);
    atom_add(root, net_supported, XA_ATOM, &net_wm_type_desk, 1);

    atom_get(root, net_num_desks, XA_CARDINAL, 0, &ndesks, 1, NULL);
    atom_get(root, net_cur_desk, XA_CARDINAL, 0, &cur_desk, 1, NULL);

    win_list_update();
    for (i = 0; i < nwins; i++)
        if (XGetWindowAttributes(dpy, wins[i], &attr)
                && !attr.override_redirect && attr.map_state == IsViewable)
            cli_new(wins[i]);
    for (i = 0; i < nwins; i++)
        if (FIND_CTX(wins[i], cli_tab, &c))
            cli_map(c);
    win_list_update();

    sattr.event_mask = SUB_MASK|ColormapChangeMask|BTN_MASK|KEY_MASK;
    XChangeWindowAttributes(dpy, root, CWEventMask, &sattr);
}

static void shutdown(void)
{
    Client *c;
    unsigned int i;

    for (i = 0; i < nwins; i++) {
        if (FIND_CTX(wins[i], frame_tab, &c)) {
            IF_DEBUG(cli_print(c, "<exit>"));
            if (c->zoomed) {
                c->geom = c->save;
                XResizeWindow(dpy, c->win, c->geom.w, c->geom.h);
            }
            XMapWindow(dpy, c->win);
            cli_free(c);
        }
    }

    XFree(wins);
    XFreeFont(dpy, font);
#ifdef X_HAVE_UTF8_STRING
    XFreeFontSet(dpy, font_set);
#endif
#ifdef XFT
    XftFontClose(dpy, xftfont);
#endif
    XFreeCursor(dpy, crs_move);
    XFreeCursor(dpy, crs_size);
    XFreeCursor(dpy, crs_frame);
    XFreeCursor(dpy, crs_win);
    XFreeGC(dpy, inv_gc);
    XFreeGC(dpy, bord_gc);
    XFreeGC(dpy, text_gc);

    XInstallColormap(dpy, DefaultColormap(dpy, screen));
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);

    XDeleteProperty(dpy, root, net_supported);
    XDeleteProperty(dpy, root, net_client_list);
    XDeleteProperty(dpy, root, net_client_stack);

    XCloseDisplay(dpy);
    exit(0);
}

int sig_set(int signum, void (*handler)(int))
{
    struct sigaction act;

    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    return sigaction(signum, &act, NULL);
}

void sig_handle(int signum)
{
    switch (signum) {
        case SIGALRM:
            timed_out = 1;
            break;
        case SIGINT:
        case SIGTERM:
        case SIGHUP:
            killed = 1;
            break;
        case SIGCHLD:
            wait(NULL);
            break;
    }
}

int err_handle(Display *dpy, XErrorEvent *e)
{
    char msg[BUF_SMALL];

    if (e->resourceid != destroying) {
        XGetErrorText(dpy, e->error_code, msg, sizeof msg);
        fprintf(stderr, "aewm: X error (%#lx): %s\n", e->resourceid, msg);
    }
    return 0;
}

void win_list_update(void)
{
    unsigned int i, j = 0;
    Window qroot, qparent, *cwins;
    Client *c;

    if (wins) XFree(wins);
    XQueryTree(dpy, root, &qroot, &qparent, &wins, &nwins);

    if ((cwins = malloc(nwins * sizeof *c))) {
        for (i = 0; i < nwins; i++)
            if (FIND_CTX(wins[i], frame_tab, &c))
                cwins[j++] = c->win;
        atom_set(root, net_client_stack, XA_WINDOW, cwins, j);
        free(cwins);
    }
}

int pointer_get(long *x, long *y)
{
    Window real_root, real_w;
    int rx, ry, wx, wy;
    unsigned int mask;

    XQueryPointer(dpy, root, &real_root, &real_w, &rx, &ry, &wx, &wy, &mask);
    *x = rx;
    *y = ry;
    return mask;
}

/* This is tricky in that we might be called during startup when wins points
 * to the client toplevels, which are saved in cli_tab. When we're already
 * running, wins gives us the frames. It's a bit of a hack. */

Brace desk_braces_sum(unsigned long desk)
{
    Brace b, tmp;
    Client *c;
    XWindowAttributes attr;
    unsigned int i;

    b = win_brace_get(root);

    for (i = 0; i < nwins; i++) {
        if ((FIND_CTX(wins[i], frame_tab, &c) ||
                FIND_CTX(wins[i], cli_tab, &c)) && ON_DESK(c->desk, desk)) {
            XGetWindowAttributes(dpy, c->win, &attr);
            if (attr.map_state == IsViewable) {
                tmp = win_brace_get(c->win);
                if (tmp.l && tmp.l > b.l) b.l = tmp.l;
                if (tmp.r && tmp.r < b.r) b.r = tmp.r;
                if (tmp.t && tmp.t > b.t) b.t = tmp.t;
                if (tmp.b && tmp.b < b.b) b.b = tmp.b;
            }
        }
    }
    return b;
}

/* Reads the _NET_WM_STRUT_PARTIAL or _NET_WM_STRUT hint and returns a
 * "brace", which is the information we really want -- coordinates for
 * each boundary, rather than distances from the screen edge.
 *
 * In the case of _NET_WM_STRUT_PARTIAL we cheat and only take the first
 * 4 values, because that's all we care about. This means we can use the
 * same code for both (_NET_WM_STRUT only specifies 4 elements). Each
 * number is the margin in pixels on that side of the display where we
 * don't want to place clients. If there is no hint, we act as if it was
 * all zeros (no margin). */

static Brace win_brace_get(Window w)
{
    Atom real_type;
    int real_format = 0;
    unsigned long items_read = 0;
    unsigned long bytes_left = 0;
    unsigned char *data;
    Brace b = {0, rw, 0, rh};

    XGetWindowProperty(dpy, w, net_wm_strut_partial, 0, 12, False,
        XA_CARDINAL, &real_type, &real_format, &items_read, &bytes_left,
        &data);

    if (!(real_format == 32 && items_read >= 12))
        XGetWindowProperty(dpy, w, net_wm_strut, 0, 4, False, XA_CARDINAL,
            &real_type, &real_format, &items_read, &bytes_left, &data);

    if (real_format == 32 && items_read >= 4) {
        b.l = ((unsigned long *)data)[0];
        b.r = rw - ((unsigned long *)data)[1];
        b.t = ((unsigned long *)data)[2];
        b.b = rh - ((unsigned long *)data)[3];
        XFree(data);
    }

    return b;
}

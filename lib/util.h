/*-
 * aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.
 */

#ifndef AEWM_UTIL_H
#define AEWM_UTIL_H

#include <X11/Xlib.h>
#include <stdio.h>

#define SYS_RC_DIR "/etc/X11/aewm"
#define DESK_ALL 0xFFFFFFFF
#define ON_DESK(a, b) (a == b || a == DESK_ALL)
#define BUF_SIZE 2048
#define BUF_SMALL 256
#define ARG(long, short, nargs) ((strcmp(argv[i], "--" long) == 0 || \
    strcmp(argv[i], "-" short) == 0) && i + (nargs) < argc)
#define RC_OPT(name) (strcmp(token, name) == 0 && tok_next(&p, token))

extern Display *dpy;
extern Window root;
extern int screen;
extern Atom utf8_string;
extern Atom wm_state;
extern Atom wm_change_state;
extern Atom wm_protos;
extern Atom wm_delete;
extern Atom net_supported;
extern Atom net_client_list;
extern Atom net_client_stack;
extern Atom net_active_window;
extern Atom net_close_window;
extern Atom net_cur_desk;
extern Atom net_num_desks;
extern Atom net_wm_name;
extern Atom net_wm_desk;
extern Atom net_wm_state;
extern Atom net_wm_state_shaded;
extern Atom net_wm_state_mv;
extern Atom net_wm_state_mh;
extern Atom net_wm_state_fs;
extern Atom net_wm_state_skipt;
extern Atom net_wm_state_skipp;
extern Atom net_wm_strut;
extern Atom net_wm_strut_partial;
extern Atom net_wm_wintype;
extern Atom net_wm_type_desk;
extern Atom net_wm_type_dock;
extern Atom net_wm_type_menu;
extern Atom net_wm_type_splash;

extern void fork_exec(char *);
extern FILE *rc_open(const char *, const char *);
extern char *rc_getl(char *, int, FILE *);
extern int tok_next(char **, char *);
extern int win_send_msg(Window, Atom, unsigned long, unsigned long);
extern unsigned long atom_get(Window, Atom, Atom, unsigned long,
    unsigned long *, unsigned long, unsigned long *);
extern unsigned long atom_set(Window, Atom, Atom, unsigned long *,
    unsigned long);
extern unsigned long atom_add(Window, Atom, Atom, unsigned long *,
    unsigned long);
extern void atom_del(Window, Atom, Atom, unsigned long);
extern char *win_name_get(Window);
extern unsigned long win_state_get(Window);

#endif /* AEWM_UTIL_H */

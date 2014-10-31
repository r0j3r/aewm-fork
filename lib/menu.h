/*-
 * aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.
 */

#ifndef AEWM_MENU_H
#define AEWM_MENU_H

enum { LAUNCH, SWITCH };

typedef void *(*item_func)(void *, char *, char *);

extern void switch_atoms_setup();
extern void snprint_wm_name(char *, size_t, Window);
extern int win_on_cur_desk(Window);
extern int win_should_skip(Window);
extern void win_raise(Window);
extern void menu_make_cmd(char *, void *, item_func);

#endif /* AEWM_MENU_H */

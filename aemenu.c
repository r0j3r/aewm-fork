/*-
 * aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.
 */

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <X11/Xatom.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include "util.h"
#include "menu.h"

void *item_make_cmd_cb(void *, char *, char *);
void item_make_cli(GtkWidget *, Window);
void fork_exec_cb(GtkWidget *, char *);
void win_raise_cb(GtkWidget *, Window);

int main(int argc, char **argv)
{
    GtkWidget *main_menu;
    int i, mode = LAUNCH;
    char *opt_config = NULL;
    unsigned long na, nr;
    Window w;

    setlocale(LC_ALL, "");
    gtk_init(&argc, &argv);

    for (i = 1; i < argc; i++) {
        if ARG("config", "rc", 1)  {
            opt_config = argv[++i];
        } else if ARG("launch", "l", 0)   {
            mode = LAUNCH;
        } else if ARG("switch", "s", 0)   {
            mode = SWITCH;
        } else {
            fprintf(stderr,
                "usage: aemenu [--switch|-s] [--config|-rc <file>]\n");
            exit(2);
        }
    }

    main_menu = gtk_menu_new();

    if (mode == LAUNCH) {
        menu_make_cmd(opt_config, main_menu, item_make_cmd_cb);
    } else /* mode == SWITCH */ {
        dpy = GDK_DISPLAY();
        root = GDK_ROOT_WINDOW();
        switch_atoms_setup();
        for (i = 0, nr = 1; nr; i += na) {
            na = atom_get(root, net_client_list, XA_WINDOW, i,
                &w, 1, &nr);
            if (na)
                item_make_cli(main_menu, w);
            else
                break;
        }
    }

    gtk_signal_connect_object(GTK_OBJECT(main_menu), "deactivate",
        GTK_SIGNAL_FUNC(gtk_main_quit), NULL);
    gtk_menu_popup(GTK_MENU(main_menu), NULL, NULL, NULL, NULL, 0, 0);

    gtk_main();
    return 0;
}

void *item_make_cmd_cb(void *menu, char *label, char *cmd)
{
    GtkWidget *item, *sub_menu = NULL;

    item = gtk_menu_item_new_with_label(label);
    gtk_menu_append(GTK_MENU(menu), item);

    if (cmd) {
        gtk_signal_connect(GTK_OBJECT(item), "activate",
            GTK_SIGNAL_FUNC(fork_exec_cb), cmd);
    } else {
        sub_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub_menu);
    }

    gtk_widget_show(item);
    return sub_menu;
}

void item_make_cli(GtkWidget *menu, Window w)
{
    GtkWidget *item;
    char buf[BUF_SIZE];

    if (win_on_cur_desk(w) && !win_should_skip(w)) {
        snprint_wm_name(buf, sizeof buf, w);
        item = gtk_menu_item_new_with_label(buf);
        gtk_menu_append(GTK_MENU(menu), item);
        gtk_signal_connect(GTK_OBJECT(item), "activate",
            GTK_SIGNAL_FUNC(win_raise_cb), (gpointer)w);
        gtk_widget_show(item);
    }
}

void fork_exec_cb(GtkWidget *widget, char *data)
{
    fork_exec(data);
    gtk_main_quit();
}

void win_raise_cb(GtkWidget *widget, Window w)
{
    win_raise(w);
    gtk_main_quit();
}

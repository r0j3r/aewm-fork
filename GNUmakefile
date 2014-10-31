# aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.

include lib/rules.mk
include opts.mk

%.o: %.c
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(X11OBJ): %.o: %.c
	$(CC) $(CFLAGS) $(INC) $(X11FLAGS) -c $< -o $@

$(WMOBJ): %.o: %.c
	$(CC) $(CFLAGS) $(INC) $(WMFLAGS) -c $< -o $@

$(GTKOBJ): %.o: %.c
	$(CC) $(CFLAGS) $(INC) $(GTKFLAGS) -c $< -o $@

$(PLAINBIN):
	$(CC) $^ -o $@

$(X11BIN):
	$(CC) $(X11LIB) $^ -o $@

$(WMBIN):
	$(CC) $(WMLIB) $^ -o $@

$(GTKBIN):
	$(CC) $(GTKLIB) $^ -o $@

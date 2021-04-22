all: install

cppflags = -D_POSIX_C_SOURCE=200809L
cppflags += -D_XOPEN_SOURCE=700
cppflags += $(CPPFLAGS)

cflags = -std=c11
cflags += -O2
cflags += -g
cflags += -Wall
cflags += -Wextra
cflags += $(CFLAGS)

ldflags = $(LDFLAGS)

ldlibs = $(LDLIBS)

clean:

distclean: clean
	rm -f install

install: install.c
	$(CC) $(cppflags) $(cflags) $(ldflags) -o $@ $< $(ldlibs)

.PHONY: all clean distclean

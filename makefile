CFLAGS ?= -Wall -Wextra -std=gnu99

ifeq ($(OS),Windows_NT)
	LDFLAGS = -lgdi32
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		LDFLAGS = -framework Cocoa
	else
		LDFLAGS = -lX11
	endif
endif

all: kterm knote kbar kwm kfile

clean:
	rm -f kterm kbar kwm knote kfile

TSM_SRC = tsm/tsm-screen.c tsm/tsm-selection.c tsm/tsm-render.c tsm/tsm-unicode.c tsm/tsm-vte.c tsm/tsm-vte-charsets.c

kterm: term.c fenster.h $(TSM_SRC)
	$(CC) term.c $(TSM_SRC) -o $@ $(CFLAGS) $(LDFLAGS) -lutil -Itsm

knote: note.c fenster.h
	$(CC) note.c -o $@ $(CFLAGS) $(LDFLAGS)

kfile: file.c fenster.h
	$(CC) file.c -o $@ $(CFLAGS) $(LDFLAGS) -lrt

kbar: bar.c
	$(CC) bar.c -o $@ $(CFLAGS) $(LDFLAGS)

kwm: wm.c
	$(CC) wm.c -o $@ $(CFLAGS) $(LDFLAGS) -lXinerama

install: kwm kbar kterm knote kfile
	mkdir -p ~/bin
	install -m 755 kbar ~/bin/
	install -m 755 kterm ~/bin/
	install -m 755 knote ~/bin/
	install -m 755 kfile ~/bin/
	sudo install -m 755 kwm /usr/bin/

fonts:
	xxd -i fonts/newyork14.uf2 > fonts/newyork14.h
	sed -i s/newyork14_uf2/newyork/g newyork14.h
	xxd -i fonts/terminus16.uf2 > fonts/terminus16.h
	sed -i s/terminus16_uf2/terminus/g terminus16.h
	xxd -i fonts/times15.uf2 > fonts/times15.h
	sed -i s/times15_uf2/times/g times15.h
	xxd -i fonts/chicago12.uf2 > fonts/chicago12.h
	sed -i s/chicago12_uf2/chicago/g chicago12.h

x:
	Xephyr -ac -br -noreset -screen 1024x768 :1

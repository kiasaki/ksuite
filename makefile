CFLAGS ?= -Wall -Wextra -std=c99

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

all: kterm kbar kwm

clean:
	rm -f kterm kbar kwm

kterm: term.c fenster.h
	$(CC) term.c -o $@ $(CFLAGS) $(LDFLAGS)

kbar: bar.c
	$(CC) bar.c -o $@ $(CFLAGS) $(LDFLAGS)

kwm: wm.c
	$(CC) wm.c -o $@ $(CFLAGS) $(LDFLAGS) -lXinerama

install: wm bar term
	mkdir -p ~/bin
	cp kbar ~/bin/kbar
	#cp kterm ~/bin/kterm
	sudo cp kwm /usr/bin/kwm

fonts:
	xxd -i fonts/newyork14.uf2 > newyork14.h
	sed -i s/newyork14_uf2/newyork/g newyork14.h
	xxd -i fonts/terminus16.uf2 > terminus16.h
	sed -i s/terminus16_uf2/terminus/g terminus16.h
	xxd -i fonts/times15.uf2 > times15.h
	sed -i s/times15_uf2/times/g times15.h
	xxd -i fonts/chicago12.uf2 > chicago12.h
	sed -i s/chicago12_uf2/chicago/g chicago12.h

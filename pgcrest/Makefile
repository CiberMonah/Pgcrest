VERSION=0.1
DESTDIR=/usr/local
MANDIR=/usr/local/man

UNAME = $(shell uname)
ifeq ($(UNAME), FreeBSD)
  LDFLAGS+= -L/usr/local/lib
  CFLAGS+= -I/usr/local/include -g
  CFLAGS+= -DCONFIG_FILE=\"/etc/pgcrest.conf\"
else
  CFLAGS+= -I/usr/include/iniparser
  CFLAGS+= -DCONFIG_FILE=\"/etc/pgcrest.conf\"
endif 

LIBS= 
LIBS+= -lhttp_parser
LIBS+= -liniparser
LIBS+= $(shell pkg-config --libs libpq)
LIBS+= $(shell pkg-config --libs libuv)
LIBS+= $(shell pkg-config --libs libjwt)
LIBS+= $(shell pkg-config --libs jansson)
CFLAGS+= $(shell pkg-config --cflags libpq)
CFLAGS+= $(shell pkg-config --cflags libuv)
CFLAGS+= -fanalyzer

SRC = pgcrest.c header.c pgpool.c auth.c request.c response.c logging.c
OBJ = $(SRC:.c=.o)

all: pgcrest


pgcrest: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)


clean:
	rm -f pgcrest $(OBJ)

install: all
	install -d $(DESTDIR)/bin
	install -m 0755 pgcrest $(DESTDIR)/bin
	install -d $(MANDIR)/man1
	sed 's/VERSION/$(VERSION)/g' < pgcrest.1 >$(MANDIR)/man1/pgcrest.1
	chmod 0644 $(MANDIR)/man1/pgcrest.1

uninstall:
	rm -f $(DESTDIR)/bin/pgcrest
	rm -f $(MANDIR)/man1/pgcrest.1

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<



.PHONY: all clean uninstall

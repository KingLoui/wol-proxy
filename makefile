# Makefile fuer wolproxy unter Ubuntu

#CFLAGS = -Iinclude -Ilinux -Iunits -Iciniparser -g -fshort-enums -DUSE_ENUMS -Dunix -DBSD_SOCKETS 
CFLAGS=-g
CC = gcc
#LIBS = -Llibs -lawngclx -static

ALL = wolproxy

OBJS = wolproxy.o

all:	$(ALL)

wolproxy:  $(OBJS)
	$(CC) $(CFLAGS) -o wolproxy $(OBJS) $(LIBS)
	
install: wolproxy
	cp wolproxy /usr/bin
	cp wolproxy.init.d.pi /etc/init.d/wolproxy
	chmod +x /etc/init.d/wolproxy
	update-rc.d wolproxy defaults

depend:
	makedepend -- $(CFLAGS) $(LOBJS:.o=.c)  

clean:
	rm -f *.o *.a $(ALL) $(OBJS)

# DO NOT DELETE THIS LINE -- make depend depends on it.


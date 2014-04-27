CC = gcc
CFLAG = -Wall -Werror -std=c99 -g

IDIR = include
SRCDIR = src

_SRC = link_list.c slice.c map.c
SRC = $(patsubst %, $(SRCDIR)/%, $(_SRC))

OBJ = $(patsubst %.c, %.o, $(_SRC))

testbin: testslice testlist testmap benchmap

objs: $(SRC)
	$(CC) -I$(IDIR) $(CFLAG) -c $(SRC)
testslice: $(SRCDIR)/slice.c
	$(CC) -I$(IDIR) $(CFLAG) -DTESTSLICE $(SRCDIR)/slice.c -o testslice

testlist: $(SRCDIR)/link_list.c
	$(CC) -I$(IDIR) $(CFLAG) -DTESTLINKLIST $(SRCDIR)/link_list.c -o testlist

testmap: $(SRCDIR)/map.c objs
	$(CC) -I$(IDIR) $(CFLAG) -DTESTMAP $(SRCDIR)/map.c -c
	$(CC) -I$(IDIR) $(CFLAG) -DTESTMAP $(OBJ) -o testmap

benchmap: $(SRCDIR)/benchmap.c objs
	$(CC) -I$(IDIR) $(CFLAG) $(SRCDIR)/map.c -c
	$(CC) -I$(IDIR) $(CFLAG) $(SRCDIR)/benchmap.c -c
	$(CC) -I$(IDIR) $(CFLAG) $(OBJ) benchmap.o -o benchmap

#bintree: $(SRCDIR)/bintree.c $(SRCDIR)/link_list.c
#	$(CC) -I$(IDIR) $(CFLAG) -DTESTBINTREE $(SRCDIR)/link_list.c $(SRCDIR)/bintree.c -o bintree

.PHONY: clean test
clean:
#	@rm bintree
	@rm *.o
	@rm testslice
	@rm testlist
	@rm testmap
	@rm benchmap

test: testbin
	./testslice
	./testlist
	./testmap

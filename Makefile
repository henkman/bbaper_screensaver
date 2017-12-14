#CC=gcc
SRCS=bbaper.c
OBJS=$(SRCS:.c=.o)
CFLAGS=-O3 -fno-ident -std=c99 -fms-extensions -Wall
NAME=bbaper
LDFLAGS=-s -lyaml
ifeq ($(OS),Windows_NT)
	LDFLAGS+=-static $(shell sdl2-config --static-libs)
	EXE=$(NAME).exe
else
	LDFLAGS+=$(shell sdl2-config --libs)
	EXE=$(NAME)
endif

.PHONY: all clean
all: $(EXE)
$(EXE): $(OBJS)
	$(CC) -o $(EXE) $(OBJS) $(LDFLAGS)
.c.o:
	$(CC) -o $@ $(CFLAGS) -c $<
clean:
	rm -f $(OBJS) $(EXE)

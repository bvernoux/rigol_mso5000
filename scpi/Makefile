# Makefile
EXEC=MSO5000_SCPI

ifeq ($(OS),Windows_NT)
	CC=gcc
	LDFLAGS=-fno-exceptions -s -lws2_32
	EXEC:=$(EXEC).exe
else
	CC=gcc
	LDFLAGS=-fno-exceptions -s -lm
endif

CFLAGS=-c -Wall -O3
STRIP_EXE=strip

OBJ=socket_portable.o \
MSO5000_SCPI.o

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)
	$(STRIP_EXE) $(EXEC)

%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

clean:
	-$(RM) *.o
	-$(RM) $(EXEC)

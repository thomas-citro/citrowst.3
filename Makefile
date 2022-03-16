GCC = gcc
CFLAGS = -g -Wall -Wshadow
SRC = $(wildcard *.c)
TAR = $(SRC:.c=.o)
EXEC = $(SRC:.c=)
LOGS = $(wildcard logfile.*)

.PHONY: all clean

all: $(TAR)

%.o: %.c
	$(GCC) $(CFLAGS) -c $<
	$(GCC)  -o $* $@

clean:
	rm -f $(TAR)
	rm -f $(EXEC)
	rm -f $(LOGS)
	rm -f cstest
	rm -f ftokFile

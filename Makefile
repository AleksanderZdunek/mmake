#(C) Aleksander Zdunek <redacted>@cs.umu.se
TARGET = mmake
OBJ = 	mmake.o mmake_parser.o

CC = gcc
CFLAGS = -g -std=gnu11 -Werror -Wall -Wextra -Wpedantic -Wmissing-declarations \
	-Wmissing-prototypes -Wold-style-definition -Wswitch-enum
LDFLAGS =

all: $(TARGET)

mmake.o mmake_parser.o: mmake_parser.h
%.o: %.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f $(OBJ) $(TARGET)

RP_HOST=root@rp-f0b92e.local
DEST_DIR=/opt/redpitaya/milosar

# name of generated binary file
BIN = milosar_trigger

# must use gnueabihf
CC = gcc

# libraries
LIBS = -lm -lpthread

# header and objects directory relative to Makefile
IDIR = ./src
ODIR = ./src

# compiler flags
CFLAGS = -std=gnu99 -Wall -Werror -L -I$(IDIR)

# h files used go here
_DEPS = colour.h constants.h trigger.h utils.h reg.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

# object files used go here (with .o extension)
_OBJ = colour.o main.o trigger.o utils.o reg.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BIN): $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean copy

clean:
	rm -f $(ODIR)/*.o

copy:
	scp $(BIN) $(RP_HOST):$(DEST_DIR)


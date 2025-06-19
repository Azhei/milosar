RP_HOST=root@10.42.0.185
DEST_DIR=/opt/redpitaya/rpc

# name of generated binary file
BIN = milosar

# must use gnueabihf
CC = gcc

# libraries 
LIBS = -lm -lpthread -lgps

# header and objects directory relative to Makefile
IDIR = ./src
ODIR = ./src

# compiler flags
CFLAGS = -std=gnu99 -Wall -Werror -L -I$(IDIR)

# h files used go here
_DEPS = reg.h utils.h synth.h colour.h ini.h binary.h constants.h gps.h led.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

# object files used go here (with .o extension)
_OBJ =  reg.o utils.o synth.o colour.o ini.o binary.o main.o gps.o led.o
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
	

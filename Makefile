# ultrafetch - minimal fastfetch-style in C (Linux + Android/Termux)
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDFLAGS ?=
# Uncomment for static (optional, not always available on Termux)
# LDFLAGS += -static

SRC = src/main.c src/common.c src/os.c src/cpu.c src/gpu.c src/ram.c src/memory.c src/swap.c src/host.c src/terminalshell.c src/terminalfont.c src/uptime.c
OBJ = $(SRC:.c=.o)
INC = -Iinclude

TARGET = xfetch

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean

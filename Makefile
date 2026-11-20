# ============================================================
#  ATM Simulator — Makefile (macOS / Linux)
# ============================================================

CC      = cc
CFLAGS  = -Wall -Wextra -Wno-unused-parameter -std=c11 -I include
LDFLAGS =

SRC     = src/main.c src/ui.c src/account.c src/json.c \
          src/logger.c src/receipt.c src/png_writer.c
OBJ     = $(SRC:.c=.o)
TARGET  = atm

.PHONY: all clean run dirs

all: dirs $(TARGET)

dirs:
	@mkdir -p Receipt logs data

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

run: all
	./$(TARGET)

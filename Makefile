SRC=worm.c
OUT=worm
CC=gcc

CFLAGS=-O3 -Wall -W -Wstrict-prototypes -Werror

all: build run

build: $(OUT)

$(OUT): $(SRC)
	@echo "Compiling $(SRC)..."
	@$(CC) $(CFLAGS) $(SRC) -o $(OUT)

run: $(OUT)
	@echo "Running $(OUT)..."
	@./$(OUT)

clean:
	@echo "Cleaning build..."
	@rm -f $(OUT)
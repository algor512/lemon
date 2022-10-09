.PHONY: all
all: CFLAGS += -Os
all: lemon

.PHONY: debug
debug: CFLAGS += -O0 -DDEBUG -g
debug: lemon

.PHONY: clean
clean:
	rm -f lemon

lemon: lemon.c
	$(CC) -o lemon $(CFLAGS) lemon.c

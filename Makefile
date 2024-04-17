CFLAGS = -std=c99 -Wall -pedantic
all: modal

modal: modal.c
	$(CC) $(CFLAGS) $< -o $@

.PHONY: run clean

run: modal
	./modal example.modal

clean:
	rm -f modal
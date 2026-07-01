CC = gcc
CFLAGS = std=c11 O2 Wall Wextra
LIBS = lm

.PHONY: build run test clean

build:
	$(CC) $(CFLAGS:%=-%) -o bootstrap/hcrun bootstrap/hcrun.c $(LIBS:%=-%)

run: build
	./bootstrap/hcrun $(FILE)

test: build
	tests/run_tests.sh

clean:
	rm -f bootstrap/hcrun

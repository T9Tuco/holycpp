CC = gcc
CFLAGS = std=c11 O2 Wall Wextra
LIBS = lm

build:
	$(CC) $(CFLAGS:%=-%) -o bootstrap/hcrun bootstrap/hcrun.c $(LIBS:%=-%)

run: build
	./bootstrap/hcrun $(FILE)

test: build
	python3 tests/run_tests.py

clean:
	rm -f bootstrap/hcrun

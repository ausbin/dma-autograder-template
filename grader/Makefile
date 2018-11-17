CC = gcc
CHECK_LIBS = $(shell pkg-config --cflags --libs check)
CFILES = $(wildcard *.c suites/*.c)
HFILES = $(wildcard *.h suites/*.h)
OFILES = $(patsubst %.c,%.o,$(CFILES))

CFLAGS = -std=gnu99 -pedantic -Wall -Werror -Wextra -g \
         -Wstrict-prototypes -Wold-style-definition

.PHONY: default run-tests run-gdb clean

default: run-tests

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -c $< -o $@

tests: $(OFILES)
	$(CC) $(CFLAGS) $^ -o $@ $(CHECK_LIBS)

run-tests: tests
	./tests $(TEST)

run-gdb: tests
	CK_FORK=no gdb --args ./tests $(TEST)

# To prevent leak false postives, tell check not to fork.
# See: http://check.sourceforge.net/doc/check_html/check_4.html#Finding-Memory-Leaks
run-valgrind: tests
	CK_FORK=no valgrind --quiet --leak-check=full --error-exitcode=1 --show-leak-kinds=all --errors-for-leak-kinds=all ./tests $(TEST)

clean:
	rm -rf tests $(OFILES)

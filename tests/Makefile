# Host unit tests for ArtfulType's portable markdown core.
#
# Native cc, no cross toolchain, no dependencies. The pure core (mdcore) is
# compiled with -DAT_HOST_TEST and linked into each test_*.c as its own
# executable. Same -std=c89 as the shipping app, so the test build stays as
# portable as the code under test.
#
#   make check   # build + run all tests, non-zero exit on any failure
#   make clean
#
# See tests/README.md and the review chapter "A Testable Core".

CC     ?= cc
CFLAGS  = -std=c89 -Wall -Wextra -Wno-unused-function \
          -DAT_HOST_TEST -I. -I../app

# Each test executable and the sources it exercises.
BINS = test_smoke test_mdcore

all: check

test_smoke: test_smoke.c test_util.h
	$(CC) $(CFLAGS) -o $@ test_smoke.c

test_mdcore: test_mdcore.c ../app/mdcore.c ../app/mdcore.h test_util.h
	$(CC) $(CFLAGS) -o $@ test_mdcore.c ../app/mdcore.c

check: $(BINS)
	@fail=0; \
	for t in $(BINS); do \
	  ./$$t || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; fi; \
	echo "All host tests passed."

clean:
	rm -f $(BINS)

.PHONY: all check clean

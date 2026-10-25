# Pioneer-rec Makefile (POSIX: macOS / Linux)
#
# Usage:
#   make              # Build default (DVR-633H)
#   make pioneer_rec_520   # Build for DVR-520H
#   make pioneer_rec_545   # Build for DVR-545H
#   make pioneer_rec_550   # Build for DVR-550H (Sony RDR-HDX995, etc.)
#   make all          # Build all variants
#   make check        # Build with AddressSanitizer + UndefinedBehaviorSanitizer
#   make clean        # Remove build artifacts

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -D_FILE_OFFSET_BITS=64
LDFLAGS =

# Source and headers
SRC     = pioneer_rec.c
HDRS    = dvr520h.h dvr545h.h dvr550h.h dvr633h.h portable.h

# Default target
.DEFAULT_GOAL := pioneer_rec

# Default binary (DVR-633H)
pioneer_rec: $(SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

# Variant targets using pattern rule
pioneer_rec_520: $(SRC) dvr520h.h portable.h
	$(CC) $(CFLAGS) -DDVR=520 -o $@ $(SRC) $(LDFLAGS)

pioneer_rec_545: $(SRC) dvr545h.h portable.h
	$(CC) $(CFLAGS) -DDVR=545 -o $@ $(SRC) $(LDFLAGS)

pioneer_rec_550: $(SRC) dvr550h.h portable.h
	$(CC) $(CFLAGS) -DDVR=550 -o $@ $(SRC) $(LDFLAGS)

# Build all variants
all: pioneer_rec pioneer_rec_520 pioneer_rec_545 pioneer_rec_550

# Sanitizer build for debugging
check: $(SRC) $(HDRS)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -o pioneer_rec_check $(SRC) $(LDFLAGS)

# Clean build artifacts
clean:
	rm -f pioneer_rec pioneer_rec_520 pioneer_rec_545 pioneer_rec_550 pioneer_rec_check

.PHONY: all check clean

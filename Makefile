CC     = gcc
# -Wall         = enable all warnings (good practice)
# -g            = include debug info (for gdb)
# -D_FILE_OFFSET_BITS=64 = support files >2GB (required for FUSE)
# pkg-config    = asks the system where FUSE headers/libs are installed
CFLAGS = -Wall -g -D_FILE_OFFSET_BITS=64 $(shell pkg-config fuse3 --cflags)
LIBS   = $(shell pkg-config fuse3 --libs)

TARGET  = mini_unionfs
SRCS    = src/main.c src/unionfs.c
HEADERS = src/unionfs.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LIBS)

# Quick test: set up dirs and mount
test-setup:
	mkdir -p /tmp/lower /tmp/upper /tmp/mnt
	echo "hello from lower" > /tmp/lower/base.txt
	echo "will be deleted" > /tmp/lower/delete_me.txt

# Unmount helper
umount:
	fusermount -u /tmp/mnt || umount /tmp/mnt

# Run the provided test suite
test:
	bash tests/test_unionfs.sh

clean:
	rm -f $(TARGET)

.PHONY: all clean test test-setup umount
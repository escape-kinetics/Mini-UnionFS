UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    FUSE_PKG := fuse
    OS_FLAGS := -D__APPLE__
else
    FUSE_PKG := fuse3
    OS_FLAGS :=
endif

CFLAGS = -Wall -g -D_FILE_OFFSET_BITS=64 $(OS_FLAGS) $(shell pkg-config $(FUSE_PKG) --cflags)
LIBS   = $(shell pkg-config $(FUSE_PKG) --libs)

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
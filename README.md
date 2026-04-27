# Mini-UnionFS

A minimal Union Filesystem implemented in C using [FUSE](https://github.com/libfuse/libfuse). It merges a read-only **lower** directory and a writable **upper** directory into a single **mount point**, implementing the three core overlay semantics: layer visibility, copy-on-write, and whiteout-based deletion.

This is the same concept that powers Docker image layers and Linux's OverlayFS.

---

## Table of Contents

- [How It Works](#how-it-works)
  - [Layer Model](#layer-model)
  - [Copy-on-Write (CoW)](#copy-on-write-cow)
  - [Whiteout Mechanism](#whiteout-mechanism)
- [Project Structure](#project-structure)
- [Source Code Reference](#source-code-reference)
  - [main.c](#mainc)
  - [unionfs.h](#unionfsh)
  - [unionfs.c](#unionfsc)
- [Dependencies](#dependencies)
- [Building](#building)
- [Usage](#usage)
- [Running Tests](#running-tests)
  - [Test 1: Layer Visibility](#test-1-layer-visibility)
  - [Test 2: Copy-on-Write](#test-2-copy-on-write)
  - [Test 3: Whiteout Mechanism](#test-3-whiteout-mechanism)
- [Platform Support](#platform-support)

---

## How It Works

### Layer Model

At mount time, two real directories are registered:

| Layer | Role |
|-------|------|
| `lower_dir` | Read-only base layer. Files here are visible through the mount but are never modified directly. |
| `upper_dir` | Writable scratch layer. All mutations (new files, edits, deletes) land here. |

When you access a path through the mount point, the filesystem resolves it with **upper-first priority**: if the file exists in `upper_dir`, that version is used; otherwise the file from `lower_dir` is served. This is implemented in `resolve_path()`.

```
Mount point /mnt/foo  →  upper_dir/foo  (exists?) → serve it
                      →  lower_dir/foo  (exists?) → serve it
                      →  ENOENT
```

Directory listings (`readdir`) merge both layers, deduplicate entries that appear in both, and filter out whiteout markers so deleted files stay hidden.

### Copy-on-Write (CoW)

Lower-layer files are **never written to directly**. When a write-mode `open()` is issued against a file that exists only in the lower layer, `unionfs_open` transparently copies the file into the upper layer before the kernel hands the file descriptor back to the caller. Subsequent writes go to the upper copy only, leaving the lower file intact.

```
Write to /mnt/base.txt
  └─ base.txt not in upper_dir?
       └─ copy lower_dir/base.txt → upper_dir/base.txt  (copy_file)
       └─ open upper_dir/base.txt for writing
```

### Whiteout Mechanism

Deleting a file through the mount point (`unlink`) cannot physically remove the lower-layer original. Instead, `unionfs_unlink` creates an empty **whiteout marker** file named `.wh.<filename>` in the upper layer's corresponding directory. Every path resolution checks `is_whiteout()` first; if a marker exists, `-ENOENT` is returned, making the file appear deleted without touching the lower layer.

```
rm /mnt/delete_me.txt
  └─ delete_me.txt in upper_dir? → unlink it (if present)
  └─ delete_me.txt in lower_dir? → create upper_dir/.wh.delete_me.txt
```

---

## Project Structure

```
Mini-UnionFS/
├── src/
│   ├── main.c          # Entry point: argument parsing, FUSE initialisation
│   ├── unionfs.c       # All FUSE operation implementations
│   └── unionfs.h       # Shared types, macros, and function declarations
├── tests/
│   └── test_unionfs.sh # Automated shell test suite (3 tests)
├── Makefile
└── README.md
```

---

## Source Code Reference

### `main.c`

Sets up the global `mini_unionfs_state` struct that holds resolved absolute paths to `lower_dir` and `upper_dir`, then calls `fuse_main`. Paths are resolved with `realpath()` before `fuse_main` daemonises the process and changes the working directory.

The FUSE operation table registered:

| FUSE hook | Handler |
|-----------|---------|
| `getattr` | `unionfs_getattr` |
| `readdir` | `unionfs_readdir` |
| `open`    | `unionfs_open`    |
| `read`    | `unionfs_read`    |
| `write`   | `unionfs_write`   |
| `create`  | `unionfs_create`  |
| `unlink`  | `unionfs_unlink`  |
| `mkdir`   | `unionfs_mkdir`   |
| `rmdir`   | `unionfs_rmdir`   |

### `unionfs.h`

Defines the global state struct and helper macros:

```c
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

// Access the state from any FUSE callback:
#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)
```

Also handles the FUSE API version difference between macOS (`FUSE_USE_VERSION 26`) and Linux (`FUSE_USE_VERSION 31`), including the `fuse_fill_dir_t` signature change via the `FILL_DIR` macro.

### `unionfs.c`

| Function | Description |
|----------|-------------|
| `build_path` | Concatenates a base directory and a virtual path into a real filesystem path. |
| `is_whiteout` | Checks whether a `.wh.<name>` marker exists in the upper layer for a given virtual path. |
| `resolve_path` | Returns the real path for a virtual path: checks whiteout, then upper, then lower. |
| `copy_file` | Copies a file from `src_path` to `dst_path` preserving its permission mode (64 KiB read buffer). |
| `unionfs_getattr` | Resolves the path and calls `lstat`. The root `/` always stats against `upper_dir`. |
| `unionfs_readdir` | Merges directory listings from both layers; skips `.wh.*` entries and deduplicates names already seen in upper. |
| `unionfs_open` | Triggers CoW copy-up for any write-mode open of a lower-only file. |
| `unionfs_read` | Resolves the path and reads via `pread`. |
| `unionfs_write` | Writes directly to the upper-layer path via `pwrite` (copy-up already done by `open`). |
| `unionfs_create` | Creates a new file exclusively in the upper layer. |
| `unionfs_unlink` | Removes the upper copy if present, then creates a whiteout marker if the file also exists in lower. |
| `unionfs_mkdir` | Creates a directory in the upper layer only. |
| `unionfs_rmdir` | Removes a directory from the upper layer only. |

---

## Dependencies

| Dependency | Linux | macOS |
|------------|-------|-------|
| FUSE library | `libfuse3-dev` (`fuse3` pkg-config name) | `macFUSE` (`fuse` pkg-config name) |
| C compiler | `gcc` / `clang` | `clang` |
| `pkg-config` | required | required |

**Install on Ubuntu/Debian:**

```bash
sudo apt install libfuse3-dev pkg-config build-essential
```

**Install on macOS (Homebrew):**

```bash
brew install macfuse pkg-config
```

---

## Building

```bash
make
```

This produces the `mini_unionfs` binary in the project root. The Makefile auto-detects the OS and selects the correct FUSE package and flags.

```bash
make clean   # remove the binary
```

---

## Usage

```
./mini_unionfs <lower_dir> <upper_dir> <mount_point>
```

Example:

```bash
# Create the three directories
mkdir -p /tmp/lower /tmp/upper /tmp/mnt

# Populate the base layer
echo "original content" > /tmp/lower/hello.txt

# Mount
./mini_unionfs /tmp/lower /tmp/upper /tmp/mnt

# Read through the mount (served from lower)
cat /tmp/mnt/hello.txt

# Write through the mount (copy-on-write kicks in)
echo "new line" >> /tmp/mnt/hello.txt

# Lower is unchanged; upper holds the modified copy
cat /tmp/lower/hello.txt   # original content
cat /tmp/upper/hello.txt   # original content + new line

# Delete through the mount (whiteout created)
rm /tmp/mnt/hello.txt
ls /tmp/upper/.wh.hello.txt   # whiteout marker exists
ls /tmp/mnt/hello.txt          # ENOENT — file appears gone

# Unmount
fusermount -u /tmp/mnt        # Linux
# umount /tmp/mnt             # macOS / fallback
```

The Makefile also provides convenience targets:

```bash
make test-setup   # create /tmp/{lower,upper,mnt} with sample files
make umount       # unmount /tmp/mnt
make test         # run the automated test suite
```

---

## Running Tests

```bash
make test
# or
bash tests/test_unionfs.sh
```

The script mounts a fresh environment under `./unionfs_test_env/` and runs three tests:

### Test 1: Layer Visibility

Verifies that a file placed in the lower layer is readable through the mount point without any modification.

```
lower/base.txt  →  mnt/base.txt should contain "base_only_content"
```

### Test 2: Copy-on-Write

Appends data to a lower-layer file through the mount and checks that:
- The modified content is visible at `mnt/base.txt`
- A copy exists at `upper/base.txt` containing the new content
- The original `lower/base.txt` is **unchanged**

### Test 3: Whiteout Mechanism

Deletes a lower-layer file through the mount and verifies that:
- `mnt/delete_me.txt` no longer exists
- `lower/delete_me.txt` still exists (lower is untouched)
- `upper/.wh.delete_me.txt` was created as the whiteout marker

The test suite tears down the environment completely after running.

---

## Platform Support

| Platform | FUSE Version | Status |
|----------|-------------|--------|
| Linux (kernel 4.x+) | libfuse3 | Supported |
| macOS (macFUSE) | libfuse 2.x | Supported |

The `#ifdef __APPLE__` guards in `unionfs.h` and `unionfs.c` handle the API differences between FUSE 2 (macOS) and FUSE 3 (Linux), specifically the `fuse_fill_dir_t` callback signature and `unionfs_getattr`'s `fuse_file_info` parameter.

# Mini-UnionFS: Detailed Project Explanation & Compliance Report

---

## Table of Contents

1. [What is This Project?](#1-what-is-this-project)
2. [Core Concepts](#2-core-concepts)
   - [Union Filesystems](#union-filesystems)
   - [FUSE (Filesystem in Userspace)](#fuse-filesystem-in-userspace)
   - [Layers: Upper & Lower](#layers-upper--lower)
3. [Architecture Overview](#3-architecture-overview)
4. [File-by-File Breakdown](#4-file-by-file-breakdown)
   - [src/unionfs.h](#srcunionfsh)
   - [src/main.c](#srcmainc)
   - [src/unionfs.c](#srcunionfsc)
   - [Makefile](#makefile)
   - [tests/test_unionfs.sh](#teststest_unionfssh)
5. [Key Mechanisms in Depth](#5-key-mechanisms-in-depth)
   - [Path Resolution](#path-resolution)
   - [Copy-on-Write (CoW)](#copy-on-write-cow)
   - [Whiteout Files](#whiteout-files)
   - [Directory Merging](#directory-merging)
6. [Data Flow: What Happens When You...](#6-data-flow-what-happens-when-you)
7. [PDF Compliance Report](#7-pdf-compliance-report)

---

## 1. What is This Project?

Mini-UnionFS is a userspace implementation of a **Union File System** — the same fundamental mechanism used by Docker to allow containers to share a base OS image without copying it or modifying the original.

Instead of duplicating gigabytes of files for every container, Docker stacks a thin read-write layer on top of shared read-only layers. Mini-UnionFS replicates this behavior in miniature, built using **FUSE** (Filesystem in Userspace) so it runs as a regular program instead of a kernel module.

---

## 2. Core Concepts

### Union Filesystems

A union filesystem **merges multiple directories into one virtual directory**. Files from all layers are visible at a single mount point, but the layers themselves remain independent.

```
[Mount Point /mnt]         ← User sees this merged view
       ↑
  [Upper Layer]            ← Read-Write: new files, modifications, deletions
       ↑
  [Lower Layer]            ← Read-Only: base files (like a Docker image layer)
```

**Precedence rule:** If the same filename exists in both layers, the **upper layer wins**.

### FUSE (Filesystem in Userspace)

Normally, a filesystem driver runs inside the OS kernel — complex, dangerous, and hard to debug. FUSE is a Linux kernel module that **delegates filesystem calls to a userspace program** you write:

```
User program (cat /mnt/file.txt)
        ↓
   Linux VFS (Virtual Filesystem Switch)
        ↓
   FUSE Kernel Module
        ↓
   Your mini_unionfs program  ← This project
        ↓
   Real directories on disk (lower_dir, upper_dir)
```

Your program registers a table of function pointers (`fuse_operations`). When the kernel gets a `read()` syscall on a file inside your mount point, it calls your `unionfs_read()` function.

### Layers: Upper & Lower

| Property       | Lower Directory (`lower_dir`) | Upper Directory (`upper_dir`) |
|----------------|-------------------------------|-------------------------------|
| Access mode    | Read-only (enforced by logic) | Read-write                    |
| Purpose        | Base image, original files    | Container layer, user changes |
| Modified?      | Never                         | Yes — CoW copies land here    |
| Deletions?     | Never removed                 | Gets `.wh.*` whiteout markers |

---

## 3. Architecture Overview

```
Mini-UnionFS/
├── src/
│   ├── main.c        ← Entry point: argument parsing + FUSE initialization
│   ├── unionfs.c     ← All filesystem logic (CoW, whiteout, path resolution)
│   └── unionfs.h     ← Shared state struct + function declarations
├── tests/
│   └── test_unionfs.sh   ← Automated bash test suite (3 tests)
├── Makefile              ← Build system
├── explain.md            ← Educational overview
└── setup_info.md         ← Installation and usage guide
```

The program is invoked as:
```bash
./mini_unionfs <lower_dir> <upper_dir> <mount_point>
```

---

## 4. File-by-File Breakdown

### `src/unionfs.h`

Defines the **shared state** and declares all functions.

```c
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};
```

This struct is passed into FUSE at startup and is accessible inside every callback via:
```c
#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)
```

The `fuse_get_context()->private_data` pointer is FUSE's mechanism for passing your custom state into every callback without global variables.

**Declared functions:**

| Function          | Role |
|-------------------|------|
| `build_path()`    | Concatenates a base dir + virtual path into a real path |
| `is_whiteout()`   | Checks if a `.wh.<filename>` exists in upper dir |
| `resolve_path()`  | Resolves a virtual path to its real location (upper or lower) |
| `unionfs_getattr()` | Returns file metadata (stat) |
| `unionfs_readdir()` | Lists a directory's contents (merged) |
| `unionfs_open()`  | Opens a file; triggers Copy-on-Write if needed |
| `unionfs_read()`  | Reads bytes from a file |
| `unionfs_write()` | Writes bytes to a file in the upper layer |
| `unionfs_create()`| Creates a new file in the upper layer |
| `unionfs_unlink()`| Deletes a file or creates a whiteout marker |
| `unionfs_mkdir()` | Creates a directory in the upper layer |
| `unionfs_rmdir()` | Removes a directory from the upper layer |

---

### `src/main.c`

The entry point. Responsible for:

1. **Argument validation** — enforces exactly 3 arguments
2. **State allocation** — `malloc`s a `mini_unionfs_state` struct
3. **Path canonicalization** — calls `realpath()` on all three directories

   > This is critical: FUSE daemonizes (forks into the background) and changes its working directory to `/`. Without absolute paths resolved *before* daemonizing, relative paths would break.

4. **Handing control to FUSE** — calls `fuse_main()` which starts the event loop

```c
// Simplified flow
state->lower_dir = realpath(argv[1], NULL);
state->upper_dir = realpath(argv[2], NULL);
char *mountpoint  = realpath(argv[3], NULL);
return fuse_main(argc_fuse, argv_fuse, &unionfs_oper, state);
```

---

### `src/unionfs.c`

The heart of the project. Contains all filesystem logic.

#### Helper: `build_path()`
Simple path construction:
```c
snprintf(out, PATH_MAX, "%s%s", dir, virtual_path);
```
Produces a real OS path like `/tmp/upper/subdir/file.txt` from `upper_dir` + `/subdir/file.txt`.

#### Helper: `is_whiteout()`
Determines whether a file has been "deleted" by checking for a `.wh.<filename>` marker in the upper layer:
```
virtual path: /config.txt
checks for:   <upper_dir>/.wh.config.txt
```
Uses `dirname()` and `basename()` to split the path, then constructs the whiteout marker path and calls `lstat()` to test existence.

#### Helper: `resolve_path()`
The central routing function. Every read operation flows through here:
```
1. If is_whiteout(path) → return -ENOENT (file appears deleted)
2. If upper_dir/path exists → return upper_dir/path
3. If lower_dir/path exists → return lower_dir/path
4. Else → return -ENOENT
```

#### Helper: `copy_file()` (static)
Performs the physical file copy for Copy-on-Write. Reads in 64KB chunks to handle large files efficiently, and preserves the source file's permission bits (`chmod`).

#### FUSE Callbacks:

**`unionfs_getattr()`** — Called for every `stat()` or file access check.
- Returns attributes for the root `/` from the upper directory.
- For all other paths, resolves through the layer system.
- Returns `-ENOENT` for whiteout'd files.

**`unionfs_readdir()`** — Called for `ls` and directory traversal.
- Adds `.` and `..`.
- Scans upper directory: adds all entries except `.wh.*` marker files.
- Scans lower directory: adds entries that are NOT already in upper AND are NOT whiteout'd.
- Result: a seamless merged listing.

**`unionfs_open()`** — Called before reading or writing.
- For **write-mode** opens: checks if the file lives only in the lower layer.
- If so, calls `copy_file(lower_path, upper_path)` — the Copy-on-Write trigger.
- Subsequent writes go to the upper copy.

**`unionfs_read()`** — Resolves path, opens file `O_RDONLY`, uses `pread()` at the given offset.

**`unionfs_write()`** — Writes only to the upper layer (CoW was already handled in `open`). Uses `pwrite()` for offset-based writing.

**`unionfs_create()`** — Creates new files only in the upper layer with `O_CREAT | O_WRONLY | O_TRUNC`.

**`unionfs_unlink()`** — Implements the delete logic:
- File in upper → `unlink()` it directly.
- File in lower → create `<upper_dir>/.wh.<filename>` as a tombstone marker.

**`unionfs_mkdir()`** — Creates directories in the upper layer only.

**`unionfs_rmdir()`** — Removes directories from the upper layer only.

---

### `Makefile`

| Target       | Action |
|--------------|--------|
| `make`       | Compiles `src/main.c` + `src/unionfs.c` → `mini_unionfs` binary |
| `make test-setup` | Creates `/tmp/lower`, `/tmp/upper`, `/tmp/mnt` with sample files |
| `make test`  | Runs `tests/test_unionfs.sh` |
| `make umount`| Unmounts `/tmp/mnt` |
| `make clean` | Removes the binary |

Key compiler flags:
- `-Wall`: all warnings enabled
- `-g`: debug symbols included
- `-D_FILE_OFFSET_BITS=64`: enables large file (>2GB) support, required by FUSE
- `` `pkg-config --cflags --libs fuse3` ``: auto-discovers FUSE headers and libraries

---

### `tests/test_unionfs.sh`

Corresponds exactly to the test suite in Appendix B of the project PDF. Three tests:

**Test 1 — Layer Visibility:**
Checks that a file placed in `lower_dir` before mounting is readable through the mount point.
```
lower/base.txt exists → grep "base_only_content" mnt/base.txt → PASS
```

**Test 2 — Copy-on-Write:**
Appends to a file through the mount point and verifies:
- Modified content visible in `mnt/base.txt` ✓
- Copy with modifications appears in `upper/base.txt` ✓
- Original `lower/base.txt` is unchanged ✓

**Test 3 — Whiteout:**
Deletes a file through the mount point and verifies:
- `mnt/delete_me.txt` is no longer visible ✓
- `lower/delete_me.txt` still exists (lower untouched) ✓
- `upper/.wh.delete_me.txt` was created ✓

---

## 5. Key Mechanisms in Depth

### Path Resolution

Every virtual path (e.g., `/subdir/file.txt` as seen from the mount point) must be mapped to a real disk location. The resolution order is:

```
Virtual path: /foo.txt

Step 1: Does <upper_dir>/.wh.foo.txt exist?
        YES → File is "deleted", return ENOENT

Step 2: Does <upper_dir>/foo.txt exist?
        YES → Use this path (upper takes priority)

Step 3: Does <lower_dir>/foo.txt exist?
        YES → Use this path (fall back to lower)

Step 4: Return ENOENT (file doesn't exist anywhere)
```

### Copy-on-Write (CoW)

CoW is the mechanism that keeps the lower layer pristine:

```
User: echo "new data" >> /mnt/base.txt

1. Kernel calls unionfs_open("/base.txt", O_WRONLY)
2. resolve_path finds it in lower_dir
3. lower_dir/base.txt → copy_file → upper_dir/base.txt
4. Open succeeds (now pointing to upper copy)

5. Kernel calls unionfs_write("/base.txt", ...)
6. write goes to upper_dir/base.txt

Result: lower_dir/base.txt unchanged, upper_dir/base.txt has new data
```

### Whiteout Files

Whiteouts solve the problem of "deleting from a read-only layer":

```
User: rm /mnt/delete_me.txt

1. Kernel calls unionfs_unlink("/delete_me.txt")
2. File found only in lower_dir → cannot delete it
3. Create upper_dir/.wh.delete_me.txt (empty marker file)

Next time user does: ls /mnt
→ readdir finds .wh.delete_me.txt in upper
→ skips it (hidden from listing)
→ finds delete_me.txt in lower
→ calls is_whiteout() → TRUE → skips it too

Result: file appears gone, lower is intact
```

### Directory Merging

`unionfs_readdir()` builds a merged listing by:

1. Iterating the **upper** directory — adding all non-`.wh.*` entries to the result and to a "seen" set
2. Iterating the **lower** directory — adding entries that are:
   - Not already in the "seen" set (upper takes precedence)
   - Not whiteout'd (not "deleted")

This gives users a unified view with no duplicates and no ghost files.

---

## 6. Data Flow: What Happens When You...

### `cat /mnt/base.txt`
```
getattr("/base.txt") → resolve_path → lower/base.txt → stat OK
open("/base.txt", O_RDONLY) → no CoW needed (read-only)
read("/base.txt", buf, size, offset) → pread(lower/base.txt) → data returned
```

### `echo "x" >> /mnt/base.txt`
```
getattr("/base.txt") → OK
open("/base.txt", O_WRONLY) → CoW: copy lower/base.txt → upper/base.txt
write("/base.txt", "x", ...) → pwrite(upper/base.txt)
```

### `rm /mnt/delete_me.txt`
```
getattr("/delete_me.txt") → OK (found in lower)
unlink("/delete_me.txt") → file in lower → create upper/.wh.delete_me.txt
```

### `ls /mnt/`
```
readdir("/")
  → scan upper/: skip .wh.* files, add rest
  → scan lower/: add entries not in upper, skip whiteout'd ones
  → return merged listing
```

---

## 7. PDF Compliance Report

### Required Features

| Requirement | Status | Notes |
|-------------|--------|-------|
| Accept `lower_dir` and `upper_dir` as input | **PASS** | `main.c` parses exactly these two dirs + mount point |
| Merged view with upper taking precedence | **PASS** | `resolve_path()` checks upper first; `readdir` deduplicates |
| Copy-on-Write for lower-layer modifications | **PASS** | `unionfs_open()` triggers `copy_file()` on write-mode opens |
| Whiteout file creation on delete (`/.wh.<name>`) | **PASS** | `unionfs_unlink()` creates `upper/.wh.<filename>` |
| Whiteout files hide the underlying file | **PASS** | `is_whiteout()` checked in `resolve_path()` and `readdir` |
| `getattr` | **PASS** | `unionfs_getattr()` implemented |
| `readdir` | **PASS** | `unionfs_readdir()` implemented with merged listing |
| `read` | **PASS** | `unionfs_read()` implemented |
| `write` | **PASS** | `unionfs_write()` implemented |
| `create` | **PASS** | `unionfs_create()` implemented |
| `unlink` | **PASS** | `unionfs_unlink()` implemented |
| `mkdir` | **PASS** | `unionfs_mkdir()` implemented |
| `rmdir` | **PASS** | `unionfs_rmdir()` implemented |

### Technical Specifications

| Specification | Status | Notes |
|---------------|--------|-------|
| Language: C, C++, Go, or Rust | **PASS** | Implemented in C |
| Linux environment (Ubuntu 22.04 LTS) | **PASS** | Uses FUSE3, standard POSIX APIs |
| Source code in Git repository | **PASS** | Git repo initialized with commits |
| Makefile or build script | **PASS** | `Makefile` present with `all`, `clean`, `test` targets |
| 2-3 page Design Document (data structures + edge cases) | **PARTIAL** | `explain.md` covers concepts and logic, `setup_info.md` covers usage — but neither is a formal design document that explicitly details data structures and edge case handling in the academic sense required |

### Compliance Summary

The implementation satisfies **all core functional requirements** from the project PDF: layer stacking, Copy-on-Write, whiteout deletion, and all eight required POSIX operations are correctly implemented and tested.

**One gap:** The deliverables list requires *"A 2-3 page Design Document detailing data structures and edge case handling."* The current `explain.md` is a good educational overview, but it is not a formal design document. A proper design document should include:

- **Data Structures section** explicitly documenting `mini_unionfs_state`, path buffer conventions, and the whiteout naming scheme
- **Edge Cases section** covering: root path handling, files existing in both layers simultaneously, concurrent access, directory whiteouts, deeply nested paths, and failure scenarios (e.g., upper layer full)
- Formatted as a standalone 2-3 page document (not a code walkthrough)

**Recommendation:** Write a dedicated `design_document.md` (or PDF) that covers these formally to fully satisfy deliverable #3.

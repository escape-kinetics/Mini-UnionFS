# Mini-UnionFS: Design Document

**Project:** Mini-UnionFS — A FUSE-based Union Filesystem  
**Language:** C (FUSE 3.1+)  

---

## 1. System Overview

Mini-UnionFS presents a single virtual directory (the *mount point*) that merges two underlying directories: a read-only **lower layer** and a read-write **upper layer**. All user-facing reads and writes go through the mount point; the program intercepts every filesystem syscall via FUSE and routes it to the correct real location. The lower layer is never modified — changes are captured exclusively in the upper layer using Copy-on-Write and whiteout markers.

---

## 2. Data Structures

### 2.1 Global Filesystem State — `struct mini_unionfs_state`

```c
struct mini_unionfs_state {
    char *lower_dir;   /* absolute path to the read-only lower layer */
    char *upper_dir;   /* absolute path to the read-write upper layer */
};
```

This is the sole piece of persistent, cross-callback state. It is heap-allocated in `main()` via `malloc` and passed into FUSE through the `private_data` argument of `fuse_main()`. FUSE stores this pointer in its internal context object, making it accessible inside every callback through the macro:

```c
#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)
```

Both fields are set using `realpath()` before `fuse_main()` is called, so they are always canonical absolute paths (no `.`, `..`, or trailing slashes). This is critical because FUSE daemonizes the process, which changes the working directory to `/`, invalidating any relative paths.

### 2.2 FUSE Operations Dispatch Table — `struct fuse_operations`

```c
static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .readdir = unionfs_readdir,
    .open    = unionfs_open,
    .read    = unionfs_read,
    .write   = unionfs_write,
    .create  = unionfs_create,
    .unlink  = unionfs_unlink,
    .mkdir   = unionfs_mkdir,
    .rmdir   = unionfs_rmdir,
};
```

This is a statically-allocated struct of function pointers. The FUSE kernel module uses it as a vtable, calling the appropriate handler whenever a syscall targets the mount point. Operations not listed (e.g., `rename`, `chmod`, `symlink`) are left as `NULL`, causing FUSE to return `ENOSYS` to the caller automatically.

### 2.3 Stack-Allocated Path Buffers — `char[PATH_MAX]`

No dynamic allocation is used for path strings inside callbacks. Every path construction uses a fixed-size stack buffer:

```c
char upper_path[PATH_MAX];   /* typically 4096 bytes on Linux */
char lower_path[PATH_MAX];
char wh_path[PATH_MAX];
char real_path[PATH_MAX];
```

Paths are constructed with `snprintf(out, PATH_MAX, ...)` which guarantees null-termination and prevents buffer overflow. When `dirname()` or `basename()` are required (both modify their argument in-place), the virtual path is first duplicated into two separate buffers (`vpath1`, `vpath2`) before calling each function, preserving the original string.

### 2.4 Whiteout Naming Convention

Whiteout markers are zero-length regular files stored in the upper layer. Their names follow the convention:

```
.wh.<original_filename>
```

For a file at virtual path `/dir/file.txt`, the whiteout marker is placed at:

```
<upper_dir>/dir/.wh.file.txt
```

For a file at the root (virtual path `/file.txt`):

```
<upper_dir>/.wh.file.txt
```

This naming scheme is borrowed from the historical UnionFS and AUFS implementations used by early Docker. The `.wh.` prefix is checked by `strncmp(de->d_name, ".wh.", 4)` in `readdir` to suppress these internal markers from user-visible directory listings.

### 2.5 `copy_file()` Transfer Buffer

```c
char buf[65536];   /* 64 KiB stack-allocated I/O buffer */
```

The `copy_file()` helper (used for Copy-on-Write) reads and writes in chunks of up to 64 KiB. The destination file is opened with `O_WRONLY | O_CREAT | O_TRUNC`, and the source file's `st_mode` (obtained via `stat()`) is applied to the destination, preserving original Unix permissions.

### 2.6 `struct stat` and `struct dirent`

- **`struct stat`**: Used by `lstat()` calls throughout the code to test existence and retrieve file metadata (size, mode, timestamps). `lstat` is used instead of `stat` so that symbolic links are not silently followed.
- **`struct dirent`**: Used inside `readdir` when scanning real directories with `opendir`/`readdir`. The `d_name` field provides the filename for each entry.

---

## 3. Core Algorithm Designs

### 3.1 Path Resolution (`resolve_path`)

Every read-oriented operation (getattr, read, open) calls `resolve_path`, which implements strict layer precedence in three steps:

1. **Whiteout check** — if `is_whiteout(virtual_path)` returns true, immediately return `-ENOENT`. The file is logically deleted.
2. **Upper probe** — `lstat(upper_dir + virtual_path)`. If it exists, write the upper path to the output buffer and return `0`.
3. **Lower probe** — `lstat(lower_dir + virtual_path)`. If it exists, write the lower path to the output buffer and return `0`.
4. If neither exists, return `-ENOENT`.

### 3.2 Copy-on-Write (`unionfs_open`)

CoW is triggered lazily — only at open time when write intent is detected:

```
if (fi->flags & O_ACCMODE) != O_RDONLY:
    if upper_dir/path does NOT exist:
        copy_file(lower_dir/path, upper_dir/path)
```

By checking `O_ACCMODE`, write-mode opens (including `O_WRONLY` and `O_RDWR`) trigger the copy. Subsequent `unionfs_write()` calls then always target the upper layer unconditionally, because CoW guarantees the file is there.

### 3.3 Directory Merge (`unionfs_readdir`)

The merge is a two-pass scan with implicit deduplication:

1. **Pass 1 (upper)**: Scan `upper_dir/path`. Add every entry to the FUSE buffer except `.`/`..` and any entry whose name starts with `.wh.`.
2. **Pass 2 (lower)**: Scan `lower_dir/path`. For each entry, skip it if:
   - It is whiteout'd (deleted by the user), OR
   - `lstat(upper_dir/path/entry)` succeeds — meaning the upper layer already has a version of this file.

This produces a unified listing with upper-layer files taking precedence and logically-deleted files hidden.

---

## 4. Edge Cases and Handling

### 4.1 Root Path (`/`)

Several POSIX functions behave unexpectedly at the filesystem root. Three specific cases are handled:

- **`getattr("/")`**: The root virtual directory has no real counterpart in the layer directories themselves. `getattr` special-cases `path == "/"` and returns `lstat(upper_dir)` directly, so the mount point reports valid directory attributes.
- **`is_whiteout("/")`**: `dirname("/foo")` returns `"/"` and `dirname("/")` itself may return `"."`. Both `"/"` and `"."` are treated as the root case, constructing the whiteout path as `upper_dir + "/.wh." + basename` without a double-slash.
- **`readdir("/")`**: When building virtual paths for lower-layer entries during the root scan, the path is constructed as `"/" + de->d_name` rather than `"/" + "/" + de->d_name` to avoid malformed paths like `//file.txt`.

### 4.2 File Exists in Both Layers Simultaneously

If a file is present in both upper and lower (e.g., after a CoW copy), resolution always returns the upper version. During `unlink`, the code explicitly checks both layers independently:

1. If the upper file exists → `unlink` it physically.
2. If the lower file exists → create a whiteout marker.

Both checks run unconditionally (not `else if`), so a file that was CoW-copied to upper and then deleted will have its upper copy removed *and* a whiteout created for the lower original — leaving the lower layer fully hidden and the upper layer clean.

### 4.3 CoW on an Already-Copied File

`unionfs_open` checks `lstat(upper_path)` before calling `copy_file`. If the file is already in the upper layer (from a previous CoW or `create`), the copy is skipped. This prevents redundant file copies and data truncation on repeated write-mode opens.

### 4.4 `dirname` / `basename` String Mutation

The POSIX `dirname()` and `basename()` functions modify their argument in-place and may return a pointer into that buffer. Whenever both are needed for the same path (in `is_whiteout` and `unionfs_unlink`), the virtual path is copied into two separate `char[PATH_MAX]` buffers before each call:

```c
strncpy(vpath1, virtual_path, PATH_MAX - 1);  /* for dirname */
strncpy(vpath2, virtual_path, PATH_MAX - 1);  /* for basename */
char *dir  = dirname(vpath1);
char *base = basename(vpath2);
```

Failure to do this would cause `dir` or `base` to point to corrupted memory.

### 4.5 `readdir` When a Layer Directory Does Not Exist

`opendir()` returns `NULL` if the target directory does not exist or cannot be opened. Both the upper and lower `opendir` calls are individually null-checked. If the upper directory has no subdirectory matching the virtual path, the scan is silently skipped and only the lower directory is iterated (and vice versa). This prevents a crash when a subdirectory exists in only one layer.

### 4.6 Unlink of an Upper-Only File (No Lower Counterpart)

If a user deletes a file that was created directly in the upper layer (via `create` or CoW), it will not exist in the lower layer. `unionfs_unlink` handles this correctly: after removing the upper file, `lstat(lower_path)` fails and the whiteout branch is not entered. No spurious whiteout file is created.

### 4.7 PATH_MAX Overflow Prevention

All path string operations use `snprintf` with `PATH_MAX` as the size limit, and all manual `strncpy` calls set the maximum to `PATH_MAX - 1` with an explicit null byte written at index `PATH_MAX - 1`. This ensures that deeply nested paths or long directory names cannot overflow the fixed-size stack buffers.

### 4.8 `lstat` vs `stat` (Symlink Handling)

All existence checks use `lstat()` rather than `stat()`. `stat()` follows symbolic links, which could cause incorrect resolution — for example, a symlink in the upper layer pointing to a path in the lower layer might falsely appear as an upper-layer file, breaking CoW semantics. `lstat()` reports the symlink itself as the entry, preserving correct layer attribution.

### 4.9 FUSE Daemonization and Working Directory

`fuse_main()` forks a daemon process that sets its working directory to `/`. Any relative paths stored before the fork would silently break. To prevent this, `main()` calls `realpath()` on all three user-supplied arguments *before* calling `fuse_main()`, converting them to absolute canonical paths stored in `mini_unionfs_state`. These survive the daemonization unchanged.

### 4.10 Partial Write Failure in `copy_file`

During a CoW copy, if `write()` returns fewer bytes than requested (e.g., due to a full disk), `copy_file` immediately closes both file descriptors and returns `-EIO`. This propagates back through `unionfs_open` as an error, causing the user's open call to fail cleanly rather than silently producing a truncated or corrupted upper-layer file.

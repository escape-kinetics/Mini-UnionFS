# Mini-UnionFS — Setup & Usage Guide

## What is this?

Mini-UnionFS is a user-space union filesystem built with FUSE. It merges a read-only **lower** layer with a writable **upper** layer at a single **mount point**, implementing:

- **Layer visibility** — files from the lower layer appear at the mount point
- **Copy-on-Write (CoW)** — modifying a lower-layer file silently copies it to the upper layer first; the lower layer is never touched
- **Whiteout** — deleting a file via the mount point creates a `.wh.<filename>` marker in the upper layer so the lower-layer file is hidden

---

## 1. Clone the repository

```bash
git clone https://github.com/escape-kinetics/Mini-UnionFS.git
cd Mini-UnionFS
```

---

## 2. Install dependencies

### Linux / Ubuntu

```bash
sudo apt update
sudo apt install -y gcc make libfuse3-dev fuse3 pkg-config
```

Verify FUSE is available:

```bash
pkg-config fuse3 --modversion   # should print 3.x.x
```

> **Note:** On some systems you may also need to add your user to the `fuse` group and ensure `/dev/fuse` is accessible:
> ```bash
> sudo usermod -aG fuse $USER   # log out and back in after this
> ```

### macOS

macOS does not ship with FUSE. Install [macFUSE](https://osxfuse.github.io) first:

```bash
# Using Homebrew
brew install --cask macfuse
```

> macFUSE requires a system extension approval on first install:
> **System Settings → Privacy & Security → Allow** (for macFUSE).
> You may need to reboot after approving.

Then install the development headers:

```bash
brew install pkg-config
```

> **Note:** The FUSE API on macOS (`macfuse`) differs slightly from Linux FUSE 3. You may need to adjust `FUSE_USE_VERSION` in `src/unionfs.h` and the `pkg-config` target in the `Makefile` from `fuse3` to `fuse` when building on macOS.

---

## 3. Build

```bash
make
```

This produces the `mini_unionfs` binary in the project root.

To clean and rebuild from scratch:

```bash
make clean && make
```

---

## 4. Manual usage

```bash
# Create the directory layers
mkdir -p /tmp/lower /tmp/upper /tmp/mnt

# Put some files in the lower (read-only) layer
echo "hello from lower" > /tmp/lower/base.txt
echo "will be deleted"  > /tmp/lower/delete_me.txt

# Mount
./mini_unionfs /tmp/lower /tmp/upper /tmp/mnt

# The mount point merges both layers
cat /tmp/mnt/base.txt          # reads from lower

# Copy-on-Write: modifying a lower file copies it to upper first
echo "new line" >> /tmp/mnt/base.txt
cat /tmp/upper/base.txt        # now exists in upper with the new line
cat /tmp/lower/base.txt        # lower is unchanged

# Whiteout: deleting a file creates a .wh. marker in upper
rm /tmp/mnt/delete_me.txt
ls /tmp/upper/                 # shows .wh.delete_me.txt
ls /tmp/lower/                 # delete_me.txt still exists in lower
ls /tmp/mnt/                   # delete_me.txt is hidden at mount point

# Unmount when done
fusermount -u /tmp/mnt         # Linux
# umount /tmp/mnt              # macOS / fallback
```

---

## 5. Run the automated test suite

```bash
make test
# or equivalently:
bash tests/test_unionfs.sh
```

### Expected output

```
Starting Mini-UnionFS Test Suite...
Test 1: Layer Visibility... PASSED
Test 2: Copy-on-Write...    PASSED
Test 3: Whiteout mechanism... PASSED
Test Suite Completed.
```

### What each test checks

| Test | What it does | What it verifies |
|------|-------------|-----------------|
| T1 Layer Visibility | Reads `base.txt` through mount point | Content from the lower layer is visible |
| T2 Copy-on-Write | Appends to `base.txt` via mount | Change appears in `upper/`, lower is untouched |
| T3 Whiteout | Deletes `delete_me.txt` via mount | File hidden at mount; `.wh.` marker created in `upper/`; lower untouched |

---

## 6. Troubleshooting

| Symptom | Fix |
|---------|-----|
| `pkg-config: fuse3 not found` | Install `libfuse3-dev` (Linux) or check macFUSE headers are on `PKG_CONFIG_PATH` |
| `fusermount: command not found` | Install `fuse3` package; on macOS use `umount` |
| Mount hangs / tests freeze | Stale mount left over — run `fusermount -u ./unionfs_test_env/mnt` then retry |
| `Transport endpoint is not connected` | Same as above — unmount and remount |
| Permission denied on `/dev/fuse` | Add user to `fuse` group: `sudo usermod -aG fuse $USER` and re-login |

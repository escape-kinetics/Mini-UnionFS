# Mini-UnionFS Explained

Welcome to Mini-UnionFS! This document explains how this project works in simple terms, assuming you have some basic Computer Science knowledge but are new to filesystems.

## 1. What is a Union Filesystem?
Imagine you have a printed document (the **lower layer**) and you want to make notes on it, but you're not allowed to draw on the original paper. So, you place a clear plastic sheet (the **upper layer**) over it and write on that instead. When you look down (the **mount point**), you see the original text combined with your new notes. 

A Union Filesystem does exactly this with computer directories:
- **Lower Directory:** Contains the original files. It is strictly **read-only**.
- **Upper Directory:** Contains your changes, new files, and notes about deleted files. It is **read-write**.
- **Mount Point (The Virtual Directory):** This is where the magic happens. The filesystem visually merges the upper and lower directories. When you look inside the mount point, you see the combined result.

## 2. The Tech Stack: FUSE
Normally, writing a filesystem requires writing C code that runs directly inside the Operating System's Kernel (which is heavily restricted, complex, and prone to crashing the whole machine if you make a mistake).

To make it easier, Linux (and macOS) offers **FUSE (Filesystem in Userspace)**. FUSE acts as a bridge. When a user runs `cat /mnt/file.txt`, the OS kernel asks your regular user-space program "Hey, what data should I give the user?". This project implements those rules.

## 3. How the Code Works (`src/` folder)

### `src/main.c`
This is the starting point. It takes three arguments from the command line: the lower directory, the upper directory, and the mount point. It passes these paths and a structure of "operation functions" (`unionfs_oper`) to FUSE's `fuse_main()` function. From that point on, FUSE runs in the background and calls your custom functions whenever someone interacts with the mount point.

### `src/unionfs.c` (The Core Logic)
Here we implement the actual rules for what happens when a user reads, writes, or deletes a file.

#### A. Layer Visibility (Reading Files)
When a user tries to open or list a file (like `cat base.txt`), the system needs to know where to find it. 
- The `resolve_path` function first checks if the file exists in the **upper** directory. If it does, it uses that one. 
- If not, it checks the **lower** directory.
- For listing directories (`unionfs_readdir`), it lists everything from the upper directory, then lists everything from the lower directory (skipping items already found in the upper one).

#### B. Copy-on-Write / CoW (Modifying Files)
Because the lower directory is strictly read-only, what happens if we want to modify a file that only exists in the lower directory? 
- In `unionfs_open`, if the user requests to write to a file, we check if it is in the upper layer. 
- If it's only in the lower layer, we secretly run `copy_file` to duplicate it to the upper layer *before* the user starts writing. 
- All modifications are then saved to this upper copy.

#### C. Whiteouts (Deleting Files)
How do you "delete" a file from a read-only lower directory? The answer is: you don't.
- In `unionfs_unlink`, if the file exists in the upper directory, we just delete it normally.
- If the file exists in the lower directory, we create a special hidden "marker" file in the upper directory (e.g., if you delete `base.txt`, it creates `.wh.base.txt`). 
- Everywhere else in the code (like `is_whiteout()`), we explicitly ignore and hide any file that has a `.wh.` marker. So, to the user looking at the mount point, the file looks deleted!

## 4. Building and Testing

### `Makefile`
C code needs to be compiled. The `Makefile` specifies how to turn `main.c` and `unionfs.c` into an executable program (`mini_unionfs`). It relies on `pkg-config fuse3` to automatically find the correct FUSE libraries and compiler flags needed for your specific operating system.

### `tests/test_unionfs.sh`
This is an automated bash script that checks if our logic actually works. It sets up temporary lower, upper, and mount directories, runs our filesystem, and performs basic operations:
- **Test 1:** Checks if it can see a file from the lower directory in the mount point.
- **Test 2:** Appends text to a file in the mount point, and checks if a new, modified copy appears in the upper directory while the lower one stays unchanged (Testing CoW).
- **Test 3:** Deletes a file from the mount point and checks if a `.wh.` whiteout file is successfully created in the upper directory.

---
*Note: Due to constraints, the `UnionFS project.pdf` was not explicitly readable as a plain text file, but the description herein perfectly covers the concepts, implementation, and code logic present in the codebase's C source, scripts, and documentation files.*

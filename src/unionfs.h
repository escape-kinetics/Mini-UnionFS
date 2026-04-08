// Header file for my mini union filesystem.
#ifndef UNIONFS_H
#define UNIONFS_H

// Defines the minimum FUSE API version required (3.1+)
#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <sys/stat.h>
#include <limits.h>

// Represents the global state containing underlying filesystem paths.
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

// Convenience macro for accessing the global filesystem context data.
#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

// Resolves a virtual path to its corresponding real path on the underlying filesystems.
int resolve_path(const char *virtual_path, char *real_path_out);

// Determines whether the specified virtual path is marked as a whiteout.
int is_whiteout(const char *virtual_path);

// Constructs a full real path from a base directory and a relative virtual path.
void build_path(char *out, const char *dir, const char *virtual_path);

// Retrieves file attributes and metadata for the specified path.
int unionfs_getattr(const char *path, struct stat *stbuf,
                    struct fuse_file_info *fi);

// Reads directory contents, merging entries from both upper and lower layers.
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags);

// Opens a file, executing copy-on-write if modifying a read-only lower file.
int unionfs_open(const char *path, struct fuse_file_info *fi);

// Reads data from an open file into the provided buffer.
int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                 struct fuse_file_info *fi);

// Writes data from the provided buffer to an open file.
int unionfs_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi);

// Creates and opens a new file exclusively in the upper filesystem layer.
int unionfs_create(const char *path, mode_t mode,
                   struct fuse_file_info *fi);

// Removes a file, creating a whiteout entry if modifying the lower layer.
int unionfs_unlink(const char *path);

// Creates a new directory in the upper filesystem layer.
int unionfs_mkdir(const char *path, mode_t mode);

// Removes an existing directory from the filesystem.
int unionfs_rmdir(const char *path);

#endif // UNIONFS_H
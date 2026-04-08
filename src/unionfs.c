#include "unionfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

void build_path(char *out, const char *dir, const char *virtual_path) {
    // TODO: join dir + virtual_path into out using snprintf
}

int is_whiteout(const char *virtual_path) {
    // TODO: check if upper_dir/.wh.filename exists
    return 0;
}

int resolve_path(const char *virtual_path, char *real_path_out) {
    // TODO: whiteout check -> upper -> lower priority lookup
    return -ENOENT;
}

int unionfs_getattr(const char *path, struct stat *stbuf,
                    struct fuse_file_info *fi) {
    (void) fi;

    if (strcmp(path, "/") == 0) {
        if (lstat(UNIONFS_DATA->upper_dir, stbuf) == -1) {
            return -errno;
        }
        return 0;
    }

    return -ENOENT;
}

int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags) {
    // TODO: list upper + lower merged, skip whiteouts and dupes
    return 0;
}

int unionfs_open(const char *path, struct fuse_file_info *fi) {
    // TODO: if write + file only in lower -> CoW copy to upper
    return 0;
}

int unionfs_read(const char *path, char *buf, size_t size,
                 off_t offset, struct fuse_file_info *fi) {
    // TODO: resolve path, open, pread, close
    return 0;
}

int unionfs_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi) {
    // TODO: open upper path, pwrite, close
    return 0;
}

int unionfs_create(const char *path, mode_t mode,
                   struct fuse_file_info *fi) {
    // TODO: create new file in upper_dir
    return 0;
}

int unionfs_unlink(const char *path) {
    // TODO: if upper -> delete. if lower -> create .wh. marker
    return 0;
}

int unionfs_mkdir(const char *path, mode_t mode) {
    // TODO: mkdir in upper_dir
    return 0;
}

int unionfs_rmdir(const char *path) {
    // TODO: rmdir in upper_dir
    return 0;
}
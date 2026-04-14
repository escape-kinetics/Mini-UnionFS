#include "unionfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(int argc, char *argv[]) {

    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mount_point>\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = malloc(sizeof(struct mini_unionfs_state));
    if (state == NULL) {
        perror("malloc failed");
        return 1;
    }

    /* Resolve to absolute paths before fuse_main daemonizes (which changes CWD). */
    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);
    char *mount_abs  = realpath(argv[3], NULL);

    if (!state->lower_dir || !state->upper_dir || !mount_abs) {
        perror("realpath failed");
        return 1;
    }

    /* Run as a daemon (no -f flag) so the calling shell can continue. */
    char *fuse_argv[] = { argv[0], mount_abs, NULL };
    int fuse_argc = 2;

    return fuse_main(fuse_argc, fuse_argv, &unionfs_oper, state);
}
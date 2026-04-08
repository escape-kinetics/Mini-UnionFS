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

    state->lower_dir = strdup(argv[1]);
    state->upper_dir = strdup(argv[2]);


    char *fuse_argv[] = { argv[0], argv[3], "-f", NULL };
    int fuse_argc = 3;

    return fuse_main(fuse_argc, fuse_argv, &unionfs_oper, state);
}
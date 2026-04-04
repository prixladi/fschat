#ifndef FSCHAT_FUSE__H
#define FSCHAT_FUSE__H

#include "fschat.h"

struct fuse_operations fschat_get_fuse_operations(struct fschat *fschat);

#endif

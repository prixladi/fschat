#ifndef UPDATER__H
#define UPDATER__H

#include "fschat.h"
#include "utils/log.h"
#include "stdbool.h"

struct updater
{
    struct fschat *fschat;
    pthread_t tid;
    bool exiting;
};

int updater_init(struct updater *updater, struct fschat *fschat);
int updater_start(struct updater *updater);
int updater_stop(struct updater *updater);

#endif

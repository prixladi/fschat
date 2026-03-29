#ifndef UPDATER__H
#define UPDATER__H

#include "channels.h"
#include "utils/log.h"
#include "stdbool.h"

struct updater
{
    struct channel_store *store;
    pthread_t tid;
    bool exiting;
};

int updater_init(struct updater *updater, struct channel_store *store);
int updater_start(struct updater *updater);
int updater_stop(struct updater *updater);

#endif

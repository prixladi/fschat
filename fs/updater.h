#ifndef updater__H
#define updater__H

#include "fschat.h"
#include "stdbool.h"
#include "api-client.h"

#include "utils/log.h"

struct updater
{
    struct fschat *fschat;
    struct api_client *api_client;
    pthread_t tid;
    bool exiting;
};

int updater_init(struct updater *updater, struct fschat *fschat, struct api_client *api_client);
int updater_start(struct updater *updater);
int updater_stop(struct updater *updater);

#endif

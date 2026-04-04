#ifndef FSCHAT__H
#define FSCHAT__H

#include <stddef.h>

#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K
#endif
#include <pthread.h>
#include <stdbool.h>

#include "api-client.h"

struct fschat
{
    pthread_rwlock_t *lock;

    char *username;

    struct fschat_channel **channels;
    int channel_count;

    struct api_client *api_client;

    pthread_t channels_sync_tid;
    pthread_t messages_sync_tid;

    bool stopped;
};

struct fschat_options
{
    char *default_username;
    char *api_base_url;
};

struct fschat_channel
{
    long id;
    char *name;
    char *contents;
    size_t contents_len;
    long latest_message_timestamp;
};

int fschat_init(struct fschat *fschat, struct fschat_options *options);
int fschat_start(struct fschat *fschat);
int fschat_stop(struct fschat *fschat);
void fschat_free(struct fschat *fschat);

// Semi-internal APIs, should be used only internally or in fschat-fuse

char *fschat_copy_username_locked(struct fschat *fschat);
int fschat_replace_username_locked(struct fschat *fschat, char *username);

struct fschat_channel *fschat_channel_create(long id, const char *name);
struct fschat_channel *fschat_channel_find_by_name(struct fschat *fschat, const char *name);
struct fschat_channel *fschat_channel_find_by_id(struct fschat *fschat, long id);
int fschat_channel_add(struct fschat *fschat, struct fschat_channel *channel);
int fschat_channel_remove_at(struct fschat *fschat, int pos);
void fschat_channel_free(struct fschat_channel *channel);

int fschat_lock_for_reading(struct fschat *fschat);
int fschat_lock_for_writing(struct fschat *fschat);
int fschat_unlock(struct fschat *fschat);

#endif

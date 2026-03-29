#ifndef CHANNELS__H
#define CHANNELS__H

#include <stddef.h>

#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K
#endif
#include <pthread.h>

struct channel
{
    char *name;
    char *contents;
    size_t contents_len;
    struct channel *next;
    pthread_rwlock_t *lock;
};

struct channel_store
{
    pthread_rwlock_t *lock;
    struct channel *channels;
};

int channel_store_init(struct channel_store *store);
int channel_store_free(struct channel_store *store);
int channel_store_lock_for_reading(struct channel_store *store);
int channel_store_lock_for_writing(struct channel_store *store);
int channel_store_unlock(struct channel_store *store);

struct channel *channel_create(const char *name, const char *initial_text);
int channel_lock_for_reading(struct channel *channel);
int channel_lock_for_writing(struct channel *channel);
int channel_unlock(struct channel *channel);
struct channel *find_channel_for_reading(struct channel_store *store, const char *name);
int channel_free(struct channel *channel);

#endif

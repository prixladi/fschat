#include "stdlib.h"
#include "string.h"

#include "channels.h"
#include "utils/log.h"

int
channel_store_init(struct channel_store *store)
{
    memset(store, 0, sizeof(struct channel_store));

    store->lock = malloc(sizeof(pthread_rwlock_t));
    if (pthread_rwlock_init(store->lock, NULL) != 0)
    {
        log_error("Unable to init channels lock.\n");
        free(store->lock);
        store->lock = NULL;
        return 1;
    }

    return 0;
}

int
channel_store_lock_for_reading(struct channel_store *store)
{
    return pthread_rwlock_rdlock(store->lock);
}

int
channel_store_lock_for_writing(struct channel_store *store)
{
    return pthread_rwlock_wrlock(store->lock);
}

int
channel_store_unlock(struct channel_store *store)
{
    return pthread_rwlock_unlock(store->lock);
}

int
channel_store_free(struct channel_store *store)
{
    channel_store_lock_for_writing(store);
    struct channel *cursor = store->channels;
    while (cursor)
    {
        struct channel *next_cursor = cursor->next;
        channel_free(cursor);
        cursor = next_cursor;
    };

    channel_store_unlock(store);

    if (store->lock)
        pthread_rwlock_destroy(store->lock);

    free(store->lock);

    store->lock = NULL;
    store->channels = NULL;

    return 0;
}

int
channel_lock_for_reading(struct channel *channel)
{
    return pthread_rwlock_rdlock(channel->lock);
}

int
channel_lock_for_writing(struct channel *channel)
{
    return pthread_rwlock_wrlock(channel->lock);
}

int
channel_unlock(struct channel *channel)
{
    return pthread_rwlock_unlock(channel->lock);
}

struct channel *
channel_create(const char *name, const char *initial_text)
{
    pthread_rwlock_t *lock = malloc(sizeof(pthread_rwlock_t));
    if (pthread_rwlock_init(lock, NULL) != 0)
    {
        log_error("Unable to init channel lock.\n");
        free(lock);
        return NULL;
    }

    struct channel *channel = calloc(1, sizeof(struct channel));
    channel->lock = lock;
    channel->name = str_dup(name);
    channel->contents = initial_text ? str_dup(initial_text) : str_dup("");
    channel->contents_len = strlen(channel->contents);

    return channel;
}

struct channel *
find_channel_for_reading(struct channel_store *store, const char *name)
{
    channel_store_lock_for_reading(store);
    struct channel *cursor = store->channels;
    while (cursor)
    {
        if (strcmp(name, cursor->name) == 0)
        {
            pthread_rwlock_rdlock(cursor->lock);
            break;
        }
        cursor = cursor->next;
    };
    channel_store_unlock(store);
    return cursor;
}

int
channel_free(struct channel *channel)
{
    if (channel->lock)
        channel_lock_for_writing(channel);

    free(channel->name);
    free(channel->contents);

    channel->name = NULL;
    channel->contents = NULL;

    if (channel->lock)
    {
        channel_unlock(channel);
        pthread_rwlock_destroy(channel->lock);
        free(channel->lock);
        channel->lock = NULL;
    }

    free(channel);

    return 0;
}

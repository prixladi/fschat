#include "stdlib.h"
#include "string.h"

#include "fs.h"
#include "utils/log.h"

int
fs_init(struct fs *fs, const char *username)
{
    memset(fs, 0, sizeof(struct fs));

    fs->lock = malloc(sizeof(pthread_rwlock_t));
    if (pthread_rwlock_init(fs->lock, NULL) != 0)
    {
        log_error("Unable to init channels lock.\n");
        free(fs->lock);
        fs->lock = NULL;
        return 1;
    }
    fs->username = str_dup(username);

    log_info("Filesystem initialized, username '%s'\n", fs->username);

    return 0;
}

char *
fs_copy_username_locked(struct fs *fs)
{
    fs_lock_for_reading(fs);
    char *username = str_dup(fs->username);
    fs_unlock(fs);
    return username;
}

int
fs_replace_username_locked(struct fs *fs, char *username)
{
    fs_lock_for_writing(fs);
    free(fs->username);
    fs->username = str_dup(username);
    log_info("Username changed to '%s'\n", fs->username);
    fs_unlock(fs);
    return 0;
}

int
fs_free(struct fs *fs)
{
    fs_lock_for_writing(fs);
    struct channel *cursor = fs->channels;
    while (cursor)
    {
        struct channel *next_cursor = cursor->next;
        channel_free(cursor);
        cursor = next_cursor;
    };
    free(fs->username);

    fs_unlock(fs);

    if (fs->lock)
        pthread_rwlock_destroy(fs->lock);

    free(fs->lock);

    fs->lock = NULL;
    fs->channels = NULL;
    fs->username = NULL;

    return 0;
}

int
fs_lock_for_reading(struct fs *fs)
{
    return pthread_rwlock_rdlock(fs->lock);
}

int
fs_lock_for_writing(struct fs *fs)
{
    return pthread_rwlock_wrlock(fs->lock);
}

int
fs_unlock(struct fs *fs)
{
    return pthread_rwlock_unlock(fs->lock);
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
find_channel_for_reading(struct fs *fs, const char *name)
{
    fs_lock_for_reading(fs);
    struct channel *cursor = fs->channels;
    while (cursor)
    {
        if (strcmp(name, cursor->name) == 0)
        {
            pthread_rwlock_rdlock(cursor->lock);
            break;
        }
        cursor = cursor->next;
    };
    fs_unlock(fs);
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

#include "stdlib.h"
#include "string.h"

#include "fschat.h"
#include "utils/log.h"

#define MIN_USERNAME_LENGTH 3
#define MAX_USERNAME_LENGTH 32

#define USERNAME_VALID(username) \
    ((strlen(username) >= MIN_USERNAME_LENGTH) && (strlen(username) <= MAX_USERNAME_LENGTH))
#define PRINT_INVALID_USERNAME_ERROR \
    log_error("Username must be between %d and %d characters long\n", MIN_USERNAME_LENGTH, MAX_USERNAME_LENGTH)

int
fschat_init(struct fschat *fschat, const char *username)
{
    if (!USERNAME_VALID(username))
    {
        PRINT_INVALID_USERNAME_ERROR;
        return 1;
    }

    memset(fschat, 0, sizeof(struct fschat));

    fschat->lock = malloc(sizeof(pthread_rwlock_t));
    if (pthread_rwlock_init(fschat->lock, NULL) != 0)
    {
        log_error("Unable to init channels lock.\n");
        free(fschat->lock);
        fschat->lock = NULL;
        return 1;
    }
    fschat->username = str_dup(username);

    log_info("Filesystem initialized, username '%s'\n", fschat->username);

    return 0;
}

char *
fschat_copy_username_locked(struct fschat *fschat)
{
    fschat_lock_for_reading(fschat);
    char *username = str_dup(fschat->username);
    fschat_unlock(fschat);
    return username;
}

int
fschat_replace_username_locked(struct fschat *fschat, char *username)
{
    if (!USERNAME_VALID(username))
    {
        PRINT_INVALID_USERNAME_ERROR;
        return 1;
    }

    fschat_lock_for_writing(fschat);
    free(fschat->username);
    fschat->username = str_dup(username);
    fschat_unlock(fschat);

    log_info("Username changed to '%s'\n", username);
    return 0;
}

void
fschat_free(struct fschat *fschat)
{
    fschat_lock_for_writing(fschat);
    struct channel *cursor = fschat->channels;
    while (cursor)
    {
        struct channel *next_cursor = cursor->next;
        channel_free(cursor);
        cursor = next_cursor;
    };
    free(fschat->username);

    fschat_unlock(fschat);

    if (fschat->lock)
        pthread_rwlock_destroy(fschat->lock);

    free(fschat->lock);

    fschat->lock = NULL;
    fschat->channels = NULL;
    fschat->username = NULL;
}

struct channel *
find_channel(struct fschat *fschat, const char *name)
{
    struct channel *cursor = fschat->channels;
    while (cursor)
    {
        if (strcmp(name, cursor->name) == 0)
            break;
        cursor = cursor->next;
    };
    return cursor;
}

int
fschat_lock_for_reading(struct fschat *fschat)
{
    return pthread_rwlock_rdlock(fschat->lock);
}

int
fschat_lock_for_writing(struct fschat *fschat)
{
    return pthread_rwlock_wrlock(fschat->lock);
}

int
fschat_unlock(struct fschat *fschat)
{
    return pthread_rwlock_unlock(fschat->lock);
}

struct channel *
channel_create(long id, const char *name, const char *initial_text)
{
    struct channel *channel = calloc(1, sizeof(struct channel));

    channel->id = id;
    channel->name = str_dup(name);
    channel->contents = initial_text ? str_dup(initial_text) : str_dup("");
    channel->contents_len = strlen(channel->contents);

    return channel;
}

void
channel_free(struct channel *channel)
{
    free(channel->name);
    free(channel->contents);

    memset(channel, 0, sizeof(struct channel));

    free(channel);
}

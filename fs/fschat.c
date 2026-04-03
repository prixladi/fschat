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

    fschat->channels = NULL;
    fschat->channel_count = 0;

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

    for (int i = 0; i < fschat->channel_count; i++)
    {
        struct channel *curr = fschat->channels[i];
        channel_free(curr);
    }
    free(fschat->channels);
    fschat->channel_count = 0;

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
channel_create(long id, const char *name)
{
    struct channel *channel = calloc(1, sizeof(struct channel));

    channel->id = id;
    channel->name = str_dup(name);
    channel->contents = str_dup("");
    channel->contents_len = strlen(channel->contents);

    return channel;
}

struct channel *
channel_find_by_name(struct fschat *fschat, const char *name)
{
    for (int i = 0; i < fschat->channel_count; i++)
    {
        struct channel *curr = fschat->channels[i];
        if (strcmp(name, curr->name) == 0)
            return curr;
    }
    return NULL;
}

struct channel *
channel_find_by_id(struct fschat *fschat, long id)
{
    for (int i = 0; i < fschat->channel_count; i++)
    {
        struct channel *curr = fschat->channels[i];
        if (curr->id == id)
            return curr;
    }
    return NULL;
}

const size_t channel_p_size = sizeof(struct channel *);
int
channel_remove_at(struct fschat *fschat, int pos)
{
    if (pos < 0 || !fschat->channel_count || pos >= fschat->channel_count)
        return -1;

    int new_size = fschat->channel_count - 1;
    struct channel **new_channels = malloc(channel_p_size * new_size);

    if (pos > 0)
        memcpy(new_channels, fschat->channels, channel_p_size * pos);
    if (pos + 1 < fschat->channel_count)
        memcpy(new_channels + pos, fschat->channels + pos + 1, channel_p_size * (fschat->channel_count - pos));

    channel_free(fschat->channels[pos]);
    free(fschat->channels);
    fschat->channels = new_channels;
    fschat->channel_count = new_size;

    return 0;
}

int
channel_add(struct fschat *fschat, struct channel *channel)
{
    int new_size = fschat->channel_count + 1;
    struct channel **new_channels = malloc(channel_p_size * new_size);

    if (fschat->channel_count)
        memcpy(new_channels, fschat->channels, fschat->channel_count * channel_p_size);

    new_channels[new_size - 1] = channel;

    free(fschat->channels);
    fschat->channels = new_channels;
    fschat->channel_count = new_size;

    return 0;
}

void
channel_free(struct channel *channel)
{
    free(channel->name);
    free(channel->contents);

    memset(channel, 0, sizeof(struct channel));
    free(channel);
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

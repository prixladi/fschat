#ifndef fschat__H
#define fschat__H

#include <stddef.h>

#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K
#endif
#include <pthread.h>

struct channel
{
    long id;
    char *name;
    char *contents;
    size_t contents_len;
    struct channel *next;
    long latest_message_timestamp;
};

struct fschat
{
    char *username;
    struct channel *channels;
    pthread_rwlock_t *lock;
};

int fschat_init(struct fschat *fschat, const char *username);
void fschat_free(struct fschat *fschat);
char *fschat_copy_username_locked(struct fschat *fschat);
int fschat_replace_username_locked(struct fschat *fschat, char *username);
int fschat_lock_for_reading(struct fschat *fschat);
int fschat_lock_for_writing(struct fschat *fschat);
int fschat_unlock(struct fschat *fschat);

struct channel *channel_create(long id, const char *name, const char *initial_text);
struct channel *find_channel(struct fschat *fschat, const char *name);
void channel_free(struct channel *channel);

#endif

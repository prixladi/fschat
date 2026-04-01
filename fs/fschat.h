#ifndef fschat__H
#define fschat__H

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

struct channel *channel_create(const char *name, const char *initial_text);
int channel_lock_for_reading(struct channel *channel);
int channel_lock_for_writing(struct channel *channel);
int channel_unlock(struct channel *channel);
struct channel *find_channel_for_reading(struct fschat *fschat, const char *name);
void channel_free(struct channel *channel);

#endif

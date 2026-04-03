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
    long latest_message_timestamp;
};

struct fschat
{
    pthread_rwlock_t *lock;
    char *username;
    struct channel **channels;
    int channel_count;
};

int fschat_init(struct fschat *fschat, const char *username);
void fschat_free(struct fschat *fschat);
char *fschat_copy_username_locked(struct fschat *fschat);
int fschat_replace_username_locked(struct fschat *fschat, char *username);

struct channel *channel_create(long id, const char *name);
struct channel *channel_find_by_name(struct fschat *fschat, const char *name);
struct channel *channel_find_by_id(struct fschat *fschat, long id);
int channel_add(struct fschat *fschat, struct channel *channel);
int channel_remove_at(struct fschat *fschat, int pos);
void channel_free(struct channel *channel);

int fschat_lock_for_reading(struct fschat *fschat);
int fschat_lock_for_writing(struct fschat *fschat);
int fschat_unlock(struct fschat *fschat);

#endif

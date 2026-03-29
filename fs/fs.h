#ifndef FS__H
#define FS__H

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

struct fs
{
    char *username;
    struct channel *channels;
    pthread_rwlock_t *lock;
};

int fs_init(struct fs *fs, const char *username);
int fs_free(struct fs *fs);
char *fs_copy_username_locked(struct fs *fs);
int fs_replace_username_locked(struct fs *fs, char *username);
int fs_lock_for_reading(struct fs *fs);
int fs_lock_for_writing(struct fs *fs);
int fs_unlock(struct fs *fs);

struct channel *channel_create(const char *name, const char *initial_text);
int channel_lock_for_reading(struct channel *channel);
int channel_lock_for_writing(struct channel *channel);
int channel_unlock(struct channel *channel);
struct channel *find_channel_for_reading(struct fs *fs, const char *name);
int channel_free(struct channel *channel);

#endif

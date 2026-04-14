#define FUSE_USE_VERSION 31
#define _XOPEN_SOURCE 600

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "fschat-fuse.h"

#include "utils/log.h"
#include "utils/memory.h"
#include "utils/math.h"

#define USERNAME_FILENAME ".username"

#define MAX_WRITE_SIZE 64

static struct fschat *get_fschat();

static void *fs_init(struct fuse_conn_info *conn, struct fuse_config *cfg);
static int fs_getattr(const char *path, struct stat *stat_buf, struct fuse_file_info *fi);
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags);
static int fs_open(const char *path, struct fuse_file_info *fi);
static int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int fs_mknod(const char *path, mode_t mode, dev_t dev);
static int fs_unlink(const char *path);

struct fuse_operations
fschat_get_fuse_operations(struct fschat *fschat)
{
    (void)fschat;
    struct fuse_operations oper = {
        .init = fs_init,
        .getattr = fs_getattr,
        .readdir = fs_readdir,
        .open = fs_open,
        .read = fs_read,
        .write = fs_write,
        .mknod = fs_mknod,
        .unlink = fs_unlink,
    };

    return oper;
}

static struct fschat *
get_fschat()
{
    return fuse_get_context()->private_data;
}

static void *
fs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void)conn;
    (void)cfg;
    return get_fschat();
}

static int
fs_getattr(const char *path, struct stat *stat_buf, struct fuse_file_info *fi)
{
    (void)fi;
    struct fschat *fschat = get_fschat();

    memset(stat_buf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0)
    {
        stat_buf->st_mode = __S_IFDIR | 0755;
        stat_buf->st_nlink = 2;
        return 0;
    }

    if (strcmp(path + 1, USERNAME_FILENAME) == 0)
    {
        scoped char *username = fschat_copy_username_locked(fschat);
        stat_buf->st_mode = __S_IFREG | 0666;
        stat_buf->st_nlink = 1;
        stat_buf->st_size = strlen(username);
        return 0;
    }

    fschat_lock_for_reading(fschat);
    struct fschat_channel *channel = fschat_channel_find_by_name(fschat, path + 1);
    if (!channel)
    {
        fschat_unlock(fschat);
        return -ENOENT;
    }

    stat_buf->st_mode = __S_IFREG | 0666;
    stat_buf->st_nlink = 1;
    stat_buf->st_size = channel->contents_len;
    stat_buf->st_ctime = channel->created_at / 1000;
    stat_buf->st_mtime = MAX(channel->latest_message_timestamp / 1000, stat_buf->st_ctime);

    fschat_unlock(fschat);

    return 0;
}

static int
fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi,
           enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;
    struct fschat *fschat = get_fschat();

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS);
    filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS);

    filler(buf, USERNAME_FILENAME, NULL, 0, FUSE_FILL_DIR_PLUS);

    fschat_lock_for_reading(fschat);

    for (int i = 0; i < fschat->channel_count; i++)
    {
        struct fschat_channel *curr = fschat->channels[i];
        filler(buf, curr->name, NULL, 0, FUSE_FILL_DIR_PLUS);
    }

    fschat_unlock(fschat);

    return 0;
}

static int
fs_open(const char *path, struct fuse_file_info *fi)
{
    struct fschat *fschat = get_fschat();

    if (strcmp(path + 1, USERNAME_FILENAME) == 0)
        return 0;

    fschat_lock_for_reading(fschat);
    struct fschat_channel *channel = fschat_channel_find_by_name(fschat, path + 1);
    if (!channel)
    {
        fschat_unlock(fschat);
        return -ENOENT;
    }

    fi->direct_io = 1;
    fschat_unlock(fschat);

    return 0;
}

static int
fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct fschat *fschat = get_fschat();
    (void)fi;

    bool found = false;
    size_t len;

    if (strcmp(path + 1, USERNAME_FILENAME) == 0)
    {
        found = true;
        scoped char *username = fschat_copy_username_locked(fschat);

        len = MIN(size, strlen(username) - offset);
        if (len > 0)
            memcpy(buf, username + offset, len);
    }
    else
    {
        fschat_lock_for_reading(fschat);
        struct fschat_channel *channel = fschat_channel_find_by_name(fschat, path + 1);
        if (channel)
        {
            found = true;
            len = MIN(size, channel->contents_len - offset);
            if (len > 0)
                memcpy(buf, channel->contents + offset, len);
        }
        fschat_unlock(fschat);
    }

    if (!found)
        return -ENOENT;

    return len;
}

static int
fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct fschat *fschat = get_fschat();
    (void)fi;
    (void)offset;

    if (size > MAX_WRITE_SIZE)
    {
        log_error("Max write size exceeded, path - %s, size - %ld, max - %d\n", path, size, MAX_WRITE_SIZE);
        return -EIO;
    }

    size_t cpy_size = size;
    // Trim the trailing \n for usages such as `echo "Hey" > fschat/channel`
    while (cpy_size > 0 && buf[cpy_size - 1] == '\n')
        cpy_size--;

    if (strcmp(path + 1, USERNAME_FILENAME) == 0)
    {
        scoped char *username = malloc(cpy_size + 1);
        username[cpy_size] = '\0';
        memcpy(username, buf, cpy_size);

        int swap_result = fschat_replace_username_locked(fschat, username);
        if (swap_result != 0)
            return -EIO;

        return size;
    }

    fschat_lock_for_reading(fschat);
    struct fschat_channel *channel = fschat_channel_find_by_name(fschat, path + 1);
    if (!channel)
    {
        fschat_unlock(fschat);
        return -ENOENT;
    }

    long channel_id = channel->id;
    fschat_unlock(fschat);

    scoped char *username = fschat_copy_username_locked(fschat);
    scoped char *message = malloc((cpy_size + 1) * sizeof(char));
    message[cpy_size] = '\0';
    memcpy(message, buf, cpy_size);
    int result = api_message_post(fschat->api_client, channel_id, message, username, "testId");
    if (result != 0)
    {
        log_error("Unable to post message to channel with id '%ld'\n", channel_id);
        return -EIO;
    }

    return size;
}

static int
fs_mknod(const char *path, mode_t mode, dev_t dev)
{
    struct fschat *fschat = get_fschat();
    (void)mode;
    (void)dev;

    char *channel_name = (char *)path + 1;

    if (fschat_channel_find_by_name(fschat, channel_name) != NULL)
    {
        log_error("Unable to create channel %s because it already exists\n", channel_name);
        return -EIO;
    }

    struct api_channel api_channel = { 0 };
    int result = api_channel_create(fschat->api_client, channel_name, &api_channel);
    if (result != 0)
    {
        if (result < -1000)
            log_error(
                "Unable to parse create channel (%s) server  response, channel was probably created but it might appear with delay.\n",
                channel_name);
        else
            log_error("Unable to create channel (%s) on the server, result %d\n", channel_name, result);
        return -EIO;
    }

    fschat_lock_for_writing(fschat);

    int i = 0;
    for (; i < fschat->channel_count;)
    {
        if (fschat->channels[i]->id == api_channel.id)
            break;
        i++;
    }

    // There is a chance that updater already synchronized server state to the fschat store.
    // Note that we can't just wait for updater because caller can try open the file instantly after creating it and it would fail
    if (i == fschat->channel_count)
    {
        struct fschat_channel *channel = fschat_channel_create(api_channel.id, api_channel.name, api_channel.created_at);
        fschat_channel_add(fschat, channel);
    }

    fschat_unlock(fschat);

    log_info("Created channel '%s' with id %ld\n", api_channel.name, api_channel.id);

    api_channel_free(&api_channel);
    return 0;
}

static int
fs_unlink(const char *path)
{
    struct fschat *fschat = get_fschat();

    char *channel_name = (char *)path + 1;

    if (strcmp(path + 1, USERNAME_FILENAME) == 0)
    {
        log_error("Unable to remove username file because it is a protected file.\n");
        return -EIO;
    }

    struct fschat_channel *channel = fschat_channel_find_by_name(fschat, channel_name);
    if (!channel)
    {
        log_error("Unable to remove channel '%s' because it was not found\n", channel_name);
        return -EIO;
    }
    long channel_id = channel->id;

    int api_result = api_channel_delete(fschat->api_client, channel->id);
    if (api_result != 0)
    {
        log_error("Unable to remove channel from the server '%s', result %d\n", channel_name, api_result);
        return -EIO;
    }

    fschat_lock_for_writing(fschat);

    for (int i = 0; i < fschat->channel_count; i++)
    {
        struct fschat_channel *curr = fschat->channels[i];
        if (strcmp(channel_name, curr->name) == 0)
        {
            fschat_channel_remove_at(fschat, i);
            break;
        }
    }
    // We dont need to check if channel was found because the update thread might have already synced the state

    fschat_unlock(fschat);

    log_info("Removed channel '%s' with id %ld\n", channel_name, channel_id);

    return 0;
}

#define FUSE_USE_VERSION 31
#define _XOPEN_SOURCE 600

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#include "channels.h"
#include "updater.h"
#include "utils/log.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

static struct channel_store store;

static void *hello_init(struct fuse_conn_info *conn, struct fuse_config *cfg);
static int hello_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi,
                         enum fuse_readdir_flags flags);
static int hello_open(const char *path, struct fuse_file_info *fi);
static int hello_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int hello_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

int
main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    log_init(LOG_DEBUG);

    if (channel_store_init(&store) != 0)
    {
        log_critical("Unable to init channel store\n");
        return 1;
    }

    struct channel *channel1 = channel_create("channel2", "");
    struct channel *channel2 = channel_create("channel1", "");
    channel1->next = channel2;
    store.channels = channel1;

    struct updater updater = { 0 };

    if (updater_init(&updater, &store) != 0)
    {
        log_critical("Unable to init updater\n");
        return 2;
    }
    if (updater_start(&updater) != 0)
    {
        log_critical("Unable to start updater\n");
        return 3;
    }

    struct fuse_operations hello_oper = { .init = hello_init,
                                          .getattr = hello_getattr,
                                          .readdir = hello_readdir,
                                          .open = hello_open,
                                          .read = hello_read,
                                          .write = hello_write };

    int ret = fuse_main(argc, argv, &hello_oper, NULL);

    updater_stop(&updater);
    channel_store_free(&store);

    return ret;
}

static void *
hello_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void)conn;
    (void)cfg;
    return NULL;
}

static int
hello_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void)fi;

    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = __S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    struct channel *channel = find_channel_for_reading(&store, path + 1);
    if (!channel)
        return -ENOENT;

    stbuf->st_mode = __S_IFREG | 0666;
    stbuf->st_nlink = 1;
    stbuf->st_size = channel->contents_len;

    channel_unlock(channel);

    return 0;
}

static int
hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi,
              enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS);
    filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS);

    struct channel *cursor = store.channels;
    while (cursor)
    {
        filler(buf, cursor->name, NULL, 0, FUSE_FILL_DIR_PLUS);
        cursor = cursor->next;
    };

    return 0;
}

static int
hello_open(const char *path, struct fuse_file_info *fi)
{
    struct channel *channel = find_channel_for_reading(&store, path + 1);
    if (!channel)
        return -ENOENT;
    channel_unlock(channel);

    fi->direct_io = 1;

    return 0;
}

static int
hello_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    (void)fi;

    struct channel *channel = find_channel_for_reading(&store, path + 1);
    if (!channel)
        return -ENOENT;

    size_t len = MIN(size, channel->contents_len - offset);
    if (len > 0)
        memcpy(buf, channel->contents + offset, len);

    channel_unlock(channel);

    return len;
}

static int
hello_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    (void)path;
    (void)fi;
    (void)buf;

    struct channel *channel = find_channel_for_reading(&store, path + 1);
    if (!channel)
        return -ENOENT;

    printf("write %ld %ld\n", size, offset);

    channel_unlock(channel);

    return size;
}

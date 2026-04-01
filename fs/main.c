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

#include "fschat.h"
#include "updater.h"

#include "utils/log.h"
#include "utils/memory.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define USERNAME_FILENAME ".username"

#define OPTION(t, p) { t, offsetof(struct options, p), 1 }

static struct options
{
    char *username;
    int show_help;
} options;

static const struct fuse_opt option_spec[] = { OPTION("--username=%s", username), OPTION("-h", show_help),
                                               OPTION("--help", show_help), FUSE_OPT_END };

static void show_help(const char *progname);

static void *fs_init(struct fuse_conn_info *conn, struct fuse_config *cfg);
static int fs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags);
static int fs_open(const char *path, struct fuse_file_info *fi);
static int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

static int options_free(struct options *opts);

static struct fschat fschat;
int
main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    log_init(LOG_DEBUG);

    options.username = strdup(getlogin());

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
    {
        log_critical("Unable to parse args\n");
        return 1;
    }

    if (options.show_help)
    {
        show_help(argv[0]);
        assert(fuse_opt_add_arg(&args, "--help") == 0);
        args.argv[0][0] = '\0';
        int ret = fuse_main(args.argc, args.argv, NULL, NULL);
        fuse_opt_free_args(&args);
        return ret;
    }

    if (fschat_init(&fschat, options.username) != 0)
    {
        fuse_opt_free_args(&args);
        log_critical("Unable to init channel fschat\n");
        return 1;
    }

    struct channel *channel1 = channel_create("channel2", "");
    struct channel *channel2 = channel_create("channel1", "");
    channel1->next = channel2;
    fschat.channels = channel1;

    struct updater updater = { 0 };

    if (updater_init(&updater, &fschat) != 0)
    {
        log_critical("Unable to init updater\n");
        return 1;
    }
    if (updater_start(&updater) != 0)
    {
        log_critical("Unable to start updater\n");
        return 1;
    }

    struct fuse_operations oper = {
        .init = fs_init, .getattr = fs_getattr, .readdir = fs_readdir, .open = fs_open, .read = fs_read, .write = fs_write
    };

    log_info("FUSE starting\n");
    int ret = fuse_main(args.argc, args.argv, &oper, NULL);
    fuse_opt_free_args(&args);
    log_info("FUSE stopped\n");

    updater_stop(&updater);

    fschat_free(&fschat);
    options_free(&options);

    return ret;
}

static void
show_help(const char *progname)
{
    printf("usage: %s [options] <mountpoint>\n\n", progname);
    printf("File-system specific options:\n"
           "    --username=<s>      Display name of the user\n"
           "\n");
}

static void *
fs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void)conn;
    (void)cfg;
    return NULL;
}

static int
fs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void)fi;

    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = __S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    if (strcmp(path + 1, USERNAME_FILENAME) == 0)
    {
        scoped char *username = fschat_copy_username_locked(&fschat);
        stbuf->st_mode = __S_IFREG | 0666;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(username);
        return 0;
    }

    struct channel *channel = find_channel_for_reading(&fschat, path + 1);
    if (!channel)
        return -ENOENT;

    stbuf->st_mode = __S_IFREG | 0666;
    stbuf->st_nlink = 1;
    stbuf->st_size = channel->contents_len;

    channel_unlock(channel);

    return 0;
}

static int
fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi,
           enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS);
    filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS);

    filler(buf, USERNAME_FILENAME, NULL, 0, FUSE_FILL_DIR_PLUS);

    struct channel *cursor = fschat.channels;
    while (cursor)
    {
        filler(buf, cursor->name, NULL, 0, FUSE_FILL_DIR_PLUS);
        cursor = cursor->next;
    };

    return 0;
}

static int
fs_open(const char *path, struct fuse_file_info *fi)
{
    if (strcmp(path + 1, USERNAME_FILENAME) == 0)
        return 0;

    struct channel *channel = find_channel_for_reading(&fschat, path + 1);
    if (!channel)
        return -ENOENT;

    fi->direct_io = 1;
    channel_unlock(channel);

    return 0;
}

static int
fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    (void)fi;

    bool found = false;
    size_t len;

    if (strcmp(path + 1, USERNAME_FILENAME) == 0)
    {
        found = true;
        scoped char *username = fschat_copy_username_locked(&fschat);

        len = MIN(size, strlen(username) - offset);
        if (len > 0)
            memcpy(buf, username + offset, len);
    }
    else
    {
        struct channel *channel = find_channel_for_reading(&fschat, path + 1);
        if (channel)
        {
            found = true;
            len = MIN(size, channel->contents_len - offset);
            if (len > 0)
                memcpy(buf, channel->contents + offset, len);

            channel_unlock(channel);
        }
    }

    if (!found)
        return -ENOENT;

    return len;
}

static int
fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    (void)path;
    (void)fi;
    (void)buf;

    if (offset)
    {
        log_error("Offset during write is not allowed, path - %s", path);
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

        int swap_result = fschat_replace_username_locked(&fschat, username);
        if (swap_result != 0)
            return -EIO;

        return size;
    }

    struct channel *channel = find_channel_for_reading(&fschat, path + 1);
    if (!channel)
        return -ENOENT;

    printf("write %ld %ld\n", size, offset);

    channel_unlock(channel);

    return size;
}

static int
options_free(struct options *opts)
{
    free(opts->username);
    opts->username = NULL;
    return 0;
}

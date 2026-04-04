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
#include "fschat-fuse.h"
#include "api-client.h"

#include "utils/log.h"
#include "utils/memory.h"
#include "utils/math.h"

#define USERNAME_FILENAME ".username"

#define MAX_WRITE_SIZE 64

#define OPTION(t, p) { t, offsetof(struct options, p), 1 }

static struct options
{
    char *username;
    int show_help;
} options;

static const struct fuse_opt option_spec[] = { OPTION("--username=%s", username), OPTION("-h", show_help),
                                               OPTION("--help", show_help), FUSE_OPT_END };

static void show_help(const char *progname);

static int options_free(struct options *opts);

int
main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    log_init(LOG_DEBUG);

    options.username = strdup(getlogin());

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (fuse_opt_parse(&args, &options, option_spec, NULL) != 0)
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

    struct fschat_options fschat_options = { .api_base_url = "http://localhost:3000",
                                             .default_username = options.username };

    static struct fschat fschat;
    if (fschat_init(&fschat, &fschat_options) != 0)
    {
        log_critical("Unable to init fschat\n");
        return 2;
    }

    if (fschat_start(&fschat) != 0)
    {
        log_critical("Unable to start fschat\n");
        return 3;
    }

    struct fuse_operations oper = fschat_get_fuse_operations(&fschat);

    log_info("FUSE starting\n");
    int ret = fuse_main(args.argc, args.argv, &oper, &fschat);
    fuse_opt_free_args(&args);
    log_info("FUSE stopped\n");

    fschat_stop(&fschat);
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

static int
options_free(struct options *opts)
{
    free(opts->username);
    opts->username = NULL;
    return 0;
}

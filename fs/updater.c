#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "updater.h"

static void *channels_update_loop(void *data);

int
updater_init(struct updater *updater, struct fs *fs)
{
    log_info("Updater initialized\n");
    memset(updater, 0, sizeof(struct updater));
    updater->fs = fs;
    return 0;
}

int
updater_stop(struct updater *updater)
{
    log_info("Updater stopping...\n");
    if (updater->tid)
    {
        updater->exiting = true;
        pthread_join(updater->tid, NULL);
        updater->tid = 0;
    }
    log_info("Updater stoped\n");

    return 0;
}

int
updater_start(struct updater *updater)
{
    if (updater->tid)
    {
        log_error("Updater is already running under thread %ld", updater->tid);
        return 1;
    }

    pthread_create(&updater->tid, NULL, channels_update_loop, updater);

    log_info("Updater started\n");
    return 0;
}

static void *
channels_update_loop(void *data)
{
    struct updater *updater = data;
    struct fs *fs = updater->fs;

    size_t cnt = 0;
    while (!updater->exiting)
    {
        cnt++;

        fs_lock_for_writing(fs);

        struct channel *channel = fs->channels;
        while (channel)
        {
            char *str = str_printf("[%s-%ld]: Message Message Message Message", fs->username, cnt);

            channel_lock_for_writing(channel);
            fs_unlock(fs);

            size_t new_len = channel->contents_len + strlen(str);
            char *new_str = malloc(sizeof(char) * (new_len + 2));
            if (channel->contents_len)
                memcpy(new_str, channel->contents, channel->contents_len);

            memcpy(new_str + channel->contents_len, str, strlen(str));
            new_str[new_len] = '\n';
            new_str[new_len + 1] = '\0';

            free(channel->contents);
            channel->contents = new_str;
            channel->contents_len = new_len + 1;

            fs_lock_for_writing(fs);
            struct channel *next = channel->next;
            channel_unlock(channel);
            channel = next;
            
            free(str);
        };

        fs_unlock(fs);

        sleep(1);
    }

    return NULL;
}

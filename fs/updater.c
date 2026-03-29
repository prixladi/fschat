#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "updater.h"

static void *channels_update_loop(void *data);

int
updater_init(struct updater *updater, struct fschat *fschat)
{
    log_info("Updater initialized\n");
    memset(updater, 0, sizeof(struct updater));
    updater->fschat = fschat;
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
    struct fschat *fschat = updater->fschat;

    size_t cnt = 0;
    while (!updater->exiting)
    {
        cnt++;

        fschat_lock_for_writing(fschat);

        struct channel *channel = fschat->channels;
        while (channel)
        {
            char *str = str_printf("[%s-%ld]: Message Message Message Message", fschat->username, cnt);

            channel_lock_for_writing(channel);
            fschat_unlock(fschat);

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

            fschat_lock_for_writing(fschat);
            struct channel *next = channel->next;
            channel_unlock(channel);
            channel = next;

            free(str);
        };

        fschat_unlock(fschat);

        sleep(1);
    }

    return NULL;
}

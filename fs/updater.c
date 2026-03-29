#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "updater.h"

static void *channels_update_loop(void *data);

int
updater_init(struct updater *updater, struct channel_store *store)
{
    memset(updater, 0, sizeof(struct updater));
    updater->store = store;
    return 0;
}

int
updater_stop(struct updater *updater)
{
    if (updater->tid)
    {
        updater->exiting = true;
        pthread_join(updater->tid, NULL);
        updater->tid = 0;
    }

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

    return 0;
}

static void *
channels_update_loop(void *data)
{
    struct updater *updater = data;
    struct channel_store *store = updater->store;

    size_t cnt = 0;
    while (!updater->exiting)
    {
        cnt++;
        char *str = str_printf("[USER-%ld]: Message Message Message Message", cnt);

        channel_store_lock_for_writing(store);

        struct channel *channel = store->channels;
        while (channel)
        {
            channel_lock_for_writing(channel);
            channel_store_unlock(store);
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

            channel_store_lock_for_writing(store);
            struct channel *next = channel->next;
            channel_unlock(channel);
            channel = next;
        };

        channel_store_unlock(store);

        free(str);
        sleep(1);
    }

    return NULL;
}

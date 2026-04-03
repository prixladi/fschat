#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "updater.h"
#include "api-client.h"

#include "utils/memory.h"
#include "utils/math.h"

static void *channels_update_loop(void *data);
static void *channels_update_loop2(void *data);

int
updater_init(struct updater *updater, struct fschat *fschat, struct api_client *api_client)
{
    memset(updater, 0, sizeof(struct updater));

    updater->fschat = fschat;
    updater->api_client = api_client;

    log_info("Updater initialized\n");

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
        pthread_join(updater->tid2, NULL);
        updater->tid = 0;
        updater->tid2 = 0;
    }
    log_info("Updater stoped\n");

    return 0;
}

int
updater_start(struct updater *updater)
{
    if (updater->tid || updater->tid2)
    {
        log_error("Updater is already running under threads - '%ld' and '%ld'", updater->tid, updater->tid2);
        return 1;
    }

    pthread_create(&updater->tid, NULL, channels_update_loop, updater);
    pthread_create(&updater->tid2, NULL, channels_update_loop2, updater);

    log_info("Updater started\n");
    return 0;
}

static void *
channels_update_loop2(void *data)
{
    struct updater *updater = data;
    struct fschat *fschat = updater->fschat;
    struct api_client *api_client = updater->api_client;

    while (!updater->exiting)
    {
        sleep(1);

        struct api_channel_list api_channels;
        int result = api_channels_list(api_client, &api_channels);
        if (result != 0)
        {
            log_error("Unable to get channels, result '%d'\n", result);
            continue;
        }

        fschat_lock_for_reading(fschat);

        bool matching = true;
        for (int i = 0; i < api_channels.count && matching; i++)
        {
            int j = 0;
            for (; j < fschat->channel_count;)
            {
                if (strcmp(fschat->channels[j]->name, api_channels.items[i].name) == 0 &&
                    fschat->channels[j]->id == api_channels.items[i].id)
                    break;
                j++;
            }
            matching = j < fschat->channel_count;
        }

        for (int j = 0; j < fschat->channel_count && matching; j++)
        {
            int i = 0;
            for (; i < api_channels.count && matching;)
            {
                if (strcmp(fschat->channels[i]->name, api_channels.items[i].name) == 0 &&
                    fschat->channels[i]->id == api_channels.items[i].id)
                    break;
                i++;
            }
            matching = i < api_channels.count;
        }

        fschat_unlock(fschat);

        if (!matching)
        {
            fschat_lock_for_writing(fschat);

            log_info("Channels are not matching, proceeding with update\n");

            int added = 0;
            int renamed = 0;
            int removed = 0;

            for (int i = 0; i < fschat->channel_count; i++)
            {
                struct channel *channel = fschat->channels[i];

                int j = 0;
                for (; j < api_channels.count;)
                {
                    if (channel->id == api_channels.items[j].id)
                        break;
                    j++;
                }

                if (j >= api_channels.count)
                {
                    fschat_channel_remove_at(fschat, i);
                    log_info("Removed channel %s with id %ld\n", channel->name, channel->id);
                    removed++;
                }
            }

            for (int j = 0; j < api_channels.count; j++)
            {
                struct api_channel *api_channel = api_channels.items + j;

                int i = 0;
                for (; i < fschat->channel_count;)
                {
                    struct channel *channel = fschat->channels[i];
                    if (channel->id == api_channel->id)
                        break;
                    i++;
                }

                if (i >= fschat->channel_count)
                {
                    struct channel *channel = fschat_channel_create(api_channel->id, api_channel->name);
                    fschat_channel_add(fschat, channel);
                    log_info("Added channel %s with id %ld\n", channel->name, channel->id);
                    added++;
                }

                struct channel *channel = fschat->channels[i];
                if (strcmp(channel->name, api_channel->name) != 0)
                {
                    log_info("Renaming channel with id %ld from %s to %s\n", channel->id, channel->name,
                             api_channel->name);
                    free(channel->name);
                    channel->name = str_dup(api_channel->name);
                }
            }

            fschat_unlock(fschat);
            log_info("Channels updated - '%d' removed, '%d' added, '%d' renamed.\n", removed, added, renamed);
        }

        api_channel_list_free(&api_channels);
    }

    return NULL;
}

static void *
channels_update_loop(void *data)
{
    struct updater *updater = data;
    struct fschat *fschat = updater->fschat;
    struct api_client *api_client = updater->api_client;

    while (!updater->exiting)
    {
        fschat_lock_for_reading(fschat);

        for (int i = 0; i < fschat->channel_count; i++)
        {
            struct channel *channel = fschat->channels[i];

            long channel_id = channel->id;
            long latest_message_timestamp = channel->latest_message_timestamp;

            fschat_unlock(fschat);

            struct api_message_list messages;
            int result = api_messages_list(api_client, channel_id, latest_message_timestamp + 1, &messages);
            if (result != 0)
            {
                log_error("Unable to get messages for channel '%ld', result '%d'\n", channel_id, result);
                fschat_lock_for_reading(fschat);
                continue;
            }

            if (!messages.count)
            {
                api_message_list_free(&messages);
                fschat_lock_for_reading(fschat);
                continue;
            }

            log_debug("Received '%d' new messages for channel '%ld'\n", messages.count, channel_id);

            char **lines = malloc(sizeof(char *) * messages.count);
            long new_latest_message_timestamp = latest_message_timestamp;

            size_t total_len = 0;
            for (int i = 0; i < messages.count; i++)
            {
                struct api_message *m = messages.items + i;
                new_latest_message_timestamp = MAX(new_latest_message_timestamp, m->timestamp);
                lines[i] = str_printf("[%s/%s) | %ld]: %s\n", m->username, m->user_id, m->timestamp, m->text);
                total_len += strlen(lines[i]);
            }

            scoped char *full = malloc(sizeof(char) * (total_len + 1));
            full[total_len] = '\0';

            size_t cursor = 0;
            for (int i = 0; i < messages.count; i++)
            {
                scoped char *line = lines[i];
                size_t len = strlen(line);
                memcpy(full + cursor, line, len);
                cursor += len;
            }
            free(lines);
            api_message_list_free(&messages);

            fschat_lock_for_writing(fschat);

            // Need to find the channel again because it is possible it got removed
            // TODO: Optimization - add starting index here because it should almost always be on the same spot
            channel = fschat_channel_find_by_id(fschat, channel_id);
            if (channel)
            {
                char *new_content = str_concat(channel->contents, full);
                free(channel->contents);
                channel->contents = new_content;
                channel->latest_message_timestamp = new_latest_message_timestamp;
                channel->contents_len = strlen(new_content);
            }

            fschat_unlock(fschat);

            fschat_lock_for_reading(fschat);
        };
        fschat_unlock(fschat);

        sleep(1);
    }

    return NULL;
}

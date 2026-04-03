#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "updater.h"
#include "api-client.h"

#include "utils/memory.h"
#include "utils/math.h"

static void *channels_update_loop(void *data);

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
    struct api_client *api_client = updater->api_client;

    while (!updater->exiting)
    {
        fschat_lock_for_reading(fschat);

        struct channel *channel = fschat->channels;
        while (channel)
        {
            struct channel *next = channel->next;

            long channel_id = channel->id;
            long latest_message_timestamp = channel->latest_message_timestamp;

            fschat_unlock(fschat);

            struct api_message_list messages;
            int result = api_messages_list(api_client, channel_id, latest_message_timestamp + 1, &messages);
            if (result != 0)
            {
                log_error("Unable to get messages for channel '%ld', result '%d'\n", channel_id, result);
                channel = next;
                fschat_lock_for_reading(fschat);
                continue;
            }

            if (!messages.count)
            {
                api_message_list_free(&messages);
                channel = next;
                fschat_lock_for_reading(fschat);
                continue;
            }

            log_debug("'%d' new messages for channel '%ld'", messages.count, channel_id);

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

            char *new_content = str_concat(channel->contents, full);
            free(channel->contents);
            channel->contents = new_content;
            channel->latest_message_timestamp = new_latest_message_timestamp;
            channel->contents_len = strlen(new_content);

            fschat_unlock(fschat);

            fschat_lock_for_reading(fschat);
            channel = next;
        };
        fschat_unlock(fschat);

        sleep(1);
    }

    return NULL;
}

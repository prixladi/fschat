#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fschat.h"
#include "utils/math.h"
#include "utils/log.h"
#include "utils/memory.h"

#define MIN_USERNAME_LENGTH 3
#define MAX_USERNAME_LENGTH 32

#define USERNAME_VALID(username) \
    ((strlen(username) >= MIN_USERNAME_LENGTH) && (strlen(username) <= MAX_USERNAME_LENGTH))
#define PRINT_INVALID_USERNAME_ERROR \
    log_error("Username must be between %d and %d characters long\n", MIN_USERNAME_LENGTH, MAX_USERNAME_LENGTH)

static void *channels_sync_loop(void *data);
static void *messages_sync_loop(void *data);
static int init_channels(struct fschat *fschat);

int
fschat_init(struct fschat *fschat, struct fschat_options *options)
{
    memset(fschat, 0, sizeof(struct fschat));

    if (!options->default_username || !USERNAME_VALID(options->default_username))
    {
        PRINT_INVALID_USERNAME_ERROR;
        return -1;
    }

    if (!options->api_base_url)
    {
        log_error("Missing api base url in fschat init options\n");
        return -1;
    }

    fschat->lock = malloc(sizeof(pthread_rwlock_t));
    if (pthread_rwlock_init(fschat->lock, NULL) != 0)
    {
        log_error("Unable to init channels lock\n");
        free(fschat->lock);
        fschat->lock = NULL;
        return 1;
    }
    fschat->username = str_dup(options->default_username);

    struct api_client *api_client = malloc(sizeof(struct api_client *));
    if (api_client_init(api_client, options->api_base_url) != 0)
    {
        log_error("Unable to init api client\n");
        return -1;
    }

    fschat->api_client = api_client;

    // No need to check the result, if init fails the worker will sync the channels
    init_channels(fschat);

    log_info("FSchat filesystem initialized, username '%s'\n", fschat->username);

    return 0;
}

int
fschat_start(struct fschat *fschat)
{
    if (fschat->channels_sync_tid || fschat->messages_sync_tid)
    {
        log_error("FSchat is already running under threads - '%ld' and '%ld'", fschat->channels_sync_tid,
                  fschat->messages_sync_tid);
        return 1;
    }

    pthread_create(&fschat->channels_sync_tid, NULL, channels_sync_loop, fschat);
    pthread_create(&fschat->messages_sync_tid, NULL, messages_sync_loop, fschat);

    log_info("FSchat started\n");
    return 0;
}

int
fschat_stop(struct fschat *fschat)
{
    log_info("FSchat stopping...\n");
    fschat->stopped = true;
    if (fschat->channels_sync_tid)
    {
        pthread_join(fschat->channels_sync_tid, NULL);
        fschat->channels_sync_tid = 0;
    }
    if (fschat->messages_sync_tid)
    {
        pthread_join(fschat->messages_sync_tid, NULL);
        fschat->messages_sync_tid = 0;
    }
    log_info("FSchat stoped\n");

    return 0;
}

static int
init_channels(struct fschat *fschat)
{
    fschat->channels = NULL;
    fschat->channel_count = 0;

    struct api_client *api_client = fschat->api_client;

    struct api_channel_list api_channels;
    int result = api_channels_list(api_client, &api_channels);
    if (result != 0)
    {
        log_error("Unable to initialize channels, result '%d'\n", result);
        return 1;
    }

    if (api_channels.count < 1)
    {
        log_info("No channels found during initialization");
        api_channel_list_free(&api_channels);
        return 0;
    }

    struct fschat_channel **channels = malloc(sizeof(struct fschat_channel *) * api_channels.count);
    for (int i = 0; i < api_channels.count; i++)
    {
        struct api_channel *api_channel = api_channels.items + i;
        struct fschat_channel *channel = fschat_channel_create(api_channel->id, api_channel->name);
        channels[i] = channel;
        log_info("Add channel '%s' with id '%ld' during initialization\n", channel->name, channel->id);
    }

    fschat->channels = channels;
    fschat->channel_count = api_channels.count;
    log_info("Added '%d' channels during initialization\n", api_channels.count);
    api_channel_list_free(&api_channels);

    return 0;
}

static void *
channels_sync_loop(void *data)
{
    struct fschat *fschat = data;
    struct api_client *api_client = fschat->api_client;

    while (!fschat->stopped)
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
                struct fschat_channel *channel = fschat->channels[i];

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
                    struct fschat_channel *channel = fschat->channels[i];
                    if (channel->id == api_channel->id)
                        break;
                    i++;
                }

                if (i >= fschat->channel_count)
                {
                    struct fschat_channel *channel = fschat_channel_create(api_channel->id, api_channel->name);
                    fschat_channel_add(fschat, channel);
                    log_info("Added channel %s with id %ld\n", channel->name, channel->id);
                    added++;
                }

                struct fschat_channel *channel = fschat->channels[i];
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
messages_sync_loop(void *data)
{
    struct fschat *fschat = data;
    struct api_client *api_client = fschat->api_client;

    while (!fschat->stopped)
    {
        fschat_lock_for_reading(fschat);

        for (int i = 0; i < fschat->channel_count; i++)
        {
            struct fschat_channel *channel = fschat->channels[i];

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

char *
fschat_copy_username_locked(struct fschat *fschat)
{
    fschat_lock_for_reading(fschat);
    char *username = str_dup(fschat->username);
    fschat_unlock(fschat);
    return username;
}

int
fschat_replace_username_locked(struct fschat *fschat, char *username)
{
    if (!USERNAME_VALID(username))
    {
        PRINT_INVALID_USERNAME_ERROR;
        return 1;
    }

    fschat_lock_for_writing(fschat);
    free(fschat->username);
    fschat->username = str_dup(username);
    fschat_unlock(fschat);

    log_info("Username changed to '%s'\n", username);
    return 0;
}

void
fschat_free(struct fschat *fschat)
{
    fschat_lock_for_writing(fschat);

    for (int i = 0; i < fschat->channel_count; i++)
    {
        struct fschat_channel *curr = fschat->channels[i];
        fschat_channel_free(curr);
    }
    free(fschat->channels);
    fschat->channel_count = 0;

    free(fschat->username);

    fschat_unlock(fschat);

    if (fschat->lock)
        pthread_rwlock_destroy(fschat->lock);

    free(fschat->lock);

    fschat->lock = NULL;
    fschat->channels = NULL;
    fschat->username = NULL;
}

struct fschat_channel *
fschat_channel_create(long id, const char *name)
{
    struct fschat_channel *channel = calloc(1, sizeof(struct fschat_channel));

    channel->id = id;
    channel->name = str_dup(name);
    channel->contents = str_dup("");
    channel->contents_len = strlen(channel->contents);

    return channel;
}

struct fschat_channel *
fschat_channel_find_by_name(struct fschat *fschat, const char *name)
{
    for (int i = 0; i < fschat->channel_count; i++)
    {
        struct fschat_channel *curr = fschat->channels[i];
        if (strcmp(name, curr->name) == 0)
            return curr;
    }
    return NULL;
}

struct fschat_channel *
fschat_channel_find_by_id(struct fschat *fschat, long id)
{
    for (int i = 0; i < fschat->channel_count; i++)
    {
        struct fschat_channel *curr = fschat->channels[i];
        if (curr->id == id)
            return curr;
    }
    return NULL;
}

const size_t channel_p_size = sizeof(struct fschat_channel *);
int
fschat_channel_remove_at(struct fschat *fschat, int pos)
{
    if (pos < 0 || !fschat->channel_count || pos >= fschat->channel_count)
        return -1;

    int new_size = fschat->channel_count - 1;
    struct fschat_channel **new_channels = malloc(channel_p_size * new_size);

    if (pos > 0)
        memcpy(new_channels, fschat->channels, channel_p_size * pos);
    if (pos + 1 < fschat->channel_count)
        memcpy(new_channels + pos, fschat->channels + pos + 1, channel_p_size * (fschat->channel_count - pos));

    fschat_channel_free(fschat->channels[pos]);
    free(fschat->channels);
    fschat->channels = new_channels;
    fschat->channel_count = new_size;

    return 0;
}

int
fschat_channel_add(struct fschat *fschat, struct fschat_channel *channel)
{
    int new_size = fschat->channel_count + 1;
    struct fschat_channel **new_channels = malloc(channel_p_size * new_size);

    if (fschat->channel_count)
        memcpy(new_channels, fschat->channels, fschat->channel_count * channel_p_size);

    new_channels[new_size - 1] = channel;

    free(fschat->channels);
    fschat->channels = new_channels;
    fschat->channel_count = new_size;

    return 0;
}

void
fschat_channel_free(struct fschat_channel *channel)
{
    free(channel->name);
    free(channel->contents);

    memset(channel, 0, sizeof(struct fschat_channel));
    free(channel);
}

int
fschat_lock_for_reading(struct fschat *fschat)
{
    return pthread_rwlock_rdlock(fschat->lock);
}

int
fschat_lock_for_writing(struct fschat *fschat)
{
    return pthread_rwlock_wrlock(fschat->lock);
}

int
fschat_unlock(struct fschat *fschat)
{
    return pthread_rwlock_unlock(fschat->lock);
}

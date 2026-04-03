#ifndef api_client__H
#define api_client__H

struct api_client
{
    char *base_url;
};

struct api_channel
{
    long id; 
    char *name; 
};

struct api_channel_list
{
    struct api_channel *items;
    int count;
};

struct api_message
{
    long id;
    char *text;
    char *username;
    char *user_id;
    long timestamp;
    long channel_id;
};

struct api_message_list
{
    struct api_message *items;
    int count;
};

int api_client_init(struct api_client *client, const char *base_url);
void api_client_free(struct api_client *client);

int api_channels_list(const struct api_client *client, struct api_channel_list *list);
int api_channel_create(const struct api_client *client, const char *name);
int api_channel_delete(const struct api_client *client, long channel_id);
int api_message_post(const struct api_client *client, long channel_id, const char *text, const char *username,
                     const char *user_id);
int api_messages_list(const struct api_client *client, long channel_id, long since_ms, struct api_message_list *list);

void api_channel_free(struct api_channel *channel);
void api_channel_list_free(struct api_channel_list *list);
void api_message_free(struct api_message *order);
void api_message_list_free(struct api_message_list *list);

#endif

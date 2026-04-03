#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "api-client.h"

#include "external/cJSON.h"

#include "utils/string.h"
#include "utils/memory.h"

struct response_buf
{
    char *data;
    size_t len;
};

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userdata);
static char *raw_request(const char *method, const char *url, const char *json_body, long *status_out);

static int parse_channel(const cJSON *obj, struct api_channel *channel);
static int parse_message(const cJSON *obj, struct api_message *message);

int
api_client_init(struct api_client *client, const char *base_url)
{
    memset(client, 0, sizeof(struct api_client));
    client->base_url = str_dup(base_url);
    return 0;
}

void
api_client_free(struct api_client *client)
{
    free(client->base_url);
    client->base_url = NULL;
}

int
api_channels_list(const struct api_client *client, struct api_channel_list *list)
{
    memset(list, 0, sizeof(struct api_channel_list));

    long status;
    scoped char *url = str_printf("%s/channels", client->base_url);
    scoped char *body = raw_request("GET", url, NULL, &status);

    if (!body || status != 200)
    {
        return status != 0 ? status : -1;
    }

    cJSON *root = cJSON_Parse(body);

    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        return -1;
    }

    int len = cJSON_GetArraySize(root);
    list->items = calloc(len, sizeof(struct api_channel));
    list->count = len;

    cJSON *item = root;
    for (int i = 0; i < len; i++)
    {
        if (!item)
            return -1;
        struct api_channel channel = { 0 };
        int res = parse_channel(item, &channel);
        if (res != 0)
            return -1;
        list->items[i] = channel;
    }

    cJSON_Delete(root);
    return 0;
}
int
api_channel_create(const struct api_client *client, const char *name)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "name", name);
    scoped char *body_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    long status;
    scoped char *url = str_printf("%s/channels", client->base_url);
    scoped char *body = raw_request("POST", url, body_str, &status);

    if (status != 201)
        return status != 0 ? status : -1;

    return 0;
}

int
api_channel_delete(const struct api_client *client, long channel_id)
{
    long status;
    scoped char *url = str_printf("%s/channels/%ld", client->base_url, channel_id);
    scoped char *body = raw_request("DELETE", url, NULL, &status);

    if (status != 204)
        return status != 0 ? status : -1;

    return 0;
}

int
api_message_post(const struct api_client *client, long channel_id, const char *text, const char *username,
                 const char *user_id)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "text", text);
    cJSON_AddStringToObject(req, "username", username);
    cJSON_AddStringToObject(req, "user_id", user_id);
    scoped char *body_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    long status;
    scoped char *url = str_printf("%s/channels/%ld/messages", client->base_url, channel_id);
    scoped char *body = raw_request("POST", url, body_str, &status);

    if (status != 201)
        return status != 0 ? status : -1;

    return 0;
}

int
api_messages_list(const struct api_client *client, long channel_id, long since_ms, struct api_message_list *list)
{
    memset(list, 0, sizeof(struct api_channel_list));

    long status;
    scoped char *url;
    if (since_ms > 0)
        url = str_printf("%s/channels/%ld/messages?since=%ld", client->base_url, channel_id, since_ms);
    else
        url = str_printf("%s/channels/%ld/messages", client->base_url, channel_id);
    scoped char *body = raw_request("GET", url, NULL, &status);
    if (!body || status != 200)
    {
        return status != 0 ? status : -1;
    }

    cJSON *root = cJSON_Parse(body);

    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        return -1;
    }

    int len = cJSON_GetArraySize(root);
    list->items = calloc(len, sizeof(struct api_message));
    list->count = len;

    cJSON *item = root->child;
    for (int i = 0; i < len; i++)
    {
        if (!item)
            return -1;
        struct api_message message = { 0 };
        int res = parse_message(item, &message);
        if (res != 0)
        {
            cJSON_Delete(root);
            return -1;
        }
        list->items[i] = message;
        item = item->next;
    }

    cJSON_Delete(root);
    return 0;
}

void
api_channel_free(struct api_channel *channel)
{
    free(channel->name);
    channel->name = NULL;
}

void
api_channel_list_free(struct api_channel_list *list)
{
    for (int i = 0; i < list->count; i++)
        api_channel_free(list->items + i);

    free(list->items);
    list->items = NULL;

    list->count = 0;
}

void
api_message_free(struct api_message *message)
{
    free(message->text);
    free(message->username);
    free(message->user_id);

    message->text = NULL;
    message->username = NULL;
    message->user_id = NULL;
}

void
api_message_list_free(struct api_message_list *list)
{
    for (int i = 0; i < list->count; i++)
        api_message_free(list->items + i);

    free(list->items);
    list->items = NULL;

    list->count = 0;
}

static size_t
write_cb(void *contents, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    struct response_buf *buf = userdata;

    char *ptr = realloc(buf->data, buf->len + total + 1);
    if (!ptr)
        return 0;

    buf->data = ptr;
    memcpy(buf->data + buf->len, contents, total);
    buf->len += total;
    buf->data[buf->len] = '\0';

    return total;
}

static char *
raw_request(const char *method, const char *url, const char *json_body, long *status_out)
{
    struct response_buf buf = { .data = NULL, .len = 0 };
    *status_out = -1;

    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;

    struct curl_slist *headers = NULL;
    if (json_body)
        headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    if (headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (json_body)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);

    CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status_out);

    if (headers)
        curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return buf.data;
}

static int
parse_channel(const cJSON *obj, struct api_channel *channel)
{
    memset(channel, 0, sizeof(struct api_channel));
    if (!cJSON_IsObject(obj))
        return -1;

    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "name");

    if (cJSON_IsNumber(id))
        channel->id = (long)id->valuedouble;
    else
        return -1;

    if (cJSON_IsString(name) && name->valuestring)
        channel->name = strdup(name->valuestring);
    else
        return -1;

    return 0;
}

static int
parse_message(const cJSON *obj, struct api_message *message)
{
    memset(message, 0, sizeof(struct api_message));
    if (!cJSON_IsObject(obj))
    {
        return -1;
    }

    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *text = cJSON_GetObjectItemCaseSensitive(obj, "text");
    cJSON *username = cJSON_GetObjectItemCaseSensitive(obj, "username");
    cJSON *user_id = cJSON_GetObjectItemCaseSensitive(obj, "user_id");
    cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(obj, "timestamp");
    cJSON *channel_id = cJSON_GetObjectItemCaseSensitive(obj, "channel_id");

    if (cJSON_IsNumber(id))
        message->id = (long)id->valuedouble;
    else
    {
        return -1;
    }
    if (cJSON_IsString(text) && text->valuestring)
        message->text = strdup(text->valuestring);
    else
    {
        return -1;
    }
    if (cJSON_IsString(username) && username->valuestring)
        message->username = strdup(username->valuestring);
    else
    {
        return -1;
    }
    if (cJSON_IsString(user_id) && user_id->valuestring)
        message->user_id = strdup(user_id->valuestring);
    else
    {
        return -1;
    }
    if (cJSON_IsNumber(timestamp))
        message->timestamp = (long)timestamp->valuedouble;
    else
    {
        return -1;
    }
    if (cJSON_IsNumber(channel_id))
        message->channel_id = (long)channel_id->valuedouble;
    else
    {
        return -1;
    }

    return 0;
}

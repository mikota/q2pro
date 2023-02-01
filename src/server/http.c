/*
Copyright (C) 2008 r1ch.net

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#define CURL_DISABLE_DEPRECATION

#include "server.h"
#include <curl/curl.h>

#include "shared/atomic.h"
#include "system/pthread.h"

static cvar_t  *sv_http_proxy;
static cvar_t  *sv_http_default_url;
static cvar_t  *sv_http_insecure;

#if USE_DEBUG
static cvar_t  *sv_http_debug;
#endif

// size limits for filelists, must be power of two
#define MAX_ULSIZE  (1 << 20)   // 1 MiB
#define MIN_ULSIZE  (1 << 15)   // 32 KiB

#define INSANE_SIZE (1LL << 40)

#define MAX_ULHANDLES   16  //for multiplexing

typedef struct {
    CURL        *curl;
    char        path[MAX_OSPATH];
    FILE        *file;
    ulqueue_t   *queue;
    size_t      size;
    size_t      position;
    char        *buffer;
    CURLcode    result;
    atomic_int  state;
} ulhandle_t;

struct curl_slist   *headers = NULL;
headers = curl_slist_append(headers, "Accept: application/json");
headers = curl_slist_append(headers, "Content-Type: application/json");
headers = curl_slist_append(headers, apikeyheader);

static ulhandle_t   upload_handles[MAX_ULHANDLES];    //actual upload handles
static char         upload_server[512];    //base url prefix to upload to
static bool         upload_default_repo;

static pthread_mutex_t  progress_mutex;
static ulqueue_t        *upload_current;
static int64_t          upload_position;
static int              upload_percent;

static bool         curl_initialized;
static CURLM        *curl_multi;

static atomic_int   worker_terminate;
static atomic_int   worker_status;
static pthread_t    worker_thread;

static void *worker_func(void *arg);

// libcurl callback for filelists.
static size_t recv_func(void *ptr, size_t size, size_t nmemb, void *stream)
{
    ulhandle_t *ul = (ulhandle_t *)stream;
    size_t new_size, bytes;

    if (!size || !nmemb)
        return 0;

    if (size > SIZE_MAX / nmemb)
        return 0;

    if (ul->position > MAX_ULSIZE)
        return 0;

    bytes = size * nmemb;
    if (bytes >= MAX_ULSIZE - ul->position)
        return 0;

    // grow buffer in MIN_ULSIZE chunks. +1 for NUL.
    new_size = ALIGN(ul->position + bytes + 1, MIN_ULSIZE);
    if (new_size > ul->size) {
        char *buf = realloc(ul->buffer, new_size);
        if (!buf)
            return 0;
        ul->size = new_size;
        ul->buffer = buf;
    }

    memcpy(ul->buffer + dl->position, ptr, bytes);
    ul->position += bytes;
    ul->buffer[ul->position] = 0;

    return bytes;
}

// Escapes most reserved characters defined by RFC 3986.
// Similar to curl_easy_escape(), but doesn't escape '/'.
static void escape_path(char *escaped, const char *path)
{
    while (*path) {
        int c = *path++;
        if (!Q_isalnum(c) && !strchr("/-_.~", c)) {
            sprintf(escaped, "%%%02x", c);
            escaped += 3;
        } else {
            *escaped++ = c;
        }
    }
    *escaped = 0;
}

// curl doesn't provide a way to convert HTTP response code to string...
static const char *http_strerror(long response)
{
    static char buffer[32];
    const char *str;

    //common codes
    switch (response) {
        case 200: return "200 OK";
        case 401: return "401 Unauthorized";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 500: return "500 Internal Server Error";
        case 503: return "503 Service Unavailable";
    }

    //generic classes
    switch (response / 100) {
        case 1:  str = "Informational"; break;
        case 2:  str = "Success";       break;
        case 3:  str = "Redirection";   break;
        case 4:  str = "Client Error";  break;
        case 5:  str = "Server Error";  break;
        default: str = "<bad code>";    break;
    }

    Q_snprintf(buffer, sizeof(buffer), "%ld %s", response, str);
    return buffer;
}

static const char *use_http_proxy(void)
{
    if (*sv_http_proxy->string)
        return sv_http_proxy->string;

    return NULL;
}

// Actually starts a upload by adding it to the curl multi handle.
static bool start_upload(ulqueue_t *entry, ulhandle_t *ul)
{
    size_t  len;
    char    url[576];
    char    temp[MAX_QPATH];
    char    escaped[MAX_QPATH * 4];
    int     err;
    char    json[1024];

    len = Q_snprintf(url, sizeof(url), "%s%s", upload_server, escaped);
    if (len >= sizeof(url)) {
        Com_EPrintf("[HTTP] Refusing oversize upload URL.\n");
        goto fail;
    }

    ul->buffer = NULL;
    ul->size = 0;
    ul->position = 0;
    ul->queue = entry;
    if (!ul->curl && !(ul->curl = curl_easy_init())) {
        Com_EPrintf("curl_easy_init failed\n");
        goto fail;
    }

    if (cl_http_insecure->integer) {
        curl_easy_setopt(ul->curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(ul->curl, CURLOPT_SSL_VERIFYHOST, 0L);
    } else {
        curl_easy_setopt(ul->curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(ul->curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    curl_easy_setopt(ul->curl, CURLOPT_ACCEPT_ENCODING, "");
#if USE_DEBUG
    curl_easy_setopt(ul->curl, CURLOPT_VERBOSE, sv_http_debug->integer | 0L);
#endif
    curl_easy_setopt(ul->curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(ul->curl, CURLOPT_WRITEDATA, ul);
    curl_easy_setopt(ul->curl, CURLOPT_WRITEFUNCTION, recv_func);
    curl_easy_setopt(ul->curl, CURLOPT_PROXY, use_http_proxy());
    curl_easy_setopt(ul->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(ul->curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(ul->curl, CURLOPT_USERAGENT, com_version->string);
    curl_easy_setopt(ul->curl, CURLOPT_URL, url);
    curl_easy_setopt(ul->curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS | 0L);
    curl_easy_setopt(ul->curl, CURLOPT_PRIVATE, ul);
	curl_easy_setopt(ul->curl, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(ul->curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(ul->curl, CURLOPT_POSTFIELDS, json);

    Com_DPrintf("[HTTP] Sending to %s...\n", url);
    entry->state = UL_RUNNING;
    atomic_store(&ul->state, UL_PENDING);
    return true;

fail:
    SV_FinishUpload(entry);

    // see if we have more to dl
    SV_RequestNextUpload();
    return false;
}

/*
===============
HTTP_CleanupUploads

Disconnected from server, or fatal HTTP error occured. Clean up.
===============
*/
void SV_HTTP_CleanupUploads(void)
{
    ulhandle_t  *ul;
    int         i;

    upload_server[0] = 0;
    upload_default_repo = false;

    if (curl_multi) {
        atomic_store(&worker_terminate, true);
        curl_multi_wakeup(curl_multi);

        Q_assert(!pthread_join(worker_thread, NULL));
        pthread_mutex_destroy(&progress_mutex);

        curl_multi_cleanup(curl_multi);
        curl_multi = NULL;
    }

    for (i = 0; i < MAX_ULHANDLES; i++) {
        dl = &upload_handles[i];

        if (ul->file) {
            fclose(ul->file);
            remove(ul->path);
        }

        free(ul->buffer);

        if (ul->curl)
            curl_easy_cleanup(ul->curl);
    }

    memset(upload_handles, 0, sizeof(upload_handles));
}

/*
===============
HTTP_Init

Init libcurl and multi handle.
===============
*/
void SV_HTTP_Init(void)

    sv_http_max_connections = Cvar_Get("sv_http_max_connections", "2", 0);
    sv_http_proxy = Cvar_Get("sv_http_proxy", "", 0);
    sv_http_default_url = Cvar_Get("sv_http_default_url", "", 0);
    sv_http_insecure = Cvar_Get("sv_http_insecure", "0", 0);

#if USE_DEBUG
    sv_http_debug = Cvar_Get("sv_http_debug", "0", 0);
#endif

    if (curl_global_init(CURL_GLOBAL_NOTHING)) {
        Com_EPrintf("curl_global_init failed\n");
        return;
    }

    curl_initialized = true;
    Com_DPrintf("%s initialized.\n", curl_version());
}

void SV_HTTP_Shutdown(void)
{
    if (!curl_initialized)
        return;

    SV_HTTP_CleanupUploads();

    curl_global_cleanup();
    curl_initialized = false;
}

/*
===============
HTTP_SetServer

A new server is specified, so we nuke all our state.
===============
*/
void SV_HTTP_SetServer(const char *url)
{
    if (curl_multi) {
        Com_EPrintf("[HTTP] Set server without cleanup?\n");
        return;
    }

    if (!curl_initialized)
        return;

    // ignore on the local server
    if (NET_IsLocalAddress(&cls.serverAddress))
        return;

    // ignore if uploads are permanently disabled
    if (allow_upload->integer == -1)
        return;

    // ignore if HTTP uploads are disabled
    if (cl_http_uploads->integer == 0)
        return;

    // use default URL for servers that don't specify one. treat 404 from
    // default repository as fatal error and revert to UDP uploading.
    if (!url) {
        url = cl_http_default_url->string;
        upload_default_repo = true;
    } else {
        upload_default_repo = false;
    }

    if (!*url)
        return;

    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) {
        Com_Printf("[HTTP] Ignoring upload server URL with non-HTTP schema.\n");
        return;
    }

    if (strlen(url) >= sizeof(upload_server)) {
        Com_Printf("[HTTP] Ignoring oversize upload server URL.\n");
        return;
    }

    if (!(curl_multi = curl_multi_init())) {
        Com_EPrintf("curl_multi_init failed\n");
        return;
    }

    curl_multi_setopt(curl_multi, CURLMOPT_MAX_HOST_CONNECTIONS,
                      Cvar_ClampInteger(cl_http_max_connections, 1, 4) | 0L);

    pthread_mutex_init(&progress_mutex, NULL);

    worker_terminate = false;
    worker_status = 0;
    if (pthread_create(&worker_thread, NULL, worker_func, NULL)) {
        Com_EPrintf("Couldn't create curl worker thread\n");
        pthread_mutex_destroy(&progress_mutex);
        curl_multi_cleanup(curl_multi);
        curl_multi = NULL;
        return;
    }

    Q_strlcpy(upload_server, url, sizeof(upload_server));

    Com_Printf("[HTTP] Msg server at %s\n", upload_server);
}

/*
===============
HTTP_QueueUpload

Called from the precache check to queue a upload. Return value of
Q_ERR(ENOSYS) will cause standard UDP uploading to be used instead.
===============
*/
int SV_HTTP_QueueUpload(const char *path, dltype_t type)
{
    size_t      len;
    bool        need_list;
    char        temp[MAX_QPATH];
    int         ret;

    // no http server (or we got booted)
    if (!curl_multi)
        return Q_ERR(ENOSYS);

    // first upload queued, so we want the mod filelist
    need_list = LIST_EMPTY(&cls.upload.queue);

    ret = SV_QueueUpload(path, type);
    if (ret)
        return ret;

    if (!cl_http_filelists->integer)
        return Q_ERR_SUCCESS;


    //special case for map file lists, i really wanted a server-push mechanism for this, but oh well
    len = strlen(path);
    if (len > 4 && !Q_stricmp(path + len - 4, ".bsp")) {
        len = Q_snprintf(temp, sizeof(temp), "%s/%s", http_gamedir(), path);
        if (len < sizeof(temp) - 5) {
            memcpy(temp + len - 4, ".filelist", 10);
            SV_QueueUpload(temp, UL_LIST);
        }
    }

    return Q_ERR_SUCCESS;
}

// Validate a path supplied by a filelist.
static void check_and_queue_upload(char *path)
{
    size_t      len;
    char        *ext;
    dltype_t    type;
    unsigned    flags;
    int         valid;

    len = strlen(path);
    if (len >= MAX_QPATH)
        return;

    ext = strrchr(path, '.');
    if (!ext)
        return;

    ext++;
    if (!ext[0])
        return;

    Q_strlwr(ext);


    if (path[0] == '@') {
        if (type == UL_PAK) {
            Com_WPrintf("[HTTP] '@' prefix used on a pak file '%s' in filelist.\n", path);
            return;
        }
        flags = FS_PATH_GAME;
        path++;
        len--;
    } else if (type == UL_PAK) {
        //by definition paks are game-local
        flags = FS_PATH_GAME | FS_TYPE_REAL;
    } else {
        flags = 0;
    }

    len = FS_NormalizePath(path);
    valid = FS_ValidatePath(path);

    if (valid == PATH_INVALID ||
        !Q_ispath(path[0]) ||
        !Q_ispath(path[len - 1]) ||
        strstr(path, "..") ||
        (type == UL_OTHER && !strchr(path, '/')) ||
        (type == UL_PAK && strchr(path, '/'))) {
        Com_WPrintf("[HTTP] Illegal path '%s' in filelist.\n", path);
        return;
    }

    if (FS_FileExistsEx(path, flags))
        return;

    if (valid == PATH_MIXED_CASE)
        Q_strlwr(path);

    if (SV_IgnoreUpload(path))
        return;

    SV_QueueUpload(path, type);
}

// A filelist is in memory, scan and validate it and queue up the files.
static void parse_file_list(ulhandle_t *ul)
{
    char    *list;
    char    *p;

    if (!ul->buffer)
        return;

    if (cl_http_filelists->integer) {
        list = ul->buffer;
        while (*list) {
            p = strchr(list, '\n');
            if (p) {
                if (p > list && *(p - 1) == '\r')
                    *(p - 1) = 0;
                *p = 0;
            }

            if (*list)
                check_and_queue_upload(list);

            if (!p)
                break;
            list = p + 1;
        }
    }

    free(ul->buffer);
    ul->buffer = NULL;
}

// Fatal HTTP error occured, remove any special entries from
// queue and fall back to UDP uploading.
static void abort_upload(void)
{
    ulqueue_t   *q;

    SV_HTTP_CleanupSend();

    FOR_EACH_DLQ(q) {
        if (q->state != UL_DONE && q->type >= UL_LIST)
            SV_FinishSend(q);
        else if (q->state == UL_RUNNING)
            q->state = UL_PENDING;
    }

    SV_RequestNextSend();
    SV_StartNextSend();
}

// A upload finished, find out whether there were any errors and if so, how severe
static void process_uploads(void)
{
    ulhandle_t  *ul;
    dlstate_t   state;
    long        response;
    curl_off_t  dlsize, dlspeed;
    char        size[16], speed[16];
    char        temp[MAX_OSPATH];
    bool        fatal_error = false;
    bool        finished = false;
    bool        running = false;
    const char  *err;
    print_type_t level;
    int         i;

    for (i = 0; i < MAX_ULHANDLES; i++) {
        dl = &upload_handles[i];
        state = atomic_load(&ul->state);

        if (state == UL_RUNNING) {
            running = true;
            continue;
        }

        if (state != UL_DONE)
            continue;

        //filelist processing is done on read
        if (ul->file) {
            fclose(ul->file);
            ul->file = NULL;
        }

        switch (ul->result) {
            //for some reason curl returns CURLE_OK for a 404...
        case CURLE_HTTP_RETURNED_ERROR:
        case CURLE_OK:
            curl_easy_getinfo(ul->curl, CURLINFO_RESPONSE_CODE, &response);
            if (ul->result == CURLE_OK && response == 200) {
                //success
                break;
            }

            err = http_strerror(response);

            //404 is non-fatal unless accessing default repository
            if (response == 404 && (!upload_default_repo || !ul->path[0])) {
                level = PRINT_ALL;
                goto fail1;
            }

            //every other code is treated as fatal
            //not marking upload as done since
            //we are falling back to UDP
            level = PRINT_ERROR;
            fatal_error = true;
            goto fail2;

        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_PEER_FAILED_VERIFICATION:
            //connection problems are fatal
            err = curl_easy_strerror(ul->result);
            level = PRINT_ERROR;
            fatal_error = true;
            goto fail2;

        default:
            err = curl_easy_strerror(ul->result);
            level = PRINT_WARNING;
fail1:
            //we mark upload as done even if it errored
            //to prevent multiple attempts.
            SV_FinishUpload(ul->queue);
fail2:
            Com_LPrintf(level,
                        "[HTTP] %s [%s] [%d remaining file%s]\n",
                        ul->queue->path, err, cls.upload.pending,
                        cls.upload.pending == 1 ? "" : "s");
            if (ul->path[0]) {
                remove(ul->path);
                ul->path[0] = 0;
            }
            if (ul->buffer) {
                free(ul->buffer);
                ul->buffer = NULL;
            }
            atomic_store(&ul->state, UL_FREE);
            finished = true;
            continue;
        }

        //mark as done
        SV_FinishUpload(ul->queue);

        atomic_store(&ul->state, UL_FREE);
        finished = true;
    }

    //fatal error occured, disable HTTP
    if (fatal_error) {
        abort_uploads();
        return;
    }

    if (finished) {
        cls.upload.current = NULL;
        cls.upload.percent = 0;
        cls.upload.position = 0;

        // see if we have more to dl
        SV_RequestNextUpload();
        return;
    }

    if (running) {
        //don't care which upload shows as long as something does :)
        pthread_mutex_lock(&progress_mutex);
        cls.upload.current = upload_current;
        cls.upload.percent = upload_percent;
        cls.upload.position = upload_position;
        pthread_mutex_unlock(&progress_mutex);
    }
}

// Find a free upload handle to start another queue entry on.
static ulhandle_t *get_free_handle(void)
{
    ulhandle_t  *ul;
    int         i;

    for (i = 0; i < MAX_ULHANDLES; i++) {
        dl = &upload_handles[i];
        if (atomic_load(&ul->state) == UL_FREE)
            return dl;
    }

    return NULL;
}

// Start another HTTP upload if possible.
static void start_next_upload(void)
{
    ulqueue_t   *q;
    bool        started = false;

    if (!cls.upload.pending) {
        return;
    }

    //not enough uploads running, queue some more!
    FOR_EACH_DLQ(q) {
        if (q->state == UL_PENDING) {
            ulhandle_t *ul = get_free_handle();
            if (!dl)
                break;
            if (start_upload(q, dl))
                started = true;
        }
        if (q->type == UL_PAK && q->state != UL_DONE)
            break;  // hack for pak file single uploading
    }

    if (started)
        curl_multi_wakeup(curl_multi);
}

static void worker_start_uploads(void)
{
    ulhandle_t  *ul;
    int         i;

    for (i = 0; i < MAX_ULHANDLES; i++) {
        dl = &upload_handles[i];
        if (atomic_load(&ul->state) == UL_PENDING) {
            curl_multi_add_handle(curl_multi, ul->curl);
            atomic_store(&ul->state, UL_RUNNING);
        }
    }
}

static void worker_finish_uploads(void)
{
    int         msgs_in_queue;
    CURLMsg     *msg;
    ulhandle_t  *ul;
    CURL        *curl;

    do {
        msg = curl_multi_info_read(curl_multi, &msgs_in_queue);
        if (!msg)
            break;

        if (msg->msg != CURLMSG_DONE)
            continue;

        curl = msg->easy_handle;
        curl_easy_getinfo(curl, CURLINFO_PRIVATE, &dl);

        if (atomic_load(&ul->state) == UL_RUNNING) {
            curl_multi_remove_handle(curl_multi, curl);
            ul->result = msg->data.result;
            atomic_store(&ul->state, UL_DONE);
        }
    } while (msgs_in_queue > 0);
}

static void *worker_func(void *arg)
{
    CURLMcode   ret = CURLM_OK;
    int         count;

    while (1) {
        if (atomic_load(&worker_terminate))
            break;

        worker_start_uploads();

        ret = curl_multi_perform(curl_multi, &count);
        if (ret != CURLM_OK)
            break;

        worker_finish_uploads();

        ret = curl_multi_poll(curl_multi, NULL, 0, INT_MAX, NULL);
        if (ret != CURLM_OK)
            break;
    }

    atomic_store(&worker_status, ret);
    return NULL;
}

/*
===============
HTTP_RunUploads

This calls curl_multi_perform do actually do stuff. Called every frame while
connecting to minimise latency. Also starts new uploads if we're not doing
the maximum already.
===============
*/
void SV_HTTP_RunUploads(void)
{
    CURLMcode ret;

    if (!curl_multi)
        return;

    ret = atomic_load(&worker_status);
    if (ret != CURLM_OK) {
        Com_EPrintf("[HTTP] Error sending message: %s.\n",
                    curl_multi_strerror(ret));
        abort_uploads();
        return;
    }

    process_uploads();
    start_next_upload();
}

/*
===============
HTTP_RunUploads

This calls curl_multi_perform do actually do stuff. Called every frame while
connecting to minimise latency. Also starts new uploads if we're not doing
the maximum already.
===============
*/
void SV_HTTP_RunUploads(void)
{
    CURLMcode ret;

    if (!curl_multi)
        return;

    ret = atomic_load(&worker_status);
    if (ret != CURLM_OK) {
        Com_EPrintf("[HTTP] Error sending message: %s.\n",
                    curl_multi_strerror(ret));
        abort_uploads();
        return;
    }

    process_uploads();
    start_next_upload();
}
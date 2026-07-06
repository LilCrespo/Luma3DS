#include <3ds.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

#include "menus/spotify.h"
#include "draw.h"
#include "ifile.h"
#include "minisoc.h"

#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/memory_buffer_alloc.h>

#define SPOTIFY_CONFIG_DIR        "/config/spotify"
#define SPOTIFY_TOKEN_PATH        SPOTIFY_CONFIG_DIR "/token.json"

#define SPOTIFY_API_HOST        "api.spotify.com"
#define SPOTIFY_ACCOUNTS_HOST   "accounts.spotify.com"

#define SPOTIFY_HTTP_RESPONSE_SIZE 8192

#define SPOTIFY_API_IP_U32      ((35u << 24) | (186u << 16) | (224u << 8) | 24u)
#define SPOTIFY_ACCOUNTS_IP_U32 ((35u << 24) | (186u << 16) | (224u << 8) | 24u)

#define SPOTIFY_HOTKEY_MODIFIERS (KEY_R | KEY_SELECT)
#define SPOTIFY_HOTKEY_NEXT      (SPOTIFY_HOTKEY_MODIFIERS | KEY_RIGHT)
#define SPOTIFY_HOTKEY_PREVIOUS  (SPOTIFY_HOTKEY_MODIFIERS | KEY_LEFT)
#define SPOTIFY_HOTKEY_PLAYPAUSE (SPOTIFY_HOTKEY_MODIFIERS | KEY_UP)
#define SPOTIFY_HOTKEY_STATUS    (SPOTIFY_HOTKEY_MODIFIERS | KEY_DOWN)

typedef struct Spotify_Result {
    int tokenRet;
    int sock;
    int connectRet;
    int configRet;
    int setupRet;
    int hostnameRet;
    int handshakeRet;
    int handshakeLoops;
    int requestRet;
    int writeRet;
    int readRet;
    int refreshRet;
    bool didRefresh;
    char statusLine[96];
} Spotify_Result;

Menu spotifyMenu = {
    "Spotify",
    {
        { "Next track", METHOD, .method = &SpotifyMenu_Next },
        { "Previous track", METHOD, .method = &SpotifyMenu_Previous },
        { "Play/Pause toggle", METHOD, .method = &SpotifyMenu_PlayPause },
        { "Play", METHOD, .method = &SpotifyMenu_Play },
        { "Pause", METHOD, .method = &SpotifyMenu_Pause },
        { "Playback status", METHOD, .method = &SpotifyMenu_Status },
        { "Hotkey controls", METHOD, .method = &SpotifyMenu_Controls },
        {},
    }
};

static u32 spotifyRngState = 0x12345678;

static mbedtls_ssl_context spotifySsl;
static mbedtls_ssl_config spotifyConf;
static unsigned char spotifyMbedHeap[64 * 1024] __attribute__((aligned(8)));

static char spotifyTokenJson[4096];
static char spotifyAccessToken[1536];
static char spotifyRefreshToken[1536];
static char spotifyClientId[128];
static char spotifyScope[512];

static char spotifyHttpRequest[4096];
static char spotifyHttpResponse[SPOTIFY_HTTP_RESPONSE_SIZE];
static char spotifyRefreshBody[3072];
static char spotifyTokenOut[4096];
static char spotifyNewAccessToken[1536];
static char spotifyNewRefreshToken[1536];

static Spotify_Result spotifyLastResult;

static volatile bool spotifyHotkeyBusy = false;
static u32 spotifyLastHotkeyCombo = 0;

static const char *Spotify_ActionName(Spotify_Action action)
{
    switch(action)
    {
        case SPOTIFY_ACTION_NEXT: return "Next track";
        case SPOTIFY_ACTION_PREVIOUS: return "Previous track";
        case SPOTIFY_ACTION_PLAY: return "Play";
        case SPOTIFY_ACTION_PAUSE: return "Pause";
        case SPOTIFY_ACTION_PLAYPAUSE: return "Play/Pause";
        case SPOTIFY_ACTION_STATUS:
        default: return "Playback status";
    }
}

static void Spotify_ResetResult(Spotify_Result *r)
{
    memset(r, 0, sizeof(*r));

    r->tokenRet = -9999;
    r->sock = -1;
    r->connectRet = -1;
    r->configRet = -9999;
    r->setupRet = -9999;
    r->hostnameRet = -9999;
    r->handshakeRet = -9999;
    r->requestRet = -9999;
    r->writeRet = -9999;
    r->readRet = -9999;
    r->refreshRet = 0;
    r->didRefresh = false;
    r->statusLine[0] = '\0';
}

static int Spotify_AppendTo(char *dst, u32 dstSize, const char *src)
{
    u32 len = strlen(dst);
    u32 i = 0;

    while(src[i])
    {
        if(len + 1 >= dstSize)
            return -1;

        dst[len++] = src[i++];
        dst[len] = '\0';
    }

    return 0;
}

static bool Spotify_IsUnreservedUrlChar(char c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

static int Spotify_AppendUrlEncoded(char *dst, u32 dstSize, const char *src)
{
    static const char *hex = "0123456789ABCDEF";
    char tmp[4];

    tmp[0] = '%';
    tmp[3] = '\0';

    while(*src)
    {
        unsigned char c = (unsigned char)*src++;

        if(Spotify_IsUnreservedUrlChar((char)c))
        {
            char one[2];
            one[0] = (char)c;
            one[1] = '\0';

            if(Spotify_AppendTo(dst, dstSize, one) < 0)
                return -1;
        }
        else
        {
            tmp[1] = hex[(c >> 4) & 0xF];
            tmp[2] = hex[c & 0xF];

            if(Spotify_AppendTo(dst, dstSize, tmp) < 0)
                return -2;
        }
    }

    return 0;
}

static int Spotify_ParseJsonString(const char *json, const char *key, char *out, u32 outSize)
{
    const char *p = strstr(json, key);
    u32 len = 0;

    if(outSize == 0)
        return -10;

    out[0] = '\0';

    if(p == NULL)
        return -1;

    p = strchr(p, ':');
    if(p == NULL)
        return -2;

    p++;
    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;

    if(*p != '"')
        return -3;

    p++;

    while(*p && *p != '"')
    {
        if(*p == '\\')
        {
            p++;
            if(!*p)
                break;
        }

        if(len + 1 >= outSize)
            return -4;

        out[len++] = *p++;
        out[len] = '\0';
    }

    if(len == 0)
        return -5;

    return 0;
}

static int Spotify_LoadTokenFile(void)
{
    IFile file;
    Result res;
    u64 total = 0;
    int accessRet;
    int refreshRet;
    int clientRet;

    memset(spotifyTokenJson, 0, sizeof(spotifyTokenJson));
    memset(spotifyAccessToken, 0, sizeof(spotifyAccessToken));
    memset(spotifyRefreshToken, 0, sizeof(spotifyRefreshToken));
    memset(spotifyClientId, 0, sizeof(spotifyClientId));
    memset(spotifyScope, 0, sizeof(spotifyScope));

    res = IFile_Open(
        &file,
        ARCHIVE_SDMC,
        fsMakePath(PATH_EMPTY, ""),
        fsMakePath(PATH_ASCII, SPOTIFY_TOKEN_PATH),
        FS_OPEN_READ
    );

    if(R_FAILED(res))
        return -100;

    res = IFile_Read(
        &file,
        &total,
        spotifyTokenJson,
        sizeof(spotifyTokenJson) - 1
    );

    IFile_Close(&file);

    if(R_FAILED(res))
        return -101;

    if(total >= sizeof(spotifyTokenJson))
        total = sizeof(spotifyTokenJson) - 1;

    spotifyTokenJson[(u32)total] = '\0';

    accessRet = Spotify_ParseJsonString(
        spotifyTokenJson,
        "\"access_token\"",
        spotifyAccessToken,
        sizeof(spotifyAccessToken)
    );

    refreshRet = Spotify_ParseJsonString(
        spotifyTokenJson,
        "\"refresh_token\"",
        spotifyRefreshToken,
        sizeof(spotifyRefreshToken)
    );

    clientRet = Spotify_ParseJsonString(
        spotifyTokenJson,
        "\"client_id\"",
        spotifyClientId,
        sizeof(spotifyClientId)
    );

    Spotify_ParseJsonString(
        spotifyTokenJson,
        "\"scope\"",
        spotifyScope,
        sizeof(spotifyScope)
    );

    if(accessRet != 0)
        return -110;

    if(refreshRet != 0)
        return -111;

    if(clientRet != 0)
        return -112;

    return 0;
}

static void Spotify_ExtractHttpStatusLine(Spotify_Result *result)
{
    u32 i = 0;

    result->statusLine[0] = '\0';

    while(spotifyHttpResponse[i] &&
          spotifyHttpResponse[i] != '\r' &&
          spotifyHttpResponse[i] != '\n' &&
          i + 1 < sizeof(result->statusLine))
    {
        result->statusLine[i] = spotifyHttpResponse[i];
        i++;
    }

    result->statusLine[i] = '\0';
}

static void Spotify_EnsureConfigDirectory(void)
{
    FS_Archive archive;
    Result res = FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));

    if(R_SUCCEEDED(res))
    {
        FSUSER_CreateDirectory(archive, fsMakePath(PATH_ASCII, "/config"), 0);
        FSUSER_CreateDirectory(archive, fsMakePath(PATH_ASCII, SPOTIFY_CONFIG_DIR), 0);
        FSUSER_CloseArchive(archive);
    }
}

static int Spotify_WriteFile(const char *path, const char *text)
{
    IFile file;
    Result res;
    u64 total = 0;

    Spotify_EnsureConfigDirectory();

    res = IFile_Open(
        &file,
        ARCHIVE_SDMC,
        fsMakePath(PATH_EMPTY, ""),
        fsMakePath(PATH_ASCII, path),
        FS_OPEN_CREATE | FS_OPEN_WRITE
    );

    if(R_FAILED(res))
        return -1;

    IFile_SetSize(&file, 0);
    res = IFile_Write(&file, &total, text, strlen(text), 0);
    IFile_Close(&file);

    return R_SUCCEEDED(res) ? 0 : -2;
}

static bool Spotify_HttpStatusStartsWith(const Spotify_Result *result, const char *prefix)
{
    return strncmp(result->statusLine, prefix, strlen(prefix)) == 0;
}

static bool Spotify_HttpLooksOk(const Spotify_Result *result)
{
    return Spotify_HttpStatusStartsWith(result, "HTTP/1.1 200") ||
           Spotify_HttpStatusStartsWith(result, "HTTP/1.1 202") ||
           Spotify_HttpStatusStartsWith(result, "HTTP/1.1 204");
}

static bool Spotify_HttpIsUnauthorized(const Spotify_Result *result)
{
    return Spotify_HttpStatusStartsWith(result, "HTTP/1.1 401");
}

static const char *Spotify_ResultText(const Spotify_Result *result)
{
    if(Spotify_HttpLooksOk(result))
        return "OK";

    if(result->refreshRet != 0)
        return "ERROR REFRESH";

    if(Spotify_HttpIsUnauthorized(result))
        return "ERROR UNAUTHORIZED";

    if(result->tokenRet != 0)
        return "ERROR TOKEN";

    if(result->sock < 0 || result->connectRet != 0)
        return "ERROR TCP";

    if(result->handshakeLoops <= 0 || result->handshakeRet != 0)
        return "ERROR TLS";

    if(result->requestRet != 0)
        return "ERROR REQUEST";

    if(result->writeRet <= 0)
        return "ERROR SEND";

    if(result->readRet <= 0)
        return "ERROR RESPONSE";

    return "ERROR SPOTIFY";
}

static int Spotify_Rng(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;

    u64 tick = svcGetSystemTick();
    u32 tickLow = (u32)(tick ^ (tick >> 32));

    spotifyRngState ^= tickLow;

    for(size_t i = 0; i < len; i++)
    {
        spotifyRngState = spotifyRngState * 1664525u + 1013904223u;
        buf[i] = (unsigned char)(spotifyRngState >> 24);
    }

    return 0;
}

static int Spotify_MbedSend(void *ctx, const unsigned char *buf, size_t len)
{
    int sock = *(int *)ctx;
    int ret = socSend(sock, buf, len, 0);

    if(ret < 0)
        return MBEDTLS_ERR_NET_SEND_FAILED;

    return ret;
}

static int Spotify_MbedRecv(void *ctx, unsigned char *buf, size_t len)
{
    int sock = *(int *)ctx;
    int ret = socRecv(sock, buf, len, 0);

    if(ret < 0)
        return MBEDTLS_ERR_NET_RECV_FAILED;

    if(ret == 0)
        return MBEDTLS_ERR_NET_CONN_RESET;

    return ret;
}

static int Spotify_BuildSpotifyRequest(const char *method, const char *path)
{
    spotifyHttpRequest[0] = '\0';

    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), method) < 0)
        return -1;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), " ") < 0)
        return -2;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), path) < 0)
        return -3;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), " HTTP/1.1\r\n") < 0)
        return -4;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), "Host: " SPOTIFY_API_HOST "\r\n") < 0)
        return -5;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), "Authorization: Bearer ") < 0)
        return -6;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), spotifyAccessToken) < 0)
        return -7;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), "\r\n") < 0)
        return -8;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), "User-Agent: Spotify-Rosalina/0.1\r\n") < 0)
        return -9;

    if(strcmp(method, "GET") != 0)
    {
        if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), "Content-Length: 0\r\n") < 0)
            return -10;
    }

    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), "Connection: close\r\n\r\n") < 0)
        return -11;

    return 0;
}

static int Spotify_BuildRefreshRequest(void)
{
    char contentLength[64];

    spotifyRefreshBody[0] = '\0';
    spotifyHttpRequest[0] = '\0';

    if(Spotify_AppendTo(spotifyRefreshBody, sizeof(spotifyRefreshBody), "grant_type=refresh_token&refresh_token=") < 0)
        return -1;
    if(Spotify_AppendUrlEncoded(spotifyRefreshBody, sizeof(spotifyRefreshBody), spotifyRefreshToken) < 0)
        return -2;
    if(Spotify_AppendTo(spotifyRefreshBody, sizeof(spotifyRefreshBody), "&client_id=") < 0)
        return -3;
    if(Spotify_AppendUrlEncoded(spotifyRefreshBody, sizeof(spotifyRefreshBody), spotifyClientId) < 0)
        return -4;

    sprintf(contentLength, "Content-Length: %lu\r\n", (unsigned long)strlen(spotifyRefreshBody));

    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), "POST /api/token HTTP/1.1\r\n") < 0)
        return -5;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), "Host: " SPOTIFY_ACCOUNTS_HOST "\r\n") < 0)
        return -6;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), "User-Agent: Spotify-Rosalina/0.1\r\n") < 0)
        return -7;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), "Content-Type: application/x-www-form-urlencoded\r\n") < 0)
        return -8;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), contentLength) < 0)
        return -9;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), "Connection: close\r\n\r\n") < 0)
        return -10;
    if(Spotify_AppendTo(spotifyHttpRequest, sizeof(spotifyHttpRequest), spotifyRefreshBody) < 0)
        return -11;

    return 0;
}

static void Spotify_GetActionRequest(Spotify_Action action, const char **method, const char **path)
{
    switch(action)
    {
        case SPOTIFY_ACTION_NEXT:
            *method = "POST";
            *path = "/v1/me/player/next";
            break;
        case SPOTIFY_ACTION_PREVIOUS:
            *method = "POST";
            *path = "/v1/me/player/previous";
            break;
        case SPOTIFY_ACTION_PLAY:
            *method = "PUT";
            *path = "/v1/me/player/play";
            break;
        case SPOTIFY_ACTION_PAUSE:
            *method = "PUT";
            *path = "/v1/me/player/pause";
            break;
        case SPOTIFY_ACTION_STATUS:
        default:
            *method = "GET";
            *path = "/v1/me/player";
            break;
    }
}

static void Spotify_RunHttpsRequest(const char *host, u32 ip, Spotify_Result *result)
{
    Result miniSocRes = 0;
    int sock = -1;

    memset(spotifyHttpResponse, 0, sizeof(spotifyHttpResponse));
    result->statusLine[0] = '\0';

    memset(spotifyMbedHeap, 0, sizeof(spotifyMbedHeap));
    mbedtls_memory_buffer_alloc_init(spotifyMbedHeap, sizeof(spotifyMbedHeap));

    mbedtls_ssl_init(&spotifySsl);
    mbedtls_ssl_config_init(&spotifyConf);

    miniSocRes = miniSocInit();
    if(R_FAILED(miniSocRes))
    {
        result->connectRet = (int)miniSocRes;
        goto tls_free;
    }

    for(int socketTry = 0; socketTry < 15; socketTry++)
    {
        sock = socSocket(AF_INET, SOCK_STREAM, 0);
        if(sock >= 0)
            break;
        svcSleepThread(100 * 1000 * 1000LL);
    }

    result->sock = sock;

    if(sock < 0)
        goto soc_exit;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(443);
    addr.sin_addr.s_addr = htonl(ip);

    result->connectRet = socConnect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if(result->connectRet != 0)
        goto close_socket;

    result->configRet = mbedtls_ssl_config_defaults(
        &spotifyConf,
        MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT
    );
    if(result->configRet != 0)
        goto close_socket;

    mbedtls_ssl_conf_authmode(&spotifyConf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&spotifyConf, Spotify_Rng, NULL);

    result->setupRet = mbedtls_ssl_setup(&spotifySsl, &spotifyConf);
    if(result->setupRet != 0)
        goto close_socket;

    result->hostnameRet = mbedtls_ssl_set_hostname(&spotifySsl, host);
    if(result->hostnameRet != 0)
        goto close_socket;

    mbedtls_ssl_set_bio(&spotifySsl, &sock, Spotify_MbedSend, Spotify_MbedRecv, NULL);

    do
    {
        result->handshakeRet = mbedtls_ssl_handshake(&spotifySsl);
        result->handshakeLoops++;
        if(result->handshakeLoops > 200)
            break;
    }
    while(result->handshakeRet == MBEDTLS_ERR_SSL_WANT_READ ||
          result->handshakeRet == MBEDTLS_ERR_SSL_WANT_WRITE);

    if(result->handshakeRet != 0)
        goto close_socket;

    result->writeRet = mbedtls_ssl_write(
        &spotifySsl,
        (const unsigned char *)spotifyHttpRequest,
        strlen(spotifyHttpRequest)
    );

    if(result->writeRet <= 0)
        goto close_socket;

    {
        int totalRead = 0;
        int oneRead = 0;
        int wantRetries = 0;

        result->readRet = -9999;
        spotifyHttpResponse[0] = '\0';

        while(totalRead + 1 < (int)sizeof(spotifyHttpResponse))
        {
            oneRead = mbedtls_ssl_read(
                &spotifySsl,
                (unsigned char *)spotifyHttpResponse + totalRead,
                sizeof(spotifyHttpResponse) - 1 - totalRead
            );

            if(oneRead > 0)
            {
                totalRead += oneRead;
                result->readRet = totalRead;
                wantRetries = 0;
                continue;
            }

            if(oneRead == MBEDTLS_ERR_SSL_WANT_READ ||
               oneRead == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                wantRetries++;

                if(wantRetries > 50)
                    break;

                svcSleepThread(10 * 1000 * 1000LL);
                continue;
            }

            if(oneRead == 0 || oneRead == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
                break;

            if(totalRead == 0)
                result->readRet = oneRead;

            break;
        }

        if(totalRead > 0)
        {
            spotifyHttpResponse[totalRead] = '\0';
            result->readRet = totalRead;
            Spotify_ExtractHttpStatusLine(result);
        }
    }

    mbedtls_ssl_close_notify(&spotifySsl);

close_socket:
    if(sock >= 0)
        socClose(sock);
soc_exit:
    miniSocExit();
tls_free:
    mbedtls_ssl_free(&spotifySsl);
    mbedtls_ssl_config_free(&spotifyConf);
}

static int Spotify_WriteTokenFile(void)
{
    spotifyTokenOut[0] = '\0';

    if(Spotify_AppendTo(spotifyTokenOut, sizeof(spotifyTokenOut), "{\n") < 0)
        return -1;
    if(Spotify_AppendTo(spotifyTokenOut, sizeof(spotifyTokenOut), "  \"client_id\": \"") < 0)
        return -2;
    if(Spotify_AppendTo(spotifyTokenOut, sizeof(spotifyTokenOut), spotifyClientId) < 0)
        return -3;
    if(Spotify_AppendTo(spotifyTokenOut, sizeof(spotifyTokenOut), "\",\n  \"access_token\": \"") < 0)
        return -4;
    if(Spotify_AppendTo(spotifyTokenOut, sizeof(spotifyTokenOut), spotifyAccessToken) < 0)
        return -5;
    if(Spotify_AppendTo(spotifyTokenOut, sizeof(spotifyTokenOut), "\",\n  \"refresh_token\": \"") < 0)
        return -6;
    if(Spotify_AppendTo(spotifyTokenOut, sizeof(spotifyTokenOut), spotifyRefreshToken) < 0)
        return -7;
    if(Spotify_AppendTo(spotifyTokenOut, sizeof(spotifyTokenOut), "\",\n  \"expires_at\": 0,\n  \"scope\": \"") < 0)
        return -8;
    if(Spotify_AppendTo(spotifyTokenOut, sizeof(spotifyTokenOut), spotifyScope) < 0)
        return -9;
    if(Spotify_AppendTo(spotifyTokenOut, sizeof(spotifyTokenOut), "\"\n}\n") < 0)
        return -10;

    return Spotify_WriteFile(SPOTIFY_TOKEN_PATH, spotifyTokenOut);
}

static int Spotify_RefreshAccessToken(void)
{
    Spotify_Result refreshResult;
    int parseAccessRet;
    int parseRefreshRet;

    Spotify_ResetResult(&refreshResult);

    if(spotifyRefreshToken[0] == '\0' || spotifyClientId[0] == '\0')
        return -1;

    refreshResult.requestRet = Spotify_BuildRefreshRequest();
    if(refreshResult.requestRet != 0)
        return -2;

    Spotify_RunHttpsRequest(SPOTIFY_ACCOUNTS_HOST, SPOTIFY_ACCOUNTS_IP_U32, &refreshResult);

    if(!Spotify_HttpStatusStartsWith(&refreshResult, "HTTP/1.1 200"))
        return -3;

    spotifyNewAccessToken[0] = '\0';
    spotifyNewRefreshToken[0] = '\0';

    parseAccessRet = Spotify_ParseJsonString(
        spotifyHttpResponse,
        "\"access_token\"",
        spotifyNewAccessToken,
        sizeof(spotifyNewAccessToken)
    );

    if(parseAccessRet != 0)
        return -4;

    strcpy(spotifyAccessToken, spotifyNewAccessToken);

    parseRefreshRet = Spotify_ParseJsonString(
        spotifyHttpResponse,
        "\"refresh_token\"",
        spotifyNewRefreshToken,
        sizeof(spotifyNewRefreshToken)
    );

    if(parseRefreshRet == 0)
        strcpy(spotifyRefreshToken, spotifyNewRefreshToken);

    return Spotify_WriteTokenFile();
}

static void Spotify_RunSpotifyRequestOnce(const char *method, const char *path, Spotify_Result *result)
{
    Spotify_ResetResult(result);

    result->tokenRet = Spotify_LoadTokenFile();
    if(result->tokenRet != 0)
        return;

    result->requestRet = Spotify_BuildSpotifyRequest(method, path);
    if(result->requestRet != 0)
        return;

    Spotify_RunHttpsRequest(SPOTIFY_API_HOST, SPOTIFY_API_IP_U32, result);
}

static void Spotify_RunSpotifyRequestWithRefresh(const char *method, const char *path, Spotify_Result *result)
{
    Spotify_RunSpotifyRequestOnce(method, path, result);

    if(!Spotify_HttpIsUnauthorized(result))
        return;

    result->didRefresh = true;
    result->refreshRet = Spotify_RefreshAccessToken();

    if(result->refreshRet != 0)
        return;

    result->requestRet = Spotify_BuildSpotifyRequest(method, path);
    if(result->requestRet != 0)
        return;

    result->sock = -1;
    result->connectRet = -1;
    result->configRet = -9999;
    result->setupRet = -9999;
    result->hostnameRet = -9999;
    result->handshakeRet = -9999;
    result->handshakeLoops = 0;
    result->writeRet = -9999;
    result->readRet = -9999;
    result->statusLine[0] = '\0';

    Spotify_RunHttpsRequest(SPOTIFY_API_HOST, SPOTIFY_API_IP_U32, result);
}

static int Spotify_GetIsPlayingFromResponse(void)
{
    const char *p = strstr(spotifyHttpResponse, "\"is_playing\"");

    if(p == NULL)
        return -1;

    p = strchr(p, ':');

    if(p == NULL)
        return -1;

    p++;

    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;

    if(strncmp(p, "true", 4) == 0)
        return 1;

    if(strncmp(p, "false", 5) == 0)
        return 0;

    return -1;
}

static void Spotify_RunAction(Spotify_Action action, Spotify_Result *result)
{
    const char *method = "GET";
    const char *path = "/v1/me/player";

    if(action == SPOTIFY_ACTION_PLAYPAUSE)
    {
        int isPlaying = -1;

        /*
            Spotify Web API has no direct play/pause toggle endpoint.
            We implement toggle manually:
              GET /v1/me/player
              if is_playing true  -> PUT /v1/me/player/pause
              if is_playing false -> PUT /v1/me/player/play
              if 204 No Content   -> PUT /v1/me/player/play
        */
        Spotify_RunSpotifyRequestWithRefresh("GET", "/v1/me/player", result);

        if(Spotify_HttpStatusStartsWith(result, "HTTP/1.1 204"))
        {
            Spotify_RunSpotifyRequestWithRefresh("PUT", "/v1/me/player/play", result);
            return;
        }

        if(!Spotify_HttpLooksOk(result))
            return;

        isPlaying = Spotify_GetIsPlayingFromResponse();

        if(isPlaying == 1)
        {
            Spotify_RunSpotifyRequestWithRefresh("PUT", "/v1/me/player/pause", result);
        }
        else if(isPlaying == 0)
        {
            Spotify_RunSpotifyRequestWithRefresh("PUT", "/v1/me/player/play", result);
        }
        else
        {
            strcpy(result->statusLine, "HTTP/1.1 409 Unknown playback state");
        }

        return;
    }

    Spotify_GetActionRequest(action, &method, &path);
    Spotify_RunSpotifyRequestWithRefresh(method, path, result);
}

static void Spotify_DrawResult(Spotify_Action action, const Spotify_Result *result)
{
    do
    {
        Draw_Lock();
        Draw_ClearFramebuffer();

        Draw_DrawString(10, 10, COLOR_TITLE, "Spotify");

        Draw_DrawFormattedString(
            10,
            40,
            COLOR_WHITE,
            "Action: %s",
            Spotify_ActionName(action)
        );

        Draw_DrawFormattedString(
            10,
            70,
            COLOR_WHITE,
            "Response: %s",
            result->statusLine[0] ? result->statusLine : "No response"
        );

        Draw_DrawFormattedString(
            10,
            100,
            COLOR_WHITE,
            "Result: %s",
            Spotify_ResultText(result)
        );

        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}

static void Spotify_RunMenuAction(Spotify_Action action)
{
    Draw_Lock();
    Draw_ClearFramebuffer();

    Draw_DrawString(10, 10, COLOR_TITLE, "Spotify");

    Draw_DrawFormattedString(
        10,
        40,
        COLOR_WHITE,
        "Action: %s",
        Spotify_ActionName(action)
    );

    Draw_DrawString(
        10,
        70,
        COLOR_WHITE,
        "Response: ..."
    );

    Draw_DrawString(
        10,
        100,
        COLOR_WHITE,
        "Result: ..."
    );

    Draw_FlushFramebuffer();
    Draw_Unlock();

    Spotify_RunAction(action, &spotifyLastResult);
    Spotify_DrawResult(action, &spotifyLastResult);
}

void SpotifyMenu_Next(void)
{
    Spotify_RunMenuAction(SPOTIFY_ACTION_NEXT);
}

void SpotifyMenu_Previous(void)
{
    Spotify_RunMenuAction(SPOTIFY_ACTION_PREVIOUS);
}

void SpotifyMenu_Play(void)
{
    Spotify_RunMenuAction(SPOTIFY_ACTION_PLAY);
}

void SpotifyMenu_Pause(void)
{
    Spotify_RunMenuAction(SPOTIFY_ACTION_PAUSE);
}

void SpotifyMenu_PlayPause(void)
{
    Spotify_RunMenuAction(SPOTIFY_ACTION_PLAYPAUSE);
}

void SpotifyMenu_Status(void)
{
    Spotify_RunMenuAction(SPOTIFY_ACTION_STATUS);
}

void SpotifyMenu_Controls(void)
{
    do
    {
        Draw_Lock();
        Draw_ClearFramebuffer();

        Draw_DrawString(10, 10, COLOR_TITLE, "Spotify controls");

        Draw_DrawString(
            10,
            35,
            COLOR_WHITE,
            "Global hotkeys:\n\n"
            "Hold R + Select, then tap:\n\n"
            "D-Pad Right = Next track\n"
            "D-Pad Left  = Previous track\n"
            "D-Pad Up    = Play/Pause\n"
            "D-Pad Down  = Playback status"
        );

        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}

void Spotify_HandleHotkeys(u32 heldKeys, u32 downKeys)
{
    u32 combo = 0;
    Spotify_Action action = SPOTIFY_ACTION_STATUS;

    if((heldKeys & SPOTIFY_HOTKEY_MODIFIERS) != SPOTIFY_HOTKEY_MODIFIERS)
    {
        spotifyLastHotkeyCombo = 0;
        return;
    }

    if(downKeys & KEY_RIGHT)
    {
        combo = SPOTIFY_HOTKEY_NEXT;
        action = SPOTIFY_ACTION_NEXT;
    }
    else if(downKeys & KEY_LEFT)
    {
        combo = SPOTIFY_HOTKEY_PREVIOUS;
        action = SPOTIFY_ACTION_PREVIOUS;
    }
    else if(downKeys & KEY_UP)
    {
        combo = SPOTIFY_HOTKEY_PLAYPAUSE;
        action = SPOTIFY_ACTION_PLAYPAUSE;
    }
    else if(downKeys & KEY_DOWN)
    {
        combo = SPOTIFY_HOTKEY_STATUS;
        action = SPOTIFY_ACTION_STATUS;
    }
    else
    {
        spotifyLastHotkeyCombo = 0;
        return;
    }

    if(combo == spotifyLastHotkeyCombo)
        return;

    spotifyLastHotkeyCombo = combo;

    if(spotifyHotkeyBusy)
    {
        return;
    }

    spotifyHotkeyBusy = true;

    Spotify_RunAction(action, &spotifyLastResult);

    spotifyHotkeyBusy = false;
}

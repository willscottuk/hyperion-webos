#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <signal.h>

#include <pthread.h>

#include <vt/vt_openapi.h>
#include <gm.h>

#include "renderer.h"
#include "debug.h"
#include "hyperion_client.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

static struct option long_options[] = {
    {"width", optional_argument, 0, 'x'},
    {"height", optional_argument, 0, 'y'},
    {"address", required_argument, 0, 'a'},
    {"port", optional_argument, 0, 'p'},
    {"fps", optional_argument, 0, 'f'},
    {0, 0, 0, 0},
};


pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;

VT_RESOURCE_ID resource_id;
VT_CONTEXT_ID context_id;
uint32_t vt_texture_id = 0;

bool app_quit = false;
bool capture_initialized = false;
bool vt_available = false;

GM_SURFACE gm_surface;
VT_RESOLUTION_T resolution = {192, 108};
static const char *_address = NULL;
static int _port = 19400, _fps = 15, _framedelay_us = 0;

int capture_initialize();
void capture_terminate();
void capture_onevent(VT_EVENT_TYPE_T type, void *data, void *user_data);
void read_picture();
void send_picture();

static void handle_signal(int signal)
{
    switch (signal)
    {
    case SIGINT:
        app_quit = true;
        hyperion_destroy();
        break;
    default:
        break;
    }
}

static void print_usage()
{
    printf("Usage: hyperion-webos -a ADDRESS [OPTION]...\n");
    printf("\n");
    printf("Grab screen content continously and send to Hyperion via flatbuffers server.\n");
    printf("\n");
    printf("  -x, --width           Width of video frame (default 192)\n");
    printf("  -y, --height          Height of video frame (default 108)\n");
    printf("  -a, --address         IP address of Hyperion server\n");
    printf("  -p, --port            Port of Hyperion flatbuffers server (default 19400)\n");
    printf("  -f, --fps             Framerate for sending video frames (default 15)\n");
}

static int parse_options(int argc, char *argv[])
{
    int opt, longindex;
    while ((opt = getopt_long(argc, argv, "x:y:a:p:f:", long_options, &longindex)) != -1)
    {
        switch (opt)
        {
        case 'x':
            resolution.w = atoi(optarg);
            break;
        case 'y':
            resolution.h = atoi(optarg);
            break;
        case 'a':
            _address = strdup(optarg);
            break;
        case 'p':
            _port = atol(optarg);
            break;
        case 'f':
            _fps = atoi(optarg);
            break;
        }
    }
    if (!_address)
    {
        fprintf(stderr, "Error! Address not specified.\n");
        print_usage();
        return 1;
    }
    if (_fps < 0 || _fps > 60)
    {
        fprintf(stderr, "Error! FPS should between 0 (unlimited) and 60.\n");
        print_usage();
        return 1;
    }
    if (_fps == 0)
        _framedelay_us = 0;
    else
        _framedelay_us = 1000000 / _fps;
    return 0;
}

int main(int argc, char *argv[])
{
    int ret;
    if ((ret = parse_options(argc, argv)) != 0)
    {
        return ret;
    }
    if (getenv("XDG_RUNTIME_DIR") == NULL)
    {
        setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 1);
    }

    renderer_init(resolution.w, resolution.h);

    if ((ret = capture_initialize()) != 0)
    {
        goto cleanup;
    }
    renderer_set_gm_framebuffer(gm_surface.framebuffer);
    hyperion_client("webos", _address, _port, 150);
    signal(SIGINT, handle_signal);
    printf("Start connection loop\n");
    while (!app_quit)
    {
        if (hyperion_read() < 0)
        {
            fprintf(stderr, "Connection terminated.\n");
            app_quit = true;
        }
    }
    ret = 0;
cleanup:
    hyperion_destroy();
    capture_terminate();
    renderer_destroy();
    return ret;
}

int capture_initialize()
{
    int32_t supported = 0;
    if (VT_IsSystemSupported(&supported) != VT_OK || !supported)
    {
        fprintf(stderr, "[VT] VT_IsSystemSupported Failed. This TV doesn't support VT.\n");
        return -1;
    }

    fprintf(stdout, "[VT] VT_CreateVideoWindow\n");
    VT_VIDEO_WINDOW_ID window_id = VT_CreateVideoWindow(0);
    if (window_id == -1)
    {
        fprintf(stderr, "[VT] VT_CreateVideoWindow Failed\n");
        return -1;
    }
    fprintf(stdout, "[VT] window_id=%d\n", window_id);

    fprintf(stdout, "[VT] VT_AcquireVideoWindowResource\n");
    if (VT_AcquireVideoWindowResource(window_id, &resource_id) != VT_OK)
    {
        fprintf(stderr, "[VT] VT_AcquireVideoWindowResource Failed\n");
        return -1;
    }
    fprintf(stdout, "[VT] resource_id=%d\n", resource_id);

    fprintf(stdout, "[VT] VT_CreateContext\n");
    context_id = VT_CreateContext(resource_id, 2);
    if (!context_id || context_id == -1)
    {
        fprintf(stderr, "[VT] VT_CreateContext Failed\n");
        VT_ReleaseVideoWindowResource(resource_id);
        return -1;
    }
    fprintf(stdout, "[VT] context_id=%d\n", context_id);

    fprintf(stdout, "[VT] VT_SetTextureResolution\n");
    VT_SetTextureResolution(context_id, &resolution);
    // VT_GetTextureResolution(context_id, &resolution);

    fprintf(stdout, "[VT] VT_SetTextureSourceRegion\n");
    if (VT_SetTextureSourceRegion(context_id, VT_SOURCE_REGION_MAX) != VT_OK)
    {
        fprintf(stderr, "[VT] VT_SetTextureSourceRegion Failed\n");
        VT_DeleteContext(context_id);
        VT_ReleaseVideoWindowResource(resource_id);
        return -1;
    }

    fprintf(stdout, "[VT] VT_SetTextureSourceLocation\n");
    if (VT_SetTextureSourceLocation(context_id, VT_SOURCE_LOCATION_DISPLAY) != VT_OK)
    {
        fprintf(stderr, "[VT] VT_SetTextureSourceLocation Failed\n");
        VT_DeleteContext(context_id);
        VT_ReleaseVideoWindowResource(resource_id);
        return -1;
    }

    fprintf(stdout, "[VT] VT_RegisterEventHandler\n");
    if (VT_RegisterEventHandler(context_id, &capture_onevent, NULL) != VT_OK)
    {
        fprintf(stderr, "[VT] VT_RegisterEventHandler Failed\n");
        VT_DeleteContext(context_id);
        VT_ReleaseVideoWindowResource(resource_id);
        return -1;
    }

    GM_CreateSurface(resolution.w, resolution.h, 0, &gm_surface);
    capture_initialized = true;
    return 0;
}

void capture_terminate()
{
    if (!capture_initialized)
        return;
    capture_initialized = false;

    if (vt_texture_id != 0 && glIsTexture(vt_texture_id))
    {
        VT_DeleteTexture(context_id, vt_texture_id);
    }

    fprintf(stdout, "[VT] VT_UnRegisterEventHandler\n");
    if (VT_UnRegisterEventHandler(context_id) != VT_OK)
    {
        fprintf(stderr, "[VT] VT_UnRegisterEventHandler error!\n");
    }
    fprintf(stdout, "[VT] VT_DeleteContext\n");
    VT_DeleteContext(context_id);
    fprintf(stdout, "[VT] VT_ReleaseVideoWindowResource\n");
    VT_ReleaseVideoWindowResource(resource_id);

    GM_DestroySurface(gm_surface.surfaceID);
}

uint64_t getticks_us()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000 + tp.tv_nsec / 1000;
}

void capture_frame()
{
    if (!capture_initialized)
        return;
    pthread_mutex_lock(&frame_mutex);
    static uint32_t framecount = 0;
    static uint64_t last_ticks = 0, fps_ticks = 0, dur_gentexture = 0, dur_readframe = 0, dur_sendframe = 0;
    uint64_t ticks = getticks_us(), trace_start, trace_end;
    if (ticks - last_ticks < _framedelay_us)
    {
        pthread_mutex_unlock(&frame_mutex);
        return;
    }
    last_ticks = ticks;
    VT_OUTPUT_INFO_T output_info;
    if (vt_available)
    {
        if (vt_texture_id != 0 && glIsTexture(vt_texture_id))
        {
            VT_DeleteTexture(context_id, vt_texture_id);
        }

        trace_start = getticks_us();
        VT_STATUS_T vtStatus = VT_GenerateTexture(resource_id, context_id, &vt_texture_id, &output_info);
        uint32_t capture_w = resolution.w, capture_h = resolution.h;
        GM_CaptureGraphicScreen(gm_surface.surfaceID, &capture_w, &capture_h);
        trace_end = getticks_us();
        dur_gentexture += trace_end - trace_start;
        trace_start = trace_end;
        if (vtStatus == VT_OK)
        {
            renderer_set_vt_texture_id(vt_texture_id);
            renderer_generate();
            trace_end = getticks_us();
            dur_readframe += trace_end - trace_start;
            trace_start = trace_end;
            send_picture();
            trace_end = getticks_us();
            dur_sendframe += trace_end - trace_start;
            trace_start = trace_end;
            framecount++;
        }
        else
        {
            fprintf(stderr, "VT_GenerateTexture failed\n");
            vt_texture_id = 0;
        }
        vt_available = false;
    }
    if (fps_ticks == 0)
    {
        fps_ticks = ticks;
    }
    else if (ticks - fps_ticks >= 1000000)
    {
        printf("[Stat] Send framerate: %d FPS. gen %d us, read %d us, send %d us\n",
               framecount, dur_gentexture, dur_readframe, dur_sendframe);
        framecount = 0;
        dur_gentexture = 0;
        dur_readframe = 0;
        dur_sendframe = 0;
        fps_ticks = ticks;
    }
    pthread_mutex_unlock(&frame_mutex);
}

void capture_onevent(VT_EVENT_TYPE_T type, void *data, void *user_data)
{
    switch (type)
    {
    case VT_AVAILABLE:
        vt_available = true;
        capture_frame();
        break;
    case VT_UNAVAILABLE:
        fprintf(stderr, "VT_UNAVAILABLE received\n");
        break;
    case VT_RESOURCE_BUSY:
        fprintf(stderr, "VT_RESOURCE_BUSY received\n");
        break;
    default:
        fprintf(stderr, "UNKNOWN event received\n");
        break;
    }
}

void read_picture()
{
}

void send_picture()
{
    int width = resolution.w, height = resolution.h;

    if (hyperion_set_image(pixels_rgb, width, height) != 0)
    {
        fprintf(stderr, "Write timeout\n");
        hyperion_destroy();
        app_quit = true;
    }
}
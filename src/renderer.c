#include <assert.h>

#include <stdio.h>
#include <stdlib.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

#include "debug.h"

EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;

static int _width, _height;
static const uint8_t *_gm_framebuffer = NULL;
static uint32_t _vt_texture_id = 0;

GLubyte *pixels_rgba = NULL;
unsigned char *pixels_rgb = NULL;

GLuint gm_texture_id = 0;
GLuint offscreen_fb = 0;

static void _dump_pixels();

int renderer_init(int width, int height)
{
    _width = width;
    _height = height;
    // 1. Initialize egl
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(eglGetError() == EGL_SUCCESS);
    assert(egl_display);
    EGLint major, minor;

    eglInitialize(egl_display, &major, &minor);
    assert(eglGetError() == EGL_SUCCESS);
    printf("[EGL] Display, major = %d, minor = %d\n", major, minor);

    // 2. Select an appropriate configuration
    EGLint numConfigs;
    EGLConfig eglCfg;

    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE};

    eglChooseConfig(egl_display, configAttribs, &eglCfg, 1, &numConfigs);
    assert(eglGetError() == EGL_SUCCESS);

    // 3. Create a surface

    EGLint pbufferAttribs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
        EGL_TEXTURE_TARGET, EGL_TEXTURE_2D,
        EGL_LARGEST_PBUFFER, EGL_TRUE,
        EGL_NONE};
    egl_surface = eglCreatePbufferSurface(egl_display, eglCfg, pbufferAttribs);
    assert(eglGetError() == EGL_SUCCESS);
    assert(egl_surface);

    // 4. Bind the API
    eglBindAPI(EGL_OPENGL_ES_API);
    assert(eglGetError() == EGL_SUCCESS);

    // 5. Create a context and make it current

    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE};
    egl_context = eglCreateContext(egl_display, eglCfg, EGL_NO_CONTEXT, contextAttribs);
    assert(eglGetError() == EGL_SUCCESS);
    assert(egl_context);

    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
    assert(eglGetError() == EGL_SUCCESS);

    EGLint suf_width, suf_height;
    eglQuerySurface(egl_display, egl_surface, EGL_WIDTH, &suf_width);
    eglQuerySurface(egl_display, egl_surface, EGL_HEIGHT, &suf_height);
    assert(eglGetError() == EGL_SUCCESS);
    printf("[EGL] Surface size: %dx%d\n", suf_width, suf_height);

    // Create framebuffer for offscreen rendering
    GL_CHECK(glGenFramebuffers(1, &offscreen_fb));

    GL_CHECK(glGenTextures(1, &gm_texture_id));

    pixels_rgba = (GLubyte *)calloc(width * height, 4 * sizeof(GLubyte));
    pixels_rgb = (GLubyte *)calloc(width * height, 3 * sizeof(GLubyte));
    printf("[EGL] init complete\n");
}

int renderer_set_gm_framebuffer(const uint8_t *buffer)
{
    _gm_framebuffer = buffer;
}

int renderer_set_vt_texture_id(uint32_t texture_id)
{
    _vt_texture_id = texture_id;
}

int renderer_generate()
{
    glBindFramebuffer(GL_FRAMEBUFFER, offscreen_fb);

    glBindTexture(GL_TEXTURE_2D, _vt_texture_id);

    //Bind the texture to your FBO
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _vt_texture_id, 0);

    glBindTexture(GL_TEXTURE_2D, gm_texture_id);

    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _width, _height, 0, GL_RGBA, GL_UNSIGNED_BYTE, _gm_framebuffer));

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gm_texture_id, 0);

    //Test if everything failed
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        fprintf(stderr, "failed to make complete framebuffer object %x\n", status);
    }

    _dump_pixels();

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void _dump_pixels()
{
    glViewport(0, 0, _width, _height);

    glReadPixels(0, 0, _width, _height, GL_RGBA, GL_UNSIGNED_BYTE, pixels_rgba);
    for (int y = 0; y < _height; y++)
    {
        for (int x = 0; x < _width; x++)
        {
            int i = (y * _width) + x;
            pixels_rgb[i * 3 + 0] = pixels_rgba[i * 4 + 0];
            pixels_rgb[i * 3 + 1] = pixels_rgba[i * 4 + 1];
            pixels_rgb[i * 3 + 2] = pixels_rgba[i * 4 + 2];
        }
    }
}

int renderer_destroy()
{
    glDeleteTextures(1, &gm_texture_id);
    glDeleteFramebuffers(1, &offscreen_fb);
    eglDestroyContext(egl_display, egl_context);
    eglDestroySurface(egl_display, egl_surface);
    eglTerminate(egl_display);
    free(pixels_rgb);
    free(pixels_rgba);
}
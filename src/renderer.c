#include <assert.h>

#include <stdio.h>
#include <stdlib.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

#define INCBIN_PREFIX res_
#define INCBIN_STYLE INCBIN_STYLE_SNAKE

#include <incbin.h>

#include "debug.h"

EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;

static int _width, _height;
static const uint8_t *_gm_framebuffer = NULL;
static GLuint _vt_texture_id = 0, _gm_texture_id = 0, _fb_texture_id = 0;

GLubyte *pixels_rgba = NULL;
unsigned char *pixels_rgb = NULL;

GLuint offscreen_fb = 0;

GLuint vbo, ebo;

INCBIN(vertex_source, SOURCE_DIR "/shaders/vertex_source_gles2.glsl");
INCBIN(fragment_source, SOURCE_DIR "/shaders/fragment_source_gles2.glsl");
static const char *shader_sources[2] = {(const char *)res_vertex_source_data, (const char *)res_fragment_source_data};

static const float vertices[] = {
    -1.f, 1.f,
    -1.f, -1.f,
    1.f, -1.f,
    1.f, 1.f};

static const GLuint elements[] = {
    0, 1, 2,
    2, 3, 0};

static GLuint vertex_shader, fragment_shader, shader_program;
static GLint attrib_position, uniform_gm, uniform_vt;

static void _dump_pixels();

static void _egl_init();
static void _gl_setup();

int renderer_init(int width, int height)
{
    _width = width;
    _height = height;

    pixels_rgba = (GLubyte *)calloc(width * height, 4 * sizeof(GLubyte));
    pixels_rgb = (GLubyte *)calloc(width * height, 3 * sizeof(GLubyte));

    _egl_init();
    _gl_setup();
}

void _egl_init()
{
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
        EGL_WIDTH, _width,
        EGL_HEIGHT, _height,
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
    printf("[EGL] Init complete. Surface size: %dx%d\n", suf_width, suf_height);
}

void _gl_setup()
{
    // Create framebuffer for offscreen rendering
    GL_CHECK(glGenFramebuffers(1, &offscreen_fb));

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(elements), elements, GL_STATIC_DRAW);

    int status;
    char msg[512];
    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &shader_sources[0], (const int *)&res_vertex_source_size);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        char buf[1024];
        GLsizei len;
        glGetShaderInfoLog(vertex_shader, 1024, &len, buf);
        fprintf(stderr, "Vertex shader compile error:%.*s\n", len, buf);
    }
    assert(status == GL_TRUE);

    fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &shader_sources[1], (const int *)&res_fragment_source_size);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        char buf[1024];
        GLsizei len;
        glGetShaderInfoLog(fragment_shader, 1024, &len, buf);
        fprintf(stderr, "Fragment shader compile error:%.*s\n", len, buf);
    }
    assert(status == GL_TRUE);

    shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);

    glLinkProgram(shader_program);
    glGetProgramiv(shader_program, GL_LINK_STATUS, &status);
    assert(status == GL_TRUE);

    attrib_position = glGetAttribLocation(shader_program, "position");
    GL_CHECK(;);
    printf("attrib_position = %d\n", uniform_gm);

    uniform_gm = glGetUniformLocation(shader_program, "tex_gm");
    assert(glGetError() == GL_NO_ERROR);
    printf("uniform_gm = %d\n", uniform_gm);
    uniform_vt = glGetUniformLocation(shader_program, "tex_vt");
    assert(glGetError() == GL_NO_ERROR);
    printf("uniform_vt = %d\n", uniform_vt);

    // Create framebuffer for offscreen rendering
    GL_CHECK(glGenFramebuffers(1, &offscreen_fb));

    GL_CHECK(glGenTextures(1, &_fb_texture_id));
    glBindTexture(GL_TEXTURE_2D, _fb_texture_id);
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _width, _height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL));

    GL_CHECK(glGenTextures(1, &_gm_texture_id));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, _gm_texture_id));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _width, _height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
}

int renderer_set_gm_framebuffer(const uint8_t *buffer)
{
    _gm_framebuffer = buffer;
}

int renderer_set_vt_texture_id(uint32_t texture_id)
{
    _vt_texture_id = texture_id;
}

void print_bytes(const void *ptr, int size)
{
    const unsigned char *p = ptr;
    int i;
    for (i = 0; i < size; i++)
    {
        printf("%02hhX ", p[i]);
    }
    printf("\n");
}

const static GLfloat _position[2] = {0.5, 0.5};

int renderer_generate()
{
    glBindFramebuffer(GL_FRAMEBUFFER, offscreen_fb);

    glViewport(0, 0, _width, _height);

    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0, 0, 0, 1.0);

    GL_CHECK(glBindTexture(GL_TEXTURE_2D, _gm_texture_id));
    GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _width, _height, GL_RGBA, GL_UNSIGNED_BYTE, _gm_framebuffer));

    //Bind the texture to your FBO
    GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _fb_texture_id, 0));

    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, vbo));
    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo));
    GL_CHECK(glUseProgram(shader_program));

    GL_CHECK(glEnableVertexAttribArray(attrib_position));

    GL_CHECK(glVertexAttribPointer(attrib_position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), _position));

    GL_CHECK(glActiveTexture(GL_TEXTURE1));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, _gm_texture_id));
    GL_CHECK(glUniform1i(uniform_gm, 1));

    GL_CHECK(glActiveTexture(GL_TEXTURE2));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, _vt_texture_id));
    GL_CHECK(glUniform1i(uniform_gm, 2));

    GL_CHECK(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0));

    //Test if everything failed
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        fprintf(stderr, "failed to make complete framebuffer object 0x%x\n", status);
    }

    _dump_pixels();

    glDisableVertexAttribArray(attrib_position);
    glUseProgram(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void _dump_pixels()
{

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
    glDeleteProgram(shader_program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vbo);

    glDeleteTextures(1, &_gm_texture_id);
    glDeleteTextures(1, &_fb_texture_id);
    glDeleteFramebuffers(1, &offscreen_fb);
    eglDestroyContext(egl_display, egl_context);
    eglDestroySurface(egl_display, egl_surface);
    eglTerminate(egl_display);
    free(pixels_rgb);
    free(pixels_rgba);
}
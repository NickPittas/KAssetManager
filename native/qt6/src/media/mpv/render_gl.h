/* Official libmpv OpenGL render API header (trimmed local copy). */

#ifndef MPV_CLIENT_API_RENDER_GL_H_
#define MPV_CLIENT_API_RENDER_GL_H_

#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mpv_opengl_init_params {
    void *(*get_proc_address)(void *ctx, const char *name);
    void *get_proc_address_ctx;
} mpv_opengl_init_params;

typedef struct mpv_opengl_fbo {
    int fbo;
    int w, h;
    int internal_format;
} mpv_opengl_fbo;

#ifdef __cplusplus
}
#endif

#endif

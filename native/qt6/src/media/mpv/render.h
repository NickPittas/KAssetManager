/* Official libmpv render API header (trimmed local copy). */

#ifndef MPV_CLIENT_API_RENDER_H_
#define MPV_CLIENT_API_RENDER_H_

#include "client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mpv_render_context mpv_render_context;

typedef enum mpv_render_param_type {
    MPV_RENDER_PARAM_INVALID = 0,
    MPV_RENDER_PARAM_API_TYPE = 1,
    MPV_RENDER_PARAM_OPENGL_INIT_PARAMS = 2,
    MPV_RENDER_PARAM_OPENGL_FBO = 3,
    MPV_RENDER_PARAM_FLIP_Y = 4,
    MPV_RENDER_PARAM_DEPTH = 5,
    MPV_RENDER_PARAM_ICC_PROFILE = 6,
    MPV_RENDER_PARAM_AMBIENT_LIGHT = 7,
    MPV_RENDER_PARAM_X11_DISPLAY = 8,
    MPV_RENDER_PARAM_WL_DISPLAY = 9,
    MPV_RENDER_PARAM_ADVANCED_CONTROL = 10,
    MPV_RENDER_PARAM_NEXT_FRAME_INFO = 11,
    MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME = 12,
    MPV_RENDER_PARAM_SKIP_RENDERING = 13,
    MPV_RENDER_PARAM_DRM_DISPLAY = 14,
    MPV_RENDER_PARAM_DRM_DRAW_SURFACE_SIZE = 15,
    MPV_RENDER_PARAM_DRM_DISPLAY_V2 = 16,
    MPV_RENDER_PARAM_SW_SIZE = 17,
    MPV_RENDER_PARAM_SW_FORMAT = 18,
    MPV_RENDER_PARAM_SW_STRIDE = 19,
    MPV_RENDER_PARAM_SW_POINTER = 20,
} mpv_render_param_type;

typedef struct mpv_render_param {
    enum mpv_render_param_type type;
    void *data;
} mpv_render_param;

#define MPV_RENDER_API_TYPE_OPENGL "opengl"
#define MPV_RENDER_API_TYPE_SW "sw"

typedef enum mpv_render_frame_info_flag {
    MPV_RENDER_FRAME_INFO_PRESENT = 1 << 0,
    MPV_RENDER_FRAME_INFO_REDRAW = 1 << 1,
    MPV_RENDER_FRAME_INFO_REPEAT = 1 << 2,
    MPV_RENDER_FRAME_INFO_BLOCK_VSYNC = 1 << 3,
} mpv_render_frame_info_flag;

typedef struct mpv_render_frame_info {
    uint64_t flags;
    int64_t target_time;
} mpv_render_frame_info;

typedef void (*mpv_render_update_fn)(void *cb_ctx);

MPV_EXPORT int mpv_render_context_create(mpv_render_context **res, mpv_handle *mpv,
                                         mpv_render_param *params);
MPV_EXPORT int mpv_render_context_set_parameter(mpv_render_context *ctx,
                                                mpv_render_param param);
MPV_EXPORT int mpv_render_context_get_info(mpv_render_context *ctx,
                                           mpv_render_param param);
MPV_EXPORT void mpv_render_context_set_update_callback(mpv_render_context *ctx,
                                                       mpv_render_update_fn callback,
                                                       void *callback_ctx);
MPV_EXPORT uint64_t mpv_render_context_update(mpv_render_context *ctx);
MPV_EXPORT int mpv_render_context_render(mpv_render_context *ctx, mpv_render_param *params);
MPV_EXPORT void mpv_render_context_report_swap(mpv_render_context *ctx);
MPV_EXPORT void mpv_render_context_free(mpv_render_context *ctx);

#ifdef __cplusplus
}
#endif

#endif

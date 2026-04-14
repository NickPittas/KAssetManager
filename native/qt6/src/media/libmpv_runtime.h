#ifndef LIBMPV_RUNTIME_H
#define LIBMPV_RUNTIME_H

#include <QtCore/QLibrary>
#include <QtCore/QString>

#include "libmpv_headers.h"

class LibMpvRuntime
{
public:
    static LibMpvRuntime& instance();

    bool isAvailable() const { return m_loaded; }
    QString errorString() const { return m_error; }

    unsigned long (*client_api_version)();
    const char *(*error_string)(int error);
    void (*free_fn)(void *data);
    void (*free_node_contents)(mpv_node *node);
    mpv_handle *(*create)();
    int (*initialize)(mpv_handle *ctx);
    void (*destroy)(mpv_handle *ctx);
    void (*terminate_destroy)(mpv_handle *ctx);
    int (*set_option_string)(mpv_handle *ctx, const char *name, const char *data);
    int (*command)(mpv_handle *ctx, const char **args);
    int (*command_node)(mpv_handle *ctx, mpv_node *args, mpv_node *result);
    int (*set_property)(mpv_handle *ctx, const char *name, mpv_format format, void *data);
    int (*set_property_string)(mpv_handle *ctx, const char *name, const char *data);
    int (*get_property)(mpv_handle *ctx, const char *name, mpv_format format, void *data);
    char *(*get_property_string)(mpv_handle *ctx, const char *name);
    int (*observe_property)(mpv_handle *ctx, uint64_t reply_userdata, const char *name, mpv_format format);
    int (*unobserve_property)(mpv_handle *ctx, uint64_t registered_reply_userdata);
    int (*request_event)(mpv_handle *ctx, mpv_event_id event, int enable);
    int (*request_log_messages)(mpv_handle *ctx, const char *min_level);
    mpv_event *(*wait_event)(mpv_handle *ctx, double timeout);
    void (*set_wakeup_callback)(mpv_handle *ctx, void (*cb)(void *d), void *d);

    int (*render_context_create)(mpv_render_context **res, mpv_handle *mpv, mpv_render_param *params);
    int (*render_context_set_parameter)(mpv_render_context *ctx, mpv_render_param param);
    int (*render_context_get_info)(mpv_render_context *ctx, mpv_render_param param);
    void (*render_context_set_update_callback)(mpv_render_context *ctx, mpv_render_update_fn callback, void *callback_ctx);
    uint64_t (*render_context_update)(mpv_render_context *ctx);
    int (*render_context_render)(mpv_render_context *ctx, mpv_render_param *params);
    void (*render_context_report_swap)(mpv_render_context *ctx);
    void (*render_context_free)(mpv_render_context *ctx);

private:
    LibMpvRuntime();

    template<typename T>
    bool resolve(T& out, const char* symbol)
    {
        out = reinterpret_cast<T>(m_library.resolve(symbol));
        if (!out) {
            m_error = QStringLiteral("Failed to resolve libmpv symbol: %1").arg(QString::fromUtf8(symbol));
            return false;
        }
        return true;
    }

    bool m_loaded{false};
    QString m_error;
    QLibrary m_library;
};

#endif

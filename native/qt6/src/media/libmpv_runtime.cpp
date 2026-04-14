#include "libmpv_runtime.h"

LibMpvRuntime& LibMpvRuntime::instance()
{
    static LibMpvRuntime runtime;
    return runtime;
}

LibMpvRuntime::LibMpvRuntime()
{
#if defined(Q_OS_WIN)
    m_library.setFileName(QStringLiteral("mpv-2"));
#else
    const QStringList candidates = {
        QStringLiteral("libmpv.so.2"),
        QStringLiteral("mpv")
    };
    for (const QString& candidate : candidates) {
        m_library.setFileName(candidate);
        if (m_library.load()) {
            break;
        }
        m_error = m_library.errorString();
    }
    if (!m_library.isLoaded()) {
        return;
    }
#endif

#if defined(Q_OS_WIN)
    if (!m_library.load()) {
        m_error = m_library.errorString();
        return;
    }
#endif

    m_loaded =
        resolve(client_api_version, "mpv_client_api_version") &&
        resolve(error_string, "mpv_error_string") &&
        resolve(free_fn, "mpv_free") &&
        resolve(free_node_contents, "mpv_free_node_contents") &&
        resolve(create, "mpv_create") &&
        resolve(initialize, "mpv_initialize") &&
        resolve(destroy, "mpv_destroy") &&
        resolve(terminate_destroy, "mpv_terminate_destroy") &&
        resolve(set_option_string, "mpv_set_option_string") &&
        resolve(command, "mpv_command") &&
        resolve(command_node, "mpv_command_node") &&
        resolve(set_property, "mpv_set_property") &&
        resolve(set_property_string, "mpv_set_property_string") &&
        resolve(get_property, "mpv_get_property") &&
        resolve(get_property_string, "mpv_get_property_string") &&
        resolve(observe_property, "mpv_observe_property") &&
        resolve(unobserve_property, "mpv_unobserve_property") &&
        resolve(request_event, "mpv_request_event") &&
        resolve(request_log_messages, "mpv_request_log_messages") &&
        resolve(wait_event, "mpv_wait_event") &&
        resolve(set_wakeup_callback, "mpv_set_wakeup_callback") &&
        resolve(render_context_create, "mpv_render_context_create") &&
        resolve(render_context_set_parameter, "mpv_render_context_set_parameter") &&
        resolve(render_context_get_info, "mpv_render_context_get_info") &&
        resolve(render_context_set_update_callback, "mpv_render_context_set_update_callback") &&
        resolve(render_context_update, "mpv_render_context_update") &&
        resolve(render_context_render, "mpv_render_context_render") &&
        resolve(render_context_report_swap, "mpv_render_context_report_swap") &&
        resolve(render_context_free, "mpv_render_context_free");
}

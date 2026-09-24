#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPen>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest/QtTest>
#include <algorithm>
#include <clocale>
#include <cstdio>
#include <locale>
#include <vector>

extern "C" {
#include <mpv/client.h>
#include <mpv/render_gl.h>
}

namespace {
class LocaleGuard {
    std::string m_saved;
    bool m_active = true;
public:
    explicit LocaleGuard()
        : m_saved(setlocale(LC_NUMERIC, nullptr))
    {
        setlocale(LC_NUMERIC, "C");
    }
    ~LocaleGuard()
    {
        if (m_active) {
            setlocale(LC_NUMERIC, m_saved.c_str());
        }
    }
    LocaleGuard(const LocaleGuard&) = delete;
    LocaleGuard& operator=(const LocaleGuard&) = delete;
};

void* getProcAddress(void* ctx, const char* name)
{
    auto* context = static_cast<QOpenGLContext*>(ctx);
    if (!context || !name) {
        return nullptr;
    }
    return reinterpret_cast<void*>(context->getProcAddress(QByteArray(name)));
}

// EmbeddedMpvWidget renders libmpv video into a QOpenGLWidget framebuffer.
//
// With MPV_RENDER_PARAM_ADVANCED_CONTROL enabled, mpv_render_context_update()
// is a HARD requirement: it must be called from the render thread after each
// update callback, and it performs real work (e.g. allocating the video
// decoder's GL textures). Skipping it is exactly why an earlier version only
// ever showed the clear color: update() was never called, so the decoder had
// no textures and render() produced nothing.
class EmbeddedMpvWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit EmbeddedMpvWidget(QWidget* parent = nullptr)
        : QOpenGLWidget(parent)
    {
        setMinimumSize(320, 180);
        setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    }

    ~EmbeddedMpvWidget() override
    {
        makeCurrent();
        if (m_renderContext) {
            mpv_render_context_free(m_renderContext);
            m_renderContext = nullptr;
        }
        doneCurrent();

        if (m_mpv) {
            mpv_terminate_destroy(m_mpv);
            m_mpv = nullptr;
        }
    }

    QString lastError() const { return m_lastError; }

    // Total paints that performed a render() call (new frame or redraw).
    int renderedFrames() const { return m_renderedFrames.load(); }

    // Counts only paints where mpv_render_context_update() signalled a new
    // frame via MPV_RENDER_UPDATE_FRAME. This is the real "video delivered"
    // counter — distinct from renderedFrames(), which also counts redraws of
    // the previous frame.
    int videoFramesRendered() const { return m_videoFramesRendered.load(); }

    bool fileLoaded() const { return m_fileLoaded.load(); }
    bool hasNonBackgroundVideoPixels() const { return m_sawNonBackgroundPixels.load(); }
    int maxNonBackgroundSamples() const { return m_maxNonBackgroundSamples.load(); }
    int maxDistinctColorBuckets() const { return m_maxDistinctColorBuckets.load(); }

    bool initializePlayer()
    {
        if (m_mpv) {
            return true;
        }

        // libmpv requires LC_NUMERIC=C; Qt may have changed it.
        // RAII guard ensures restore on every exit path.
        LocaleGuard localeGuard;

        m_mpv = mpv_create();
        if (!m_mpv) {
            m_lastError = QStringLiteral("mpv_create failed");
            return false;
        }

        auto cleanupMpv = [this]() {
            mpv_terminate_destroy(m_mpv);
            m_mpv = nullptr;
        };

        struct Option {
            const char* name;
            const char* value;
        };
        constexpr Option options[] = {
            {"vo", "libmpv"},
            {"config", "no"},
            {"terminal", "no"},
            {"msg-level", "all=warn"},
            {"input-default-bindings", "no"},
            {"input-vo-keyboard", "no"},
            {"osc", "no"},
            {"osd-level", "0"},
            // Software decode keeps this proof about the update()/render
            // path rather than the GPU hwdec interop, which is a separate
            // concern.
            {"hwdec", "no"},
        };

        for (const auto& option : options) {
            const int rc = mpv_set_property_string(m_mpv, option.name, option.value);
            if (rc < 0) {
                m_lastError = QStringLiteral("mpv_set_property_string(%1=%2) failed: %3")
                                  .arg(QString::fromLatin1(option.name),
                                       QString::fromLatin1(option.value),
                                       QString::fromLatin1(mpv_error_string(rc)));
                cleanupMpv();
                return false;
            }
        }

        const int initRc = mpv_initialize(m_mpv);
        if (initRc < 0) {
            m_lastError = QStringLiteral("mpv_initialize failed: %1")
                              .arg(QString::fromLatin1(mpv_error_string(initRc)));
            cleanupMpv();
            return false;
        }

        // Drain mpv events on the GUI thread so we can observe FILE_LOADED.
        mpv_set_wakeup_callback(
            m_mpv,
            [](void* ctx) {
                auto* widget = static_cast<EmbeddedMpvWidget*>(ctx);
                QMetaObject::invokeMethod(widget, [widget]() {
                    widget->processMpvEvents();
                }, Qt::QueuedConnection);
            },
            this);

        mpv_opengl_init_params glParams = {};
        glParams.get_proc_address = getProcAddress;
        glParams.get_proc_address_ctx = QOpenGLContext::currentContext();
        int advancedControl = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advancedControl},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };

        const int renderRc = mpv_render_context_create(&m_renderContext, m_mpv, params);
        if (renderRc < 0) {
            m_lastError = QStringLiteral("mpv_render_context_create failed: %1")
                              .arg(QString::fromLatin1(mpv_error_string(renderRc)));
            cleanupMpv();
            return false;
        }

        mpv_render_context_set_update_callback(
            m_renderContext,
            [](void* ctx) {
                // Runs on the mpv core thread and must NOT call any
                // mpv_render_* function. It only schedules a repaint; paintGL
                // (render thread) then calls mpv_render_context_update() and
                // renders — exactly what ADVANCED_CONTROL requires.
                auto* widget = static_cast<EmbeddedMpvWidget*>(ctx);
                QMetaObject::invokeMethod(widget, "update", Qt::QueuedConnection);
            },
            this);

        return true;
    }

    bool loadFile(const QString& path)
    {
        if (!m_mpv && !initializePlayer()) {
            return false;
        }
        const QByteArray localPath = QFileInfo(path).absoluteFilePath().toUtf8();
        const char* cmd[] = {"loadfile", localPath.constData(), nullptr};
        const int rc = mpv_command(m_mpv, cmd);
        if (rc < 0) {
            m_lastError = QStringLiteral("mpv loadfile failed: %1")
                              .arg(QString::fromLatin1(mpv_error_string(rc)));
            return false;
        }
        processMpvEvents();
        return true;
    }

protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        initializePlayer();
    }

    void paintGL() override
    {
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        if (!m_renderContext) {
            return;
        }

        // ADVANCED_CONTROL requires update() on this (render) thread after
        // every update callback. It allocates decoder GL textures and returns
        // a bitset describing what to do next. Calling it on every paint is a
        // cheap no-op when nothing is pending and removes any race with the
        // update callback's repaint scheduling. MPV_RENDER_UPDATE_FRAME means
        // a new frame is ready to draw.
        const uint64_t updateFlags = mpv_render_context_update(m_renderContext);
        const bool framePending = (updateFlags & MPV_RENDER_UPDATE_FRAME) != 0;

        // Render only when a new frame is pending; once video has been drawn
        // once, also redraw the previous frame so the widget does not flash
        // back to the clear color between frames.
        if (!framePending && !m_seenVideoFrame) {
            return;
        }

        const auto ratio = devicePixelRatioF();
        mpv_opengl_fbo fbo = {};
        fbo.fbo = static_cast<int>(defaultFramebufferObject());
        fbo.w = qMax(1, static_cast<int>(width() * ratio));
        fbo.h = qMax(1, static_cast<int>(height() * ratio));
        int flipY = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };

        const int rc = mpv_render_context_render(m_renderContext, params);
        if (rc < 0) {
            m_lastError = QStringLiteral("mpv_render_context_render failed: %1")
                              .arg(QString::fromLatin1(mpv_error_string(rc)));
            return;
        }

        if (framePending) {
            m_videoFramesRendered.fetch_add(1);
            m_seenVideoFrame = true;
        }
        m_renderedFrames.fetch_add(1);
        mpv_render_context_report_swap(m_renderContext);

        // Objective pixel proof: read back what mpv just rendered straight
        // off this widget's framebuffer, on the render thread, before the
        // sibling annotation overlay is composited. This is the mpv output
        // alone and is what proves real video was produced.
        updatePixelProof(fbo.w, fbo.h);
    }

private:
    // Reads the freshly rendered mpv pixels off the widget FBO and records
    // whether they contain real, varied video rather than a uniform
    // background. Early-outs once video has been confirmed.
    void updatePixelProof(int width, int height)
    {
        if (m_sawNonBackgroundPixels.load()) {
            return;
        }
        if (width <= 0 || height <= 0) {
            return;
        }

        std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        const int stepX = qMax(1, width / 32);
        const int stepY = qMax(1, height / 18);
        int nonBlackPixels = 0;
        int distinctBuckets = 0;
        bool buckets[16] = {};
        for (int y = stepY / 2; y < height; y += stepY) {
            for (int x = stepX / 2; x < width; x += stepX) {
                const size_t i = (static_cast<size_t>(y) * width + x) * 4;
                const int r = pixels[i + 0];
                const int g = pixels[i + 1];
                const int b = pixels[i + 2];
                if (r > 12 || g > 12 || b > 12) {
                    ++nonBlackPixels;
                    const int bucket = ((r >> 6) << 2) ^ ((g >> 6) << 1) ^ (b >> 6);
                    if (!buckets[bucket & 0x0F]) {
                        buckets[bucket & 0x0F] = true;
                        ++distinctBuckets;
                    }
                }
            }
        }

        m_maxNonBackgroundSamples.store(qMax(m_maxNonBackgroundSamples.load(), nonBlackPixels));
        m_maxDistinctColorBuckets.store(qMax(m_maxDistinctColorBuckets.load(), distinctBuckets));
        if (nonBlackPixels > 20 && distinctBuckets > 2) {
            m_sawNonBackgroundPixels.store(true);
        }
    }

    // Drains the mpv event queue on the GUI thread (single-threaded access to
    // mpv_wait_event). Records FILE_LOADED so the test can distinguish "file
    // did not load" from "no frames decoded".
    void processMpvEvents()
    {
        if (!m_mpv) {
            return;
        }
        while (true) {
            mpv_event* event = mpv_wait_event(m_mpv, 0.0);
            if (!event || event->event_id == MPV_EVENT_NONE || event->event_id == MPV_EVENT_SHUTDOWN) {
                break;
            }
            if (event->event_id == MPV_EVENT_FILE_LOADED) {
                m_fileLoaded.store(true);
            }
        }
    }

    mpv_handle* m_mpv = nullptr;
    mpv_render_context* m_renderContext = nullptr;
    QString m_lastError;
    std::atomic<int> m_renderedFrames {0};
    std::atomic<int> m_videoFramesRendered {0};
    std::atomic<bool> m_fileLoaded {false};
    std::atomic<int> m_maxNonBackgroundSamples {0};
    std::atomic<int> m_maxDistinctColorBuckets {0};
    std::atomic<bool> m_sawNonBackgroundPixels {false};
    bool m_seenVideoFrame = false; // render thread only
};

class AnnotationProbe final : public QWidget {
    Q_OBJECT

public:
    explicit AnnotationProbe(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    int paintCount() const { return m_paintCount; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        ++m_paintCount;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::red, 5));
        painter.drawLine(20, 20, width() - 20, height() - 20);
        painter.drawEllipse(rect().center(), 30, 30);
    }

private:
    int m_paintCount = 0;
};

// Event filter for visual inspection mode: closes the window on Escape
// and records manual dismissal (Escape key or window close button).
class VisualEventFilter final : public QObject {
    Q_OBJECT

public:
    explicit VisualEventFilter(QWidget* target, QObject* parent = nullptr)
        : QObject(parent), m_target(target) {}

    bool closedByUser() const { return m_closedByUser; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                m_closedByUser = true;
                if (m_target) {
                    m_target->close();
                }
                return true;
            }
        } else if (event->type() == QEvent::Close) {
            m_closedByUser = true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* m_target = nullptr;
    bool m_closedByUser = false;
};

// Visual inspection is enabled by KASSETMANAGER_LIBMPV_POC_VISUAL=1 (or the
// short form VISUAL=1). Duration defaults to 30 s and is shortened via
// KASSETMANAGER_LIBMPV_POC_VISUAL_SECS (or VISUAL_SECS).
bool visualInspectionEnabled()
{
    return qgetenv("KASSETMANAGER_LIBMPV_POC_VISUAL") == "1" || qgetenv("VISUAL") == "1";
}

qint64 visualInspectionDurationMs()
{
    qint64 durationMs = 30000;
    const QByteArray secsEnv = qgetenv("VISUAL_SECS");
    int secs = -1;
    if (!secsEnv.isEmpty()) {
        secs = secsEnv.toInt();
    } else {
        secs = QString::fromLocal8Bit(qgetenv("KASSETMANAGER_LIBMPV_POC_VISUAL_SECS")).toInt();
    }
    if (secs > 0) {
        durationMs = static_cast<qint64>(secs) * 1000;
    }
    return durationMs;
}

class TestLibMpvEmbeddedPoc final : public QObject {
    Q_OBJECT

private slots:
    void rendersInsideQtWidgetWithoutQtTopLevelGrowth()
    {
        const QString mediaPath = QString::fromLocal8Bit(qgetenv("KASSETMANAGER_LIBMPV_POC_FILE"));
        if (mediaPath.isEmpty()) {
            QSKIP("Set KASSETMANAGER_LIBMPV_POC_FILE to an MP4/MKV file for the libmpv embedded proof.");
        }
        QVERIFY2(QFileInfo::exists(mediaPath), qPrintable(QStringLiteral("Media file does not exist: %1").arg(mediaPath)));

        QWidget window;
        window.setWindowTitle(QStringLiteral("KAssetManager libmpv embedded proof"));
        auto* layout = new QVBoxLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* stack = new QWidget(&window);
        auto* mpvWidget = new EmbeddedMpvWidget(stack);
        auto* annotation = new AnnotationProbe(stack);
        auto* stackLayout = new QStackedLayout(stack);
        stackLayout->setContentsMargins(0, 0, 0, 0);
        stackLayout->setStackingMode(QStackedLayout::StackAll);
        stackLayout->addWidget(mpvWidget);
        stackLayout->addWidget(annotation);
        layout->addWidget(stack);
        // Capture visible top-level widgets before showing our proof window.
        // We use object identity so that hidden internals or transient windows
        // don't cause spurious failures — only visible widgets matter.
        const auto isVisibleTopLevel = [](const QWidget* w) {
            return w->isWindow() && w->isVisible();
        };

        auto topLevelSnapshot = QApplication::topLevelWidgets();
        const int visibleBefore = std::count_if(topLevelSnapshot.cbegin(),
                                                 topLevelSnapshot.cend(),
                                                 isVisibleTopLevel);

        window.resize(640, 360);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        annotation->setGeometry(mpvWidget->geometry());
        annotation->raise();
        annotation->show();

        QVERIFY2(mpvWidget->initializePlayer(), qPrintable(mpvWidget->lastError()));
        QVERIFY2(mpvWidget->loadFile(mediaPath), qPrintable(mpvWidget->lastError()));

        // Wait for mpv to actually deliver decoded frames (not just clears)
        // AND for the readback proof to see real video pixels.
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            QTest::qWait(25);
            if (mpvWidget->videoFramesRendered() >= 5 && mpvWidget->hasNonBackgroundVideoPixels()) {
                break;
            }
        }

        QVERIFY2(mpvWidget->fileLoaded(),
                 qPrintable(QStringLiteral("libmpv did not report FILE_LOADED. Last error: %1").arg(mpvWidget->lastError())));
        QVERIFY2(mpvWidget->videoFramesRendered() >= 5,
                 qPrintable(QStringLiteral("No embedded video frames rendered (update()/render never produced a frame). Last error: %1")
                                .arg(mpvWidget->lastError())));
        QVERIFY2(mpvWidget->hasNonBackgroundVideoPixels(),
                 qPrintable(QStringLiteral("Embedded mpv widget still contains only the clear/background color after waiting; decoded video is not visible. maxNonBackgroundSamples=%1 maxDistinctColorBuckets=%2")
                                .arg(mpvWidget->maxNonBackgroundSamples())
                                .arg(mpvWidget->maxDistinctColorBuckets())));
        QVERIFY2(annotation->paintCount() > 0, "Annotation overlay did not paint above the mpv widget.");

        // After rendering, verify no extra visible top-level window appeared
        // beyond the proof window and any that were already visible before.
        auto topLevelAfter = QApplication::topLevelWidgets();
        const int visibleAfter = std::count_if(topLevelAfter.cbegin(),
                                                topLevelAfter.cend(),
                                                isVisibleTopLevel);
        QVERIFY2(visibleAfter <= visibleBefore + 1,
                 qPrintable(QStringLiteral("Extra visible top-level window detected. visibleBefore=%1 visibleAfter=%2")
                                .arg(visibleBefore).arg(visibleAfter)));
    }

    // Visual inspection mode: shows a real, human-visible window that plays
    // the selected media through the embedded libmpv widget with the red
    // annotation overlay on top. Stays open ~30 s (or until Escape / window
    // close) so a human can confirm rendering. Gated by environment variable
    // so the normal automated run stays fast.
    void visualInspection()
    {
        if (!visualInspectionEnabled()) {
            QSKIP("Visual inspection disabled. Set KASSETMANAGER_LIBMPV_POC_VISUAL=1 (or VISUAL=1) to enable.");
        }

        const QString mediaPath = QString::fromLocal8Bit(qgetenv("KASSETMANAGER_LIBMPV_POC_FILE"));
        QVERIFY2(!mediaPath.isEmpty(),
                 "Set KASSETMANAGER_LIBMPV_POC_FILE to an MP4/MKV file for visual inspection.");
        QVERIFY2(QFileInfo::exists(mediaPath),
                 qPrintable(QStringLiteral("Media file does not exist: %1").arg(mediaPath)));

        const qint64 durationMs = visualInspectionDurationMs();

        QWidget window;
        window.setWindowTitle(QStringLiteral(
            "KAssetManager libmpv embedded PoC - visual (Esc or close to exit)"));
        auto* layout = new QVBoxLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* stack = new QWidget(&window);
        auto* mpvWidget = new EmbeddedMpvWidget(stack);
        auto* annotation = new AnnotationProbe(stack);
        auto* stackLayout = new QStackedLayout(stack);
        stackLayout->setContentsMargins(0, 0, 0, 0);
        stackLayout->setStackingMode(QStackedLayout::StackAll);
        stackLayout->addWidget(mpvWidget);
        stackLayout->addWidget(annotation);
        layout->addWidget(stack);

        VisualEventFilter filter(&window);
        window.installEventFilter(&filter);

        window.resize(960, 540);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        annotation->setGeometry(mpvWidget->geometry());
        annotation->raise();
        annotation->show();

        QVERIFY2(mpvWidget->initializePlayer(), qPrintable(mpvWidget->lastError()));
        QVERIFY2(mpvWidget->loadFile(mediaPath), qPrintable(mpvWidget->lastError()));

        fprintf(stderr, "[visual] media: %s | window: %dx%d | duration: %lld ms\n",
                mediaPath.toUtf8().constData(), window.width(), window.height(),
                static_cast<long long>(durationMs));
        fflush(stderr);

        QElapsedTimer timer;
        timer.start();
        qint64 lastReport = -2000;
        while (timer.elapsed() < durationMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            QTest::qWait(25);
            if (!window.isVisible() || filter.closedByUser()) {
                fprintf(stderr, "[visual] dismissed by user after %lld ms\n",
                        static_cast<long long>(timer.elapsed()));
                fflush(stderr);
                break;
            }
            if (timer.elapsed() - lastReport >= 1000) {
                lastReport = timer.elapsed();
                fprintf(stderr, "[visual] t=%lld ms | renderedFrames=%d | videoFrames=%d | annotationPaints=%d | videoPixels=%s | lastError=%s\n",
                        static_cast<long long>(timer.elapsed()), mpvWidget->renderedFrames(),
                        mpvWidget->videoFramesRendered(), annotation->paintCount(),
                        mpvWidget->hasNonBackgroundVideoPixels() ? "yes" : "no",
                        mpvWidget->lastError().toUtf8().constData());
                fflush(stderr);
            }
        }

        fprintf(stderr, "[visual] finished. renderedFrames=%d | videoFrames=%d | fileLoaded=%s | annotationPaints=%d | videoPixels=%s | maxNonBackgroundSamples=%d | maxDistinctColorBuckets=%d\n",
                mpvWidget->renderedFrames(), mpvWidget->videoFramesRendered(),
                mpvWidget->fileLoaded() ? "yes" : "no", annotation->paintCount(),
                mpvWidget->hasNonBackgroundVideoPixels() ? "yes" : "no",
                mpvWidget->maxNonBackgroundSamples(), mpvWidget->maxDistinctColorBuckets());
        fflush(stderr);

        QVERIFY2(mpvWidget->videoFramesRendered() > 0,
                 qPrintable(QStringLiteral("No embedded video frames rendered. Last error: %1").arg(mpvWidget->lastError())));
        QVERIFY2(mpvWidget->hasNonBackgroundVideoPixels(),
                 "Embedded mpv widget still contains only the clear/background color; decoded video is not visible.");
        QVERIFY2(annotation->paintCount() > 0,
                 "Annotation overlay did not paint above the mpv widget.");
    }
};

} // namespace

QTEST_MAIN(TestLibMpvEmbeddedPoc)
#include "test_libmpv_embedded_poc.moc"

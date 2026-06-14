#include <QtTest/QtTest>

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QImage>
#include <QStackedLayout>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <vector>

#include <mdk/Player.h>
#include <mdk/RenderAPI.h>
#include <mdk/global.h>

class MdkVideoWidget final : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit MdkVideoWidget(QWidget* parent = nullptr)
        : QOpenGLWidget(parent)
        , m_player(std::make_unique<mdk::Player>())
    {
        setMinimumSize(640, 360);
        setAutoFillBackground(false);
        m_player->setBackgroundColor(0.0F, 0.0F, 0.0F, 1.0F);
        m_player->setDecoders(mdk::MediaType::Video, {"CUDA", "VAAPI", "VDPAU", "FFmpeg", "dav1d"});
        m_player->setRenderCallback([this](void*) {
            QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
        });
    }

    ~MdkVideoWidget() override
    {
        if (m_player) {
            m_player->set(mdk::State::Stopped);
        }
        makeCurrent();
        if (m_player) {
            m_player->setVideoSurfaceSize(-1, -1);
        }
        doneCurrent();
    }

    void loadAndPlay(const QString& path)
    {
        // Single MDK-general decode path for every container. The constructor
        // already installed the HW-first decoder list (CUDA/VAAPI/VDPAU then
        // FFmpeg/dav1d software fallback); no per-format override is applied
        // so MOV/MP4/MKV all exercise the same MDK library path. The broken
        // vulkan decoder is intentionally absent. See Player.h setDecoders().
        (void)path;
        m_mediaMode = "mdk-default";
        m_player->setMedia(QFile::encodeName(path).constData());
        m_player->set(mdk::State::Playing);
    }

    int renderedFrames() const { return m_renderedFrames.load(); }
    int overlayPaints() const { return m_overlayPaints.load(); }
    bool sawVideoPixels() const { return m_nativeReadback.load() || m_grabbedReadback.load(); }
    bool nativeReadback() const { return m_nativeReadback.load(); }
    bool grabbedReadback() const { return m_grabbedReadback.load(); }
    double lastTimestamp() const { return m_lastTimestamp.load(); }
    const char* mediaMode() const { return m_mediaMode; }

    // Secondary acceptance proof: read the widget's composited framebuffer (what Qt
    // actually presents) after the event loop has flushed paint events.
    void proofViaFramebuffer()
    {
        if (sawVideoPixels()) {
            return;
        }
        if (imageHasVideo(grabFramebuffer())) {
            m_grabbedReadback.store(true);
        }
    }

protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        m_renderApi.opaque = context();
        m_renderApi.fbo = defaultFramebufferObject();
        updateSurfaceSize();
        m_player->setRenderAPI(&m_renderApi);
    }

    void resizeGL(int, int) override
    {
        updateSurfaceSize();
    }

    void paintGL() override
    {
        m_renderApi.opaque = context();
        m_renderApi.fbo = defaultFramebufferObject();
        m_player->setRenderAPI(&m_renderApi);
        const double timestamp = m_player->renderVideo();
        if (timestamp >= 0.0) {
            m_lastTimestamp.store(timestamp);
            m_renderedFrames.fetch_add(1);
            updatePixelProof();
        }
        glFlush();

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor(255, 0, 0), 5.0);
        painter.setPen(pen);
        painter.drawLine(QPointF(width() * 0.12, height() * 0.18), QPointF(width() * 0.88, height() * 0.82));
        painter.drawEllipse(QPointF(width() * 0.50, height() * 0.50), width() * 0.12, height() * 0.18);
        m_overlayPaints.fetch_add(1);
    }

private:
    void updateSurfaceSize()
    {
        if (!m_player) {
            return;
        }
        const qreal ratio = devicePixelRatioF();
        m_player->setVideoSurfaceSize(std::max(1, int(width() * ratio)), std::max(1, int(height() * ratio)));
    }

    static bool imageHasVideo(const QImage& img)
    {
        if (img.isNull()) {
            return false;
        }
        const QImage rgba = img.convertedTo(QImage::Format_RGBA8888);
        const int w = rgba.width();
        const int h = rgba.height();
        const int stepX = std::max(1, w / 32);
        const int stepY = std::max(1, h / 18);
        int nonBlack = 0;
        int distinctBuckets = 0;
        bool buckets[16] = {};
        for (int y = stepY / 2; y < h; y += stepY) {
            const auto* line = reinterpret_cast<const unsigned char*>(rgba.constScanLine(y));
            for (int x = stepX / 2; x < w; x += stepX) {
                const int i = x * 4;
                const int r = line[i + 0];
                const int g = line[i + 1];
                const int b = line[i + 2];
                if (r > 12 || g > 12 || b > 12) {
                    ++nonBlack;
                    const int bucket = ((r >> 6) << 2) ^ ((g >> 6) << 1) ^ (b >> 6);
                    if (!buckets[bucket & 0x0F]) {
                        buckets[bucket & 0x0F] = true;
                        ++distinctBuckets;
                    }
                }
            }
        }
        return nonBlack > 20 && distinctBuckets > 2;
    }

    void updatePixelProof()
    {
        if (sawVideoPixels()) {
            return;
        }

        const qreal ratio = devicePixelRatioF();
        const int w = std::max(1, int(width() * ratio));
        const int h = std::max(1, int(height() * ratio));
        std::vector<unsigned char> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4U);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        const int stepX = std::max(1, w / 32);
        const int stepY = std::max(1, h / 18);
        int nonBlack = 0;
        int distinctBuckets = 0;
        bool buckets[16] = {};
        for (int y = stepY / 2; y < h; y += stepY) {
            for (int x = stepX / 2; x < w; x += stepX) {
                const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4U;
                const int r = pixels[i + 0];
                const int g = pixels[i + 1];
                const int b = pixels[i + 2];
                if (r > 12 || g > 12 || b > 12) {
                    ++nonBlack;
                    const int bucket = ((r >> 6) << 2) ^ ((g >> 6) << 1) ^ (b >> 6);
                    if (!buckets[bucket & 0x0F]) {
                        buckets[bucket & 0x0F] = true;
                        ++distinctBuckets;
                    }
                }
            }
        }
        if (nonBlack > 20 && distinctBuckets > 2) {
            m_nativeReadback.store(true);
        }
    }

    std::unique_ptr<mdk::Player> m_player;
    mdk::GLRenderAPI m_renderApi;
    std::atomic<int> m_renderedFrames {0};
    std::atomic<int> m_overlayPaints {0};
    std::atomic<bool> m_nativeReadback {false};
    std::atomic<bool> m_grabbedReadback {false};
    std::atomic<double> m_lastTimestamp {-1.0};
    const char* m_mediaMode {"mdk-default"};
};

class TestMdkEmbeddedPoc final : public QObject
{
    Q_OBJECT

private slots:
    void embeddedPlayback()
    {
        const QString mediaPath = qEnvironmentVariable("KASSETMANAGER_MDK_POC_FILE", QStringLiteral("/home/npittas/Videos/INTERSPORT_v8_Mix1_v3.mp4"));
        QVERIFY2(QFileInfo::exists(mediaPath), qPrintable(QStringLiteral("Missing test media: %1").arg(mediaPath)));

        QWidget window;
        window.setWindowTitle(QStringLiteral("KAssetManager MDK embedded PoC"));
        auto* layout = new QStackedLayout(&window);
        layout->setStackingMode(QStackedLayout::StackAll);
        auto* video = new MdkVideoWidget(&window);
        layout->addWidget(video);
        window.resize(960, 540);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        video->loadAndPlay(mediaPath);

        const bool visual = qEnvironmentVariableIntValue("KASSETMANAGER_MDK_POC_VISUAL") != 0;
        const int visualMs = std::max(1, qEnvironmentVariableIntValue("KASSETMANAGER_MDK_POC_VISUAL_SECS")) * 1000;
        QElapsedTimer timer;
        timer.start();
        const int timeoutMs = visual ? visualMs : 8000;
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            QTest::qWait(25);
            video->proofViaFramebuffer();
            if (!visual && video->renderedFrames() >= 5 && video->sawVideoPixels() && video->overlayPaints() > 0) {
                break;
            }
        }

        fprintf(stderr,
                "[mdk-poc] renderedFrames=%d overlayPaints=%d nativeReadback=%s grabbedReadback=%s sawVideoPixels=%s lastTimestamp=%.3f mode=%s\n",
                video->renderedFrames(), video->overlayPaints(),
                video->nativeReadback() ? "yes" : "no", video->grabbedReadback() ? "yes" : "no",
                video->sawVideoPixels() ? "yes" : "no", video->lastTimestamp(), video->mediaMode());
        fflush(stderr);

        QVERIFY2(video->renderedFrames() >= 5, "MDK did not render enough frames into the Qt widget.");
        QVERIFY2(video->sawVideoPixels(), "MDK rendered only background/black pixels; decoded video is not visible in the widget.");
        QVERIFY2(video->overlayPaints() > 0, "Overlay paint did not run above the video widget.");
    }
};

QTEST_MAIN(TestMdkEmbeddedPoc)
#include "test_mdk_embedded_poc.moc"

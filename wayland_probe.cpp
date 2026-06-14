#include <QApplication>
#include <QDockWidget>
#include <QGuiApplication>
#include <QMainWindow>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QStringList>
#include <QTimer>

int main(int argc, char** argv)
{
    const QStringList args(argv, argv + argc);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    if (args.contains(QStringLiteral("--share-gl"))) {
        QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    }
    if (args.contains(QStringLiteral("--desktop-gl"))) {
        QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    }
    if (args.contains(QStringLiteral("--raster"))) {
        QApplication::setAttribute(Qt::AA_ForceRasterWidgets);
    }

    QApplication app(argc, argv);
    QMainWindow window;
    const bool useGlWidget = args.contains(QStringLiteral("--gl-widget"));
    const bool lateGlWidget = args.contains(QStringLiteral("--late-gl-widget"));
    const bool twoGlWidgets = args.contains(QStringLiteral("--two-gl-widgets"));
    const bool core41 = args.contains(QStringLiteral("--core41"));
    if (core41) {
        QSurfaceFormat format;
        format.setMajorVersion(4);
        format.setMinorVersion(1);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setRenderableType(QSurfaceFormat::OpenGL);
        QSurfaceFormat::setDefaultFormat(format);
    }
    if (useGlWidget && !lateGlWidget) {
        window.setCentralWidget(new QOpenGLWidget(&window));
    }
    if (twoGlWidgets) {
        window.setCentralWidget(new QOpenGLWidget(&window));
        auto dock = new QDockWidget(&window);
        dock->setWidget(new QOpenGLWidget(dock));
        window.addDockWidget(Qt::BottomDockWidgetArea, dock);
    }
    window.resize(320, 200);
    window.show();
    if (useGlWidget && lateGlWidget) {
        QTimer::singleShot(1000, &window, [&window]() {
            window.setCentralWidget(new QOpenGLWidget(&window));
        });
    }
    return app.exec();
}

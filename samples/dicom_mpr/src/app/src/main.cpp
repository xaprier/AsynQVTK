#include <QVTKOpenGLNativeWidget.h>

#include <QCoreApplication>

#include "app/Application.hpp"
#include "ui/MainWindow.hpp"

int main(int argc, char* argv[]) {
    // Must be set before the QApplication is constructed. Without it,
    // QOpenGLWidget (AsyncRenderView) contexts are not guaranteed to belong
    // to the same share group as QOpenGLContext::globalShareContext(), which
    // is what AsyncRenderWorker's per-pane worker-thread contexts share
    // with. Textures created on a worker thread would then not reliably be
    // visible to the GUI-thread QOpenGLWidget that blits them.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    app::Application application(argc, argv);

    ui::MainWindow window;
    window.show();
    app::Application::processEvents();

    return app::Application::exec();
}

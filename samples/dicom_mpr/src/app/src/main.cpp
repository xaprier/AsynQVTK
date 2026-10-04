#include <QVTKOpenGLNativeWidget.h>
#include <vtkTimerLog.h>

#include <QCoreApplication>
#include <QMetaType>
#include <array>

#include "app/Application.hpp"
#include "ui/MainWindow.hpp"

int main(int argc, char* argv[]) {
    // must be set before QApplication, needed for worker-thread/GUI-thread GL context sharing
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    // VTK 8.2.0: vtkTimerLog is unlocked and global, corrupts under concurrent renders
    vtkTimerLog::LoggingOff();

    app::Application application(argc, argv);

    // needed for the cross-thread SphereMoved -> OnSphereUpdated queued connection
    using Vec3 = std::array<double, 3>;
    qRegisterMetaType<Vec3>("Vec3");

    ui::MainWindow window;
    window.show();
    app::Application::processEvents();

    return app::Application::exec();
}

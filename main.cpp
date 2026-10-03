#include <QVTKOpenGLNativeWidget.h>
#include <vtkTimerLog.h>

#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include "AsyncRenderView.hpp"

int main(int argc, char* argv[]) {
    // Worker contexts and view contexts must share the same share group so
    // textures can be shared between them. Both must be set before QApplication.
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // NOTE: verified against VTK 8.2.0. Its vtkTimerLog writes to a global,
    // unlocked log on every render; with multiple render threads this corrupts
    // the log (double free). Disable it before any render thread starts.
    // Re-check whether this is still needed if upgrading VTK.
    vtkTimerLog::LoggingOff();

    QApplication app(argc, argv);

    qDebug() << "Main thread ID:" << QThread::currentThreadId();

    constexpr int rows = 3;
    constexpr int columns = 3;

    QWidget* mainWindow = new QWidget;
    mainWindow->setWindowTitle("AsynQVTK Sample");
    mainWindow->resize(1200, 900);

    QVBoxLayout* layout = new QVBoxLayout(mainWindow);
    QGridLayout* grid = new QGridLayout;
    layout->addLayout(grid, 1);

    // Each view renders on its own worker thread, with its own GL context.
    QList<AsyncRenderView*> views;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            auto* view = new AsyncRenderView(mainWindow);
            grid->addWidget(view, r, c);
            views.append(view);
        }
    }

    QHBoxLayout* controls = new QHBoxLayout;
    QPushButton* sphereButton = new QPushButton("Add Sphere", mainWindow);
    QCheckBox* animateCheck = new QCheckBox("Animate", mainWindow);
    controls->addWidget(sphereButton);
    controls->addWidget(animateCheck);
    controls->addStretch();
    layout->addLayout(controls);

    QObject::connect(sphereButton, &QPushButton::clicked, mainWindow, [views]() {
        for (AsyncRenderView* view : views) {
            view->addSphere();
        }
    });
    QObject::connect(animateCheck, &QCheckBox::toggled, mainWindow,
                     [views](bool on) {
                         for (AsyncRenderView* view : views) {
                             view->setAnimating(on);
                         }
                     });

    mainWindow->show();

    const int result = app.exec();
    delete mainWindow;  // let each view shut down its worker cleanly
    return result;
}

#include <QVTKInteractor.h>
#include <QVTKOpenGLNativeWidget.h>
#include <vtkAutoInit.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2)
VTK_MODULE_INIT(vtkInteractionStyle)
VTK_MODULE_INIT(vtkRenderingFreeType)
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkNew.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkSphereSource.h>
#include <vtkTimerLog.h>

#include <AsynQVTK/AsyncRenderView.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// Runs once per view, on its own worker thread: builds the renderer and
// camera-interactive style the library no longer provides by default.
void SetupScene(vtkGenericOpenGLRenderWindow* window) {
    vtkNew<vtkRenderer> renderer;
    renderer->SetBackground(0.1, 0.2, 0.3);
    window->AddRenderer(renderer);

    vtkNew<vtkInteractorStyleTrackballCamera> style;
    window->GetInteractor()->SetInteractorStyle(style);
}

// Each call adds a new sphere, laid out along the x axis.
void AddSphere(vtkGenericOpenGLRenderWindow* window) {
    vtkRenderer* renderer = window->GetRenderers()->GetFirstRenderer();
    const int index = renderer->GetActors()->GetNumberOfItems();

    vtkNew<vtkSphereSource> sphere;
    sphere->SetCenter(index * 1.5, 0.0, 0.0);
    sphere->SetRadius(0.5);
    sphere->SetThetaResolution(32);
    sphere->SetPhiResolution(32);

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(sphere->GetOutputPort());

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(1.0, 0.6, 0.2);

    renderer->AddActor(actor);
    renderer->ResetCamera();
}

void AnimateTick(vtkGenericOpenGLRenderWindow* window) {
    window->GetRenderers()->GetFirstRenderer()->GetActiveCamera()->Azimuth(1.0);
    window->GetRenderers()->GetFirstRenderer()->ResetCameraClippingRange();
}

}  // namespace

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
            view->executeBlocking(&SetupScene);
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
            view->execute(&AddSphere);
        }
    });

    // Animation lives in sample code, not the library: a GUI-thread timer
    // ticks the camera on every view's worker thread via execute().
    QTimer* animationTimer = new QTimer(mainWindow);
    animationTimer->setInterval(16);
    QObject::connect(animationTimer, &QTimer::timeout, mainWindow, [views]() {
        for (AsyncRenderView* view : views) {
            view->execute(&AnimateTick);
        }
    });
    QObject::connect(animateCheck, &QCheckBox::toggled, mainWindow,
                     [animationTimer](bool on) {
                         on ? animationTimer->start() : animationTimer->stop();
                     });

    mainWindow->show();

    const int result = app.exec();
    delete mainWindow;  // let each view shut down its worker cleanly
    return result;
}

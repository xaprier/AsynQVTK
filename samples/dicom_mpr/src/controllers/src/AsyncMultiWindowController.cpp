#include "controllers/AsyncMultiWindowController.hpp"

#include <AsynQVTK/AsyncRenderView.hpp>
#include <vtkCallbackCommand.h>
#include <vtkCommand.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkResliceImageViewer.h>

#include <QString>
#include <array>

#include "controllers/AsyncSliceController.hpp"
#include "controllers/AsyncSphereController.hpp"

namespace controllers {

AsyncMultiWindowController::AsyncMultiWindowController(QObject* parent)
    : IAsyncViewController(parent) {}

AsyncMultiWindowController::~AsyncMultiWindowController() = default;

void AsyncMultiWindowController::OnRenderStart(vtkObject*, unsigned long, void* clientData, void*) {
    auto* ctx = static_cast<PaneRenderContext*>(clientData);
    ctx->mutex->lock();
    if (ctx->riv)
        ctx->riv->Render();
}

void AsyncMultiWindowController::OnRenderEnd(vtkObject*, unsigned long, void* clientData, void*) {
    auto* ctx = static_cast<PaneRenderContext*>(clientData);
    ctx->mutex->unlock();
}

void AsyncMultiWindowController::_Initialize(const std::vector<AsyncRenderView*>& views) {
    if (m_initialized)
        return;

    if (views.size() != 3 || !views[0] || !views[1] || !views[2]) {
        emit StatusChanged(tr("Cannot initialize setup in MultiWindow: expected 3 panes."));
        return;
    }

    m_views = views;

    m_sliceController = std::make_unique<AsyncSliceController>(this);
    m_sliceController->Initialize(m_views);

    // Per-pane StartEvent/EndEvent observer pair, replacing
    // render::RenderScheduler's RivRenderTarget ordering: StartEvent locks
    // the shared sphere mutex and runs this pane's riv->Render() before the
    // window renders; EndEvent unlocks. Installed unconditionally here,
    // before any sphere exists, because RIV render ordering is always
    // needed regardless of whether a sphere has been added — an uncontended
    // QMutex lock/unlock costs a few tens of nanoseconds, negligible next
    // to a render call.
    const auto& rivs = m_sliceController->GetViewers();
    for (size_t i = 0; i < m_views.size() && i < rivs.size(); ++i) {
        AsyncRenderView* view = m_views[i];
        vtkResliceImageViewer* riv = rivs[i];
        if (!view || !riv)
            continue;

        auto ctx = std::make_unique<PaneRenderContext>();
        ctx->mutex = &m_sphereMutex;
        ctx->riv = riv;
        PaneRenderContext* ctxPtr = ctx.get();
        m_paneContexts.push_back(std::move(ctx));

        view->executeBlocking([ctxPtr](vtkGenericOpenGLRenderWindow* window) {
            vtkNew<vtkCallbackCommand> startCmd;
            startCmd->SetClientData(ctxPtr);
            startCmd->SetCallback(&AsyncMultiWindowController::OnRenderStart);
            window->AddObserver(vtkCommand::StartEvent, startCmd);

            vtkNew<vtkCallbackCommand> endCmd;
            endCmd->SetClientData(ctxPtr);
            endCmd->SetCallback(&AsyncMultiWindowController::OnRenderEnd);
            window->AddObserver(vtkCommand::EndEvent, endCmd);
        });
    }

    m_initialized = true;
    emit ViewersReady();
}

void AsyncMultiWindowController::_AddSphere() {
    if (!m_initialized) {
        emit StatusChanged(tr("Cannot add sphere. AsyncMultiWindowController is not initialized."));
        return;
    }

    if (!m_sliceController) {
        emit StatusChanged(tr("Cannot add sphere. AsyncSliceController is not initialized."));
        return;
    }

    if (m_sphereAdded) {
        emit StatusChanged(tr("Sphere is already added to MultiWindow."));
        return;
    }

    static constexpr AsyncSphereController::DragPlane kPlanes[3] = {
        AsyncSphereController::DragPlane::Axial,
        AsyncSphereController::DragPlane::Coronal,
        AsyncSphereController::DragPlane::Sagittal,
    };

    m_sphereController = std::make_unique<AsyncSphereController>();
    m_sphereController->SetMutex(&m_sphereMutex);

    for (size_t i = 0; i < m_views.size() && i < 3; ++i) {
        if (m_views[i])
            m_sphereController->AddPane(m_views[i], kPlanes[i]);
    }

    QObject::connect(
        m_sphereController.get(), &AsyncSphereController::SphereMoved,
        m_sliceController.get(), &AsyncSliceController::OnSphereUpdated);

    if (m_dicomLoaded) {
        double bounds[6];
        m_imageData->GetBounds(bounds);
        m_sphereController->SetPosition({
            (bounds[0] + bounds[1]) * 0.5,
            (bounds[2] + bounds[3]) * 0.5,
            (bounds[4] + bounds[5]) * 0.5,
        });
    }

    m_sphereController->SetRadius(m_sphereRadius);
    m_sphereController->SetColor(m_sphereColor);

    m_sphereAdded = true;
}

void AsyncMultiWindowController::_RemoveSphere() {
    if (!m_initialized) {
        emit StatusChanged(tr("Cannot remove sphere. AsyncMultiWindowController is not initialized."));
        return;
    }

    if (m_sphereController) {
        m_sphereController->Cleanup();
        m_sphereController.reset();
    }

    m_sphereAdded = false;
    emit StatusChanged(tr("Sphere removed from MultiWindow."));
}

void AsyncMultiWindowController::_SetSphereRadius(double radius) {
    if (!m_initialized) {
        emit StatusChanged(tr("Cannot set sphere radius. AsyncMultiWindowController is not initialized."));
        return;
    }

    if (!m_sphereAdded || !m_sphereController) {
        emit StatusChanged(tr("No sphere to set radius for."));
        return;
    }

    m_sphereRadius = radius;
    m_sphereController->SetRadius(radius);
}

void AsyncMultiWindowController::_SetSphereColor(const std::array<double, 3> color) {
    if (!m_initialized) {
        emit StatusChanged(tr("Cannot set sphere color. AsyncMultiWindowController is not initialized."));
        return;
    }

    if (!m_sphereAdded || !m_sphereController) {
        emit StatusChanged(tr("No sphere to set color for."));
        return;
    }

    m_sphereColor = color;
    m_sphereController->SetColor(m_sphereColor);
}

void AsyncMultiWindowController::_SetImageData(vtkImageData* imageData) {
    if (!m_initialized || !imageData)
        return;

    m_imageData = vtkSmartPointer<vtkImageData>::New();
    m_imageData->ShallowCopy(imageData);

    if (!m_dicomLoaded) {
        _SetupPipeline(imageData);
        m_dicomLoaded = true;
    } else {
        if (m_sliceController)
            m_sliceController->SetImageData(imageData);
    }

    if (m_sphereController) {
        double bounds[6];
        imageData->GetBounds(bounds);
        m_sphereController->SetPosition({
            (bounds[0] + bounds[1]) * 0.5,
            (bounds[2] + bounds[3]) * 0.5,
            (bounds[4] + bounds[5]) * 0.5,
        });
    }

    emit StatusChanged(tr("DICOM image loaded successfully into MultiWindow."));
}

void AsyncMultiWindowController::_SetupPipeline(vtkImageData* imageData) {
    if (!imageData)
        return;

    if (m_sphereController) {
        QObject::connect(
            m_sphereController.get(), &AsyncSphereController::SphereMoved,
            m_sliceController.get(), &AsyncSliceController::OnSphereUpdated);
    }

    m_sliceController->SetImageData(imageData);
}

}  // namespace controllers

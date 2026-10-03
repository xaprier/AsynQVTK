#include "controllers/AsyncSliceController.hpp"

#include <AsynQVTK/AsyncRenderView.hpp>
#include <vtkCamera.h>
#include <vtkCommand.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageProperty.h>
#include <vtkImageViewer2.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkResliceCursorWidget.h>
#include <vtkResliceImageViewer.h>

#include <algorithm>

#include "controllers/AsyncResliceImageViewerInteractorStyle.hpp"

namespace controllers {

AsyncSliceController::AsyncSliceController(QObject* parent)
    : IControllerBase(parent) {}

AsyncSliceController::~AsyncSliceController() = default;

void AsyncSliceController::Initialize(const std::vector<AsyncRenderView*>& views) {
    m_views = views;
    SetupViewers();
}

void AsyncSliceController::SetupViewers() {
    static constexpr int kOrientations[3] = {
        vtkImageViewer2::SLICE_ORIENTATION_XY,
        vtkImageViewer2::SLICE_ORIENTATION_XZ,
        vtkImageViewer2::SLICE_ORIENTATION_YZ,
    };

    m_rivs.clear();
    m_rivs.resize(3);
    m_rivStyle = vtkSmartPointer<AsyncResliceImageViewerInteractorStyle>::New();

    for (size_t i = 0; i < 3; ++i) {
        if (i >= m_views.size() || !m_views[i])
            continue;

        AsyncRenderView* view = m_views[i];
        const int orientation = kOrientations[i];
        vtkSmartPointer<AsyncResliceImageViewerInteractorStyle> rivStyle = m_rivStyle;

        // Safe to capture &riv by reference: executeBlocking() does not
        // return until this lambda has fully run, so riv (a local on this
        // thread's stack) is still alive when it's assigned.
        vtkSmartPointer<vtkResliceImageViewer> riv;
        view->executeBlocking([&riv, orientation, rivStyle](vtkGenericOpenGLRenderWindow* window) {
            riv = vtkSmartPointer<vtkResliceImageViewer>::New();
            riv->SetRenderWindow(window);

            vtkRenderWindowInteractor* interactor = window->GetInteractor();
            if (interactor)
                riv->SetupInteractor(interactor);

            vtkInteractorObserver* style = interactor ? interactor->GetInteractorStyle() : nullptr;
            if (style) {
                style->AddObserver(vtkCommand::WindowLevelEvent, rivStyle);
                style->AddObserver(vtkCommand::StartWindowLevelEvent, rivStyle);
                style->AddObserver(vtkCommand::EndWindowLevelEvent, rivStyle);
                rivStyle->RegisterInteractorStyle(style, riv);
            }

            riv->SetSliceOrientation(orientation);
        });

        m_rivs[i] = riv;
    }

    m_rivStyle->SetViewers(m_rivs);
    m_rivStyle->SetViews(m_views);
}

void AsyncSliceController::SetImageData(vtkImageData* image) {
    if (!image)
        return;

    m_image = vtkSmartPointer<vtkImageData>::New();
    m_image->ShallowCopy(image);

    SetupPipeline();
}

void AsyncSliceController::SetupPipeline() {
    if (!m_image)
        return;

    static constexpr int kOrientations[3] = {
        vtkImageViewer2::SLICE_ORIENTATION_XY,
        vtkImageViewer2::SLICE_ORIENTATION_XZ,
        vtkImageViewer2::SLICE_ORIENTATION_YZ,
    };
    static constexpr int kSliceAxis[3] = {2, 1, 0};

    double spacing[3];
    double origin[3];
    double bounds[6];
    m_image->GetSpacing(spacing);
    m_image->GetOrigin(origin);
    m_image->GetBounds(bounds);

    const double worldPos[3] = {
        (bounds[0] + bounds[1]) * 0.5,
        (bounds[2] + bounds[3]) * 0.5,
        (bounds[4] + bounds[5]) * 0.5,
    };

    for (size_t i = 0; i < m_rivs.size(); ++i) {
        if (i >= m_views.size() || !m_views[i] || !m_rivs[i])
            continue;

        vtkResliceImageViewer* riv = m_rivs[i];
        vtkImageData* image = m_image;
        const int orientation = kOrientations[i];
        const int ax = kSliceAxis[i];
        const double originAx = origin[ax];
        const double spacingAx = spacing[ax];
        const double worldPosAx = worldPos[ax];

        m_views[i]->executeBlocking([riv, image, orientation, originAx, spacingAx, worldPosAx](vtkGenericOpenGLRenderWindow*) {
            riv->SetInputData(image);
            riv->SetResliceModeToAxisAligned();
            riv->SetSliceOrientation(orientation);

            const int sliceIdx = static_cast<int>((worldPosAx - originAx) / spacingAx + 0.5);
            const int clamped = std::max(riv->GetSliceMin(), std::min(riv->GetSliceMax(), sliceIdx));
            riv->SetSlice(clamped);

            riv->GetResliceCursorWidget()->InvokeEvent(vtkCommand::InteractionEvent);
            riv->SetColorLevel(500);
            riv->SetColorWindow(2000);
            riv->GetImageActor()->GetProperty()->SetColorWindow(400);
            riv->GetImageActor()->GetProperty()->SetColorLevel(127.5);
            riv->GetRenderer()->ResetCamera();
        });
    }

    FitToView();
}

void AsyncSliceController::FitToView() {
    if (!m_image)
        return;

    double bounds[6];
    m_image->GetBounds(bounds);

    for (size_t i = 0; i < m_rivs.size(); ++i) {
        if (i >= m_views.size() || !m_views[i] || !m_rivs[i])
            continue;

        vtkResliceImageViewer* riv = m_rivs[i];

        double width = 0.0;
        double height = 0.0;

        switch (i) {
            case 0:
                width = bounds[1] - bounds[0];
                height = bounds[3] - bounds[2];
                break;
            case 1:
                width = bounds[1] - bounds[0];
                height = bounds[5] - bounds[4];
                break;
            case 2:
                width = bounds[3] - bounds[2];
                height = bounds[5] - bounds[4];
                break;
        }

        if (width <= 0.0 || height <= 0.0)
            continue;

        m_views[i]->execute([riv, width, height](vtkGenericOpenGLRenderWindow*) {
            vtkRenderer* renderer = riv->GetRenderer();
            if (!renderer)
                return;
            vtkCamera* camera = renderer->GetActiveCamera();
            vtkRenderWindow* renderWindow = renderer->GetRenderWindow();
            if (!camera || !renderWindow)
                return;

            const int* size = renderWindow->GetSize();

            double vp[4];
            renderer->GetViewport(vp);
            const double vpWidth = vp[2] - vp[0];
            const double vpHeight = vp[3] - vp[1];

            const double viewportAspect =
                (size && size[0] > 0 && size[1] > 0 && vpHeight > 0.0)
                    ? (static_cast<double>(size[0]) * vpWidth) / (static_cast<double>(size[1]) * vpHeight)
                    : 1.0;

            const double imageAspect = width / height;
            double parallelScale = (imageAspect > viewportAspect) ? width / (2.0 * viewportAspect) : height / 2.0;
            parallelScale *= 1.05;

            camera->ParallelProjectionOn();
            camera->SetParallelScale(parallelScale);
            renderer->ResetCameraClippingRange();
        });
    }
}

void AsyncSliceController::OnSphereUpdated(const Vec3& worldPos) {
    if (!m_image || m_rivs.size() < 3)
        return;

    double spacing[3];
    double origin[3];
    m_image->GetSpacing(spacing);
    m_image->GetOrigin(origin);

    static constexpr int kSliceAxis[3] = {2, 1, 0};

    for (int i = 0; i < 3; ++i) {
        if (static_cast<size_t>(i) >= m_views.size() || !m_views[i] || !m_rivs[static_cast<size_t>(i)])
            continue;

        const int ax = kSliceAxis[i];
        if (spacing[ax] == 0.0)
            continue;

        vtkResliceImageViewer* riv = m_rivs[static_cast<size_t>(i)];
        const double worldPosAx = worldPos[ax];
        const double originAx = origin[ax];
        const double spacingAx = spacing[ax];

        m_views[static_cast<size_t>(i)]->execute([riv, worldPosAx, originAx, spacingAx](vtkGenericOpenGLRenderWindow*) {
            const int sliceIdx = static_cast<int>((worldPosAx - originAx) / spacingAx + 0.5);
            const int clamped = std::max(riv->GetSliceMin(), std::min(riv->GetSliceMax(), sliceIdx));
            riv->SetSlice(clamped);
        });
    }
}

const std::vector<vtkSmartPointer<vtkResliceImageViewer>>&
AsyncSliceController::GetViewers() const {
    return m_rivs;
}

}  // namespace controllers

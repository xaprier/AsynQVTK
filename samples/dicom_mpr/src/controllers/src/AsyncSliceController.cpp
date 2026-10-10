#include "controllers/AsyncSliceController.hpp"

#include <vtkCamera.h>
#include <vtkCommand.h>
#include <vtkCullerCollection.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImagePermute.h>
#include <vtkImageProperty.h>
#include <vtkImageViewer2.h>
#include <vtkNew.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkResliceCursorWidget.h>
#include <vtkResliceImageViewer.h>

#include <AsynQVTK/AsyncRenderView.hpp>
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
    // Pane 2 (sagittal) is set to XY, not YZ: it's fed m_sagittalImage (X
    // and Z axes permuted, see SetImageData), where stepping through
    // original-X positions is this copy's own Z axis. See m_sagittalImage's
    // doc comment for why.
    static constexpr int kOrientations[3] = {
        vtkImageViewer2::SLICE_ORIENTATION_XY,
        vtkImageViewer2::SLICE_ORIENTATION_XZ,
        vtkImageViewer2::SLICE_ORIENTATION_XY,
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

            // vtkFrustumCoverageCuller (renderer's default culler) calls
            // GetBounds() on every prop every render, which can run a
            // vtkAlgorithm pipeline update and land in
            // vtkGarbageCollector::Collect(). That collector is a single
            // process-wide singleton with no internal locking, so two panes'
            // worker threads landing in it at the same time corrupts its
            // shared state -- this crashed once (SIGSEGV in
            // vtkGarbageCollectorImpl::VisitTarjan). Nothing in this scene
            // needs frustum-coverage LOD, so drop the culler instead of
            // trying to serialize around VTK's unsynchronized global.
            riv->GetRenderer()->GetCullers()->RemoveAllItems();

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

    // (newX, newY, newZ) = (origY, origZ, origX): puts original X on the
    // permuted copy's slowest-varying axis, matching XY's contiguous case.
    vtkNew<vtkImagePermute> permute;
    permute->SetInputData(m_image);
    permute->SetFilteredAxes(1, 2, 0);
    permute->Update();
    m_sagittalImage = vtkSmartPointer<vtkImageData>::New();
    m_sagittalImage->ShallowCopy(permute->GetOutput());

    SetupPipeline();
}

void AsyncSliceController::SetupPipeline() {
    if (!m_image)
        return;

    // Pane 2 (sagittal) uses XY against m_sagittalImage, not YZ against
    // m_image: see m_sagittalImage's doc comment and SetupViewers().
    static constexpr int kOrientations[3] = {
        vtkImageViewer2::SLICE_ORIENTATION_XY,
        vtkImageViewer2::SLICE_ORIENTATION_XZ,
        vtkImageViewer2::SLICE_ORIENTATION_XY,
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
        vtkImageData* image = (i == 2) ? m_sagittalImage.GetPointer() : m_image.GetPointer();
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
        const size_t pi = static_cast<size_t>(i);

        // Intentionally unclamped: GetSliceMin()/GetSliceMax() are NOT plain
        // field getters, they call GetSliceRange(), which calls
        // input->UpdateInformation() -- an actual pipeline call. This runs
        // on the GUI thread (SphereMoved is a cross-thread queued
        // connection), while this pane's worker thread may be concurrently
        // rendering, so touching the pipeline here is unsafe. SetSlice()
        // below already clamps internally to the valid range, and runs on
        // the worker thread (inside execute()'s lambda), where it's safe.
        const int sliceIdx = static_cast<int>((worldPos[ax] - origin[ax]) / spacing[ax] + 0.5);

        m_pendingSlice[pi].store(sliceIdx, std::memory_order_relaxed);

        // skip if a dispatch is already in flight for this pane, it'll pick up the latest m_pendingSlice
        if (m_sliceUpdateInFlight[pi].exchange(true, std::memory_order_acq_rel))
            continue;

        std::atomic<int>* pending = &m_pendingSlice[pi];
        std::atomic<bool>* inFlight = &m_sliceUpdateInFlight[pi];
        m_views[pi]->execute([riv, pending, inFlight](vtkGenericOpenGLRenderWindow*) {
            // Loop rather than apply-once: if a newer slice index was
            // written (whose OnSphereUpdated() call saw inFlight still
            // true, so it skipped dispatching, trusting us to pick it up)
            // after we read `target` below but before we clear inFlight,
            // that write would otherwise never get applied. Re-check after
            // clearing the flag and, if the value moved on and nobody else
            // has claimed the slot, apply the latest value too.
            while (true) {
                const int target = pending->load(std::memory_order_relaxed);
                if (riv->GetSlice() != target)
                    riv->SetSlice(target);

                inFlight->store(false, std::memory_order_release);

                if (pending->load(std::memory_order_relaxed) == target)
                    break;  // nothing changed while we were applying it
                if (inFlight->exchange(true, std::memory_order_acq_rel))
                    break;  // someone else already claimed it and will dispatch
            }
        });
    }
}

const std::vector<vtkSmartPointer<vtkResliceImageViewer>>&
AsyncSliceController::GetViewers() const {
    return m_rivs;
}

}  // namespace controllers

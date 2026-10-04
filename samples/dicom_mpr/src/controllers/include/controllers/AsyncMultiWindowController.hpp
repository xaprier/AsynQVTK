#ifndef ASYNCMULTIWINDOWCONTROLLER_HPP
#define ASYNCMULTIWINDOWCONTROLLER_HPP

#include <vtkSmartPointer.h>

#include <memory>
#include <vector>

#include "controllers/IAsyncViewController.hpp"

class vtkCallbackCommand;
class vtkObject;
class vtkResliceImageViewer;

namespace controllers {

class AsyncSliceController;
class AsyncSphereController;

/**
 * @brief View controller that uses one AsyncRenderView (worker thread) per
 *        DICOM plane — fork of MultiWindowController built on the AsynQVTK
 *        library instead of render::RenderScheduler.
 *
 * Expects three AsyncRenderView instances (Axial, Coronal, Sagittal).
 */
class AsyncMultiWindowController : public IAsyncViewController {
    Q_OBJECT

  public:
    explicit AsyncMultiWindowController(QObject* parent = nullptr);

    /**
     * @brief Removes each pane's StartEvent observer (blocking, on that
     *        pane's own worker thread) before m_paneContexts is destroyed.
     *
     * Not defaulted: MultiWindowView destroys this controller (and
     * therefore m_paneContexts) before the AsyncRenderView widgets/worker
     * threads. If a render were still queued on a worker thread at that
     * point, OnRenderStart() would fire on an already-freed
     * PaneRenderContext.
     */
    ~AsyncMultiWindowController() override;

    [[nodiscard]] AsyncSliceController* GetSliceController() const { return m_sliceController.get(); }

  private:
    void _Initialize(const std::vector<AsyncRenderView*>& views) override;
    void _AddSphere() override;
    void _RemoveSphere() override;
    void _SetSphereRadius(double radius) override;
    void _SetSphereColor(const std::array<double, 3> color) override;
    void _SetImageData(vtkImageData* imageData) override;
    void _SetupPipeline(vtkImageData* imageData) override;

    // per-pane context for the StartEvent callback, kept alive in m_paneContexts
    struct PaneRenderContext {
        vtkResliceImageViewer* riv{nullptr};
        AsyncRenderView* view{nullptr};  // used by ~AsyncMultiWindowController() to remove startCmd
        vtkSmartPointer<vtkCallbackCommand> startCmd;
    };
    static void OnRenderStart(vtkObject* caller, unsigned long eventId, void* clientData, void* callData);

    std::unique_ptr<AsyncSliceController> m_sliceController;
    std::unique_ptr<AsyncSphereController> m_sphereController;
    std::vector<std::unique_ptr<PaneRenderContext>> m_paneContexts;
};

}  // namespace controllers

#endif  // ASYNCMULTIWINDOWCONTROLLER_HPP

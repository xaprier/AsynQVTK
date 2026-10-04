#ifndef ASYNCMULTIWINDOWCONTROLLER_HPP
#define ASYNCMULTIWINDOWCONTROLLER_HPP

#include <memory>
#include <vector>

#include "controllers/IAsyncViewController.hpp"

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
    };
    static void OnRenderStart(vtkObject* caller, unsigned long eventId, void* clientData, void* callData);

    std::unique_ptr<AsyncSliceController> m_sliceController;
    std::unique_ptr<AsyncSphereController> m_sphereController;
    std::vector<std::unique_ptr<PaneRenderContext>> m_paneContexts;
};

}  // namespace controllers

#endif  // ASYNCMULTIWINDOWCONTROLLER_HPP

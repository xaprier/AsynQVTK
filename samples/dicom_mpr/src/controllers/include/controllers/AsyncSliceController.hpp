#ifndef ASYNCSLICECONTROLLER_HPP
#define ASYNCSLICECONTROLLER_HPP

#include <vtkSmartPointer.h>

#include <array>
#include <atomic>
#include <vector>

#include "controllers/IControllerBase.hpp"

class vtkImageData;
class vtkResliceImageViewer;
class AsyncRenderView;

namespace controllers {

class AsyncResliceImageViewerInteractorStyle;

/**
 * @brief Manages three vtkResliceImageViewer instances for orthogonal slice
 *        display in AsynQVTK multiwindow mode — fork of SliceController,
 *        multiwindow-only (no viewport-mode branch), adapted so every RIV
 *        mutation runs on its owning pane's AsyncRenderView worker thread.
 */
class AsyncSliceController : public IControllerBase {
    Q_OBJECT

  public:
    enum Orientation {
        Axial = 0,
        Coronal = 1,
        Sagittal = 2
    };
    using Vec3 = std::array<double, 3>;

    explicit AsyncSliceController(QObject* parent = nullptr);
    ~AsyncSliceController() override;

    /** @brief Initialises the three viewers, one per pane, each built on that pane's worker thread. */
    void Initialize(const std::vector<AsyncRenderView*>& views);

    /** @brief Pushes @p image into the reslice pipeline of every pane. */
    void SetImageData(vtkImageData* image);

    /** @brief Returns the underlying viewer list (axial/coronal/sagittal order). */
    const std::vector<vtkSmartPointer<vtkResliceImageViewer>>& GetViewers() const;

    /**
     * @brief Fit each plane's camera to fill its viewport exactly.
     *
     * Safe to call at any time; no-ops if image data is not yet loaded.
     * Call after viewport geometry changes (layout switch) to avoid letterboxing.
     */
    void FitToView();

  public slots:
    /** @brief Scrolls each slice plane to the position nearest to @p worldPos. Coalesces per pane. */
    void OnSphereUpdated(const Vec3& worldPos);

  private:
    void SetupViewers();
    void SetupPipeline();

    vtkSmartPointer<vtkImageData> m_image;
    // Sagittal (pane 2) is fed this axis-permuted copy of m_image instead of
    // m_image itself: vtkImageMapper3D's texture-extraction fast path only
    // triggers when the slice-fixed axis is the volume's slowest-varying one
    // (see MakeTextureData's contiguous-extent check), which for the raw
    // volume is true only for the axial (XY) slice. Permuting axes so that
    // original X becomes this copy's slowest axis makes the sagittal pane
    // hit that same fast path.
    vtkSmartPointer<vtkImageData> m_sagittalImage;
    vtkSmartPointer<AsyncResliceImageViewerInteractorStyle> m_rivStyle;
    std::vector<vtkSmartPointer<vtkResliceImageViewer>> m_rivs;
    std::vector<AsyncRenderView*> m_views;

    // per-pane "dispatch in flight" state for OnSphereUpdated, indexed 0/1/2 = axial/coronal/sagittal
    std::array<std::atomic<int>, 3> m_pendingSlice{};
    std::array<std::atomic<bool>, 3> m_sliceUpdateInFlight{};
};

}  // namespace controllers

#endif  // ASYNCSLICECONTROLLER_HPP

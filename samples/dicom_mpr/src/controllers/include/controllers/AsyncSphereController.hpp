#ifndef ASYNCSPHERECONTROLLER_HPP
#define ASYNCSPHERECONTROLLER_HPP

#include <vtkSmartPointer.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "controllers/IControllerBase.hpp"

class AsyncRenderView;
class vtkActor;
class vtkCallbackCommand;
class vtkCellPicker;
class vtkObject;
class vtkRenderer;
class vtkSphereSource;

namespace controllers {

/**
 * @brief Draggable sphere actor for AsynQVTK multiwindow mode — fork of
 *        SphereController, adapted so every VTK mutation runs on the owning
 *        pane's AsyncRenderView worker thread instead of directly.
 *
 * Each pane owns its own vtkSphereSource/vtkPolyDataMapper/vtkActor —
 * nothing VTK-side is shared across panes' worker threads. A single shared
 * vtkSphereSource + mutex raced: riv->SetSlice() pulls actor bounds
 * (ResetCameraClippingRange) without taking the mutex, so it could read the
 * source mid-mutation from another pane's thread. Per-pane sources remove
 * the shared object instead of trying to guard every VTK call path that
 * touches it.
 *
 * Position/radius are tracked in a shared, atomic-backed SphereState block
 * (m_state) rather than plain members: SetPosition()/SetRadius() run on
 * whichever thread calls them (a pane's own worker thread during a drag, or
 * the GUI thread for slider/programmatic calls) while every pane's worker
 * thread reads them back inside its dispatched lambda, so plain members
 * would be subject to torn reads. The lambdas capture the shared_ptr<SphereState>
 * by value (not `this`) so a still-queued lambda can't dereference a freed
 * controller. Color is tracked as a plain member (m_color), not read back
 * from any VTK object — lower risk, no pipeline Update() call involved.
 */
class AsyncSphereController : public IControllerBase {
    Q_OBJECT

  public:
    enum class DragPlane { Axial,
                           Coronal,
                           Sagittal };
    using Vec3 = std::array<double, 3>;

    explicit AsyncSphereController(QObject* parent = nullptr);
    ~AsyncSphereController() override;

    /**
     * @brief Registers one pane: builds its sphere actor/mapper and attaches
     *        the drag observers to its interactor. Call once per pane, after
     *        that pane's vtkResliceImageViewer (and therefore its renderer)
     *        already exists.
     */
    void AddPane(AsyncRenderView* view, DragPlane plane);

    /** @brief Removes the actor from every pane's renderer and detaches all observers. */
    void Cleanup();

    /** @brief Moves the sphere. Coalesces per pane (see SphereState::positionInFlight). */
    void SetPosition(const Vec3& pos);
    void SetRadius(double radius);
    void SetColor(const Vec3& rgb);

    [[nodiscard]] double GetRadius() const;
    [[nodiscard]] Vec3 GetPosition() const;
    [[nodiscard]] Vec3 GetColor() const;
    [[nodiscard]] bool IsDragging() const;
    [[nodiscard]] vtkActor* ActorFor(vtkRenderer*) const;

  Q_SIGNALS:
    void SphereMoved(const Vec3& worldPos);

  private:
    struct PaneEntry {
        AsyncRenderView* view{nullptr};
        vtkRenderer* renderer{nullptr};  // pane-owned; kept alive by the renderer's own actor reference
        vtkActor* actor{nullptr};        // pane-owned; same
        vtkSmartPointer<vtkSphereSource> source;  // pane-owned; this pane's own copy, never shared
        DragPlane plane{DragPlane::Axial};
    };

    DragPlane PlaneFor(vtkRenderer* renderer) const;

    static void OnLeftButtonDown(vtkObject*, unsigned long, void* clientData, void*);
    static void OnMouseMove(vtkObject*, unsigned long, void* clientData, void*);
    static void OnLeftButtonUp(vtkObject*, unsigned long, void* clientData, void*);

    vtkSmartPointer<vtkCellPicker> m_picker;
    vtkSmartPointer<vtkCallbackCommand> m_leftDownCmd;
    vtkSmartPointer<vtkCallbackCommand> m_mouseMoveCmd;
    vtkSmartPointer<vtkCallbackCommand> m_leftUpCmd;

    std::vector<PaneEntry> m_panes;

    // Shared, atomic-backed sphere position/radius + per-pane "dispatch
    // in flight" flags. Held via shared_ptr and captured by value (never
    // `this`) in SetPosition()/SetRadius()'s dispatched lambdas, so the
    // block outlives the controller if a lambda is still queued on a
    // worker thread when Cleanup()/~AsyncSphereController() run, and so
    // every cross-thread read/write of position/radius is atomic (no torn
    // reads of what used to be plain std::array<double,3>/double members).
    //
    // positionInFlight/radiusInFlight are indexed the same as m_panes.
    // SetPosition/SetRadius skip dispatching to a pane that's still
    // catching up, since the in-flight lambda re-reads x/y/z/radius
    // anyway; the lambda itself re-checks after clearing its flag so a
    // value written while it was running isn't silently dropped (see
    // SetPosition/SetRadius).
    struct SphereState {
        std::atomic<double> x{0.0};
        std::atomic<double> y{0.0};
        std::atomic<double> z{0.0};
        std::atomic<double> radius{3.0};
        std::array<std::atomic<bool>, 3> positionInFlight{};
        std::array<std::atomic<bool>, 3> radiusInFlight{};
    };
    std::shared_ptr<SphereState> m_state = std::make_shared<SphereState>();

    // no VTK object backs this; not atomic since SetColor() has no pipeline
    // Update() call and isn't on a worker-thread-contended hot path
    Vec3 m_color{1.0, 0.3, 0.3};

    bool m_isDragging{false};
    vtkRenderer* m_activeRenderer{nullptr};
    DragPlane m_activePlane{DragPlane::Axial};
};

}  // namespace controllers

#endif  // ASYNCSPHERECONTROLLER_HPP

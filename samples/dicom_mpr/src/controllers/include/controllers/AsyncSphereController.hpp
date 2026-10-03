#ifndef ASYNCSPHERECONTROLLER_HPP
#define ASYNCSPHERECONTROLLER_HPP

#include <vtkSmartPointer.h>

#include <array>
#include <vector>

#include "controllers/IControllerBase.hpp"

class QMutex;
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
 * Usage:
 *   1. SetMutex(mutex)        — once, before AddPane(); non-owning, must
 *                                outlive this object (owned by the parent
 *                                AsyncMultiWindowController, shared with its
 *                                per-pane StartEvent/EndEvent render lock).
 *   2. AddPane(view, plane)   — once per pane. Builds this pane's actor
 *                                (sharing the controller's single
 *                                vtkSphereSource) inside view->executeBlocking(),
 *                                adds it to the pane's own renderer, and
 *                                attaches the drag observers to the pane's
 *                                own interactor.
 *   3. SetPosition / SetRadius / SetColor as needed.
 *   4. Cleanup() before removing the sphere from the scene.
 *
 * Mouse-drag callbacks (OnLeftButtonDown/Move/Up) are unchanged in spirit
 * from the original: they already run correctly on the owning pane's worker
 * thread, because they fire from inside that pane's
 * AsyncRenderWorker::processEvent() dispatch. Only SetPosition/SetRadius
 * (which mutate the one shared vtkSphereSource) take the mutex and dispatch
 * a redraw to every pane via execute().
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
     * @brief Set the mutex that guards the shared vtkSphereSource.
     *
     * Non-owning — lifetime must exceed this object. Call once, before any
     * AddPane() or drag interaction.
     */
    void SetMutex(QMutex* mutex);

    /**
     * @brief Registers one pane: builds its sphere actor/mapper and attaches
     *        the drag observers to its interactor. Call once per pane, after
     *        that pane's vtkResliceImageViewer (and therefore its renderer)
     *        already exists.
     */
    void AddPane(AsyncRenderView* view, DragPlane plane);

    /** @brief Removes the actor from every pane's renderer and detaches all observers. */
    void Cleanup();

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
        DragPlane plane{DragPlane::Axial};
    };

    DragPlane PlaneFor(vtkRenderer* renderer) const;

    static void OnLeftButtonDown(vtkObject*, unsigned long, void* clientData, void*);
    static void OnMouseMove(vtkObject*, unsigned long, void* clientData, void*);
    static void OnLeftButtonUp(vtkObject*, unsigned long, void* clientData, void*);

    vtkSmartPointer<vtkSphereSource> m_sphereSource;
    vtkSmartPointer<vtkCellPicker> m_picker;
    vtkSmartPointer<vtkCallbackCommand> m_leftDownCmd;
    vtkSmartPointer<vtkCallbackCommand> m_mouseMoveCmd;
    vtkSmartPointer<vtkCallbackCommand> m_leftUpCmd;

    std::vector<PaneEntry> m_panes;
    QMutex* m_mutex{nullptr};  // non-owning, owned by the parent AsyncMultiWindowController

    bool m_isDragging{false};
    vtkRenderer* m_activeRenderer{nullptr};
    DragPlane m_activePlane{DragPlane::Axial};
};

}  // namespace controllers

#endif  // ASYNCSPHERECONTROLLER_HPP

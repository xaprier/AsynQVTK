#include "controllers/AsyncSphereController.hpp"

#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCellPicker.h>
#include <vtkCommand.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkPlane.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkSphereSource.h>

#include <AsynQVTK/AsyncRenderView.hpp>
#include <algorithm>

namespace controllers {

AsyncSphereController::AsyncSphereController(QObject* parent)
    : IControllerBase(parent) {
    m_picker = vtkSmartPointer<vtkCellPicker>::New();
    m_picker->SetTolerance(0.005);

    m_leftDownCmd = vtkSmartPointer<vtkCallbackCommand>::New();
    m_leftDownCmd->SetCallback(&AsyncSphereController::OnLeftButtonDown);
    m_leftDownCmd->SetClientData(this);

    m_mouseMoveCmd = vtkSmartPointer<vtkCallbackCommand>::New();
    m_mouseMoveCmd->SetCallback(&AsyncSphereController::OnMouseMove);
    m_mouseMoveCmd->SetClientData(this);

    m_leftUpCmd = vtkSmartPointer<vtkCallbackCommand>::New();
    m_leftUpCmd->SetCallback(&AsyncSphereController::OnLeftButtonUp);
    m_leftUpCmd->SetClientData(this);
}

AsyncSphereController::~AsyncSphereController() = default;

void AsyncSphereController::AddPane(AsyncRenderView* view, DragPlane plane) {
    if (!view)
        return;

    for (const auto& p : m_panes) {
        if (p.view == view)
            return;
    }

    vtkSmartPointer<vtkCallbackCommand> leftDown = m_leftDownCmd;
    vtkSmartPointer<vtkCallbackCommand> mouseMove = m_mouseMoveCmd;
    vtkSmartPointer<vtkCallbackCommand> leftUp = m_leftUpCmd;
    const Vec3 position = {
        m_state->x.load(std::memory_order_relaxed),
        m_state->y.load(std::memory_order_relaxed),
        m_state->z.load(std::memory_order_relaxed),
    };
    const double radius = m_state->radius.load(std::memory_order_relaxed);
    const Vec3 color = m_color;

    PaneEntry entry;
    entry.view = view;
    entry.plane = plane;

    // Safe to capture &entry by reference: executeBlocking() does not return
    // until this lambda has fully run on the pane's worker thread, so entry
    // (a local on this thread's stack) is still alive when it's written.
    view->executeBlocking([&entry, leftDown, mouseMove, leftUp, position, radius, color](vtkGenericOpenGLRenderWindow* window) {
        vtkRenderer* renderer = window->GetRenderers()->GetFirstRenderer();
        if (!renderer)
            return;

        // this pane's own source, never shared across panes
        vtkNew<vtkSphereSource> source;
        source->SetThetaResolution(16);
        source->SetPhiResolution(16);
        source->SetCenter(position[0], position[1], position[2]);
        source->SetRadius(radius);

        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputConnection(source->GetOutputPort());

        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(color[0], color[1], color[2]);
        actor->GetProperty()->SetOpacity(0.85);

        renderer->AddActor(actor);

        entry.renderer = renderer;
        entry.actor = actor;  // kept alive by the renderer's own reference to it
        entry.source = source;

        if (vtkRenderWindowInteractor* interactor = window->GetInteractor()) {
            // Priority 3.0 — sphere events run before the RIV/viewport style (lower priority).
            interactor->AddObserver(vtkCommand::LeftButtonPressEvent, leftDown, 3.0f);
            interactor->AddObserver(vtkCommand::MouseMoveEvent, mouseMove, 3.0f);
            interactor->AddObserver(vtkCommand::LeftButtonReleaseEvent, leftUp, 3.0f);
            interactor->AddObserver(vtkCommand::LeaveEvent, leftUp, 3.0f);
        }
    });

    m_panes.push_back(entry);
}

void AsyncSphereController::Cleanup() {
    for (auto& pane : m_panes) {
        AsyncRenderView* view = pane.view;
        vtkRenderer* renderer = pane.renderer;
        vtkActor* actor = pane.actor;
        vtkSmartPointer<vtkCallbackCommand> leftDown = m_leftDownCmd;
        vtkSmartPointer<vtkCallbackCommand> mouseMove = m_mouseMoveCmd;
        vtkSmartPointer<vtkCallbackCommand> leftUp = m_leftUpCmd;
        if (!view)
            continue;

        // executeBlocking (not execute): callers of Cleanup() (notably
        // AsyncMultiWindowController::_RemoveSphere()) reset the controller
        // right after this returns, so the actor/observer removal must have
        // actually run on the worker thread before we get back to them.
        view->executeBlocking([renderer, actor, leftDown, mouseMove, leftUp](vtkGenericOpenGLRenderWindow* window) {
            if (renderer && actor)
                renderer->RemoveActor(actor);
            if (vtkRenderWindowInteractor* interactor = window->GetInteractor()) {
                interactor->RemoveObserver(leftDown);
                interactor->RemoveObserver(mouseMove);
                interactor->RemoveObserver(leftUp);
            }
        });
    }

    m_panes.clear();
    m_isDragging = false;
    m_activeRenderer = nullptr;
}

void AsyncSphereController::SetPosition(const Vec3& pos) {
    m_state->x.store(pos[0], std::memory_order_relaxed);
    m_state->y.store(pos[1], std::memory_order_relaxed);
    m_state->z.store(pos[2], std::memory_order_relaxed);

    for (size_t i = 0; i < m_panes.size() && i < m_state->positionInFlight.size(); ++i) {
        PaneEntry& pane = m_panes[i];
        if (!pane.view)
            continue;

        // skip if a dispatch is already in flight for this pane, it'll pick up the latest position
        if (m_state->positionInFlight[i].exchange(true, std::memory_order_acq_rel))
            continue;

        vtkSmartPointer<vtkSphereSource> source = pane.source;
        vtkActor* actor = pane.actor;
        std::shared_ptr<SphereState> state = m_state;  // not `this`: outlives the controller if still queued
        const size_t idx = i;
        pane.view->execute([state, source, actor, idx](vtkGenericOpenGLRenderWindow*) {
            // Loop rather than apply-once: if a newer position was written
            // (and its SetPosition() call saw positionInFlight still true,
            // so it skipped dispatching, trusting us to pick it up) after we
            // read `target` below but before we clear positionInFlight, that
            // write would otherwise never get applied. Re-check after
            // clearing the flag and, if the value moved on and nobody else
            // has claimed the slot, apply the latest value too.
            while (true) {
                const double tx = state->x.load(std::memory_order_relaxed);
                const double ty = state->y.load(std::memory_order_relaxed);
                const double tz = state->z.load(std::memory_order_relaxed);
                if (source) {
                    source->SetCenter(tx, ty, tz);
                    source->Update();
                }
                if (actor)
                    actor->Modified();

                state->positionInFlight[idx].store(false, std::memory_order_release);

                const double cx = state->x.load(std::memory_order_relaxed);
                const double cy = state->y.load(std::memory_order_relaxed);
                const double cz = state->z.load(std::memory_order_relaxed);
                if (cx == tx && cy == ty && cz == tz)
                    break;  // nothing changed while we were applying it
                if (state->positionInFlight[idx].exchange(true, std::memory_order_acq_rel))
                    break;  // someone else already claimed it and will dispatch
            }
        });
    }

    emit SphereMoved(pos);
}

void AsyncSphereController::SetRadius(double radius) {
    m_state->radius.store(radius, std::memory_order_relaxed);

    for (size_t i = 0; i < m_panes.size() && i < m_state->radiusInFlight.size(); ++i) {
        PaneEntry& pane = m_panes[i];
        if (!pane.view)
            continue;

        if (m_state->radiusInFlight[i].exchange(true, std::memory_order_acq_rel))
            continue;

        vtkSmartPointer<vtkSphereSource> source = pane.source;
        vtkActor* actor = pane.actor;
        std::shared_ptr<SphereState> state = m_state;  // not `this`: outlives the controller if still queued
        const size_t idx = i;
        pane.view->execute([state, source, actor, idx](vtkGenericOpenGLRenderWindow*) {
            // See SetPosition()'s lambda for why this loops instead of
            // applying once.
            while (true) {
                const double target = state->radius.load(std::memory_order_relaxed);
                if (source) {
                    source->SetRadius(target);
                    source->Update();
                }
                if (actor)
                    actor->Modified();

                state->radiusInFlight[idx].store(false, std::memory_order_release);

                if (state->radius.load(std::memory_order_relaxed) == target)
                    break;
                if (state->radiusInFlight[idx].exchange(true, std::memory_order_acq_rel))
                    break;
            }
        });
    }
}

void AsyncSphereController::SetColor(const Vec3& rgb) {
    m_color = rgb;

    for (auto& pane : m_panes) {
        vtkActor* actor = pane.actor;
        if (!pane.view)
            continue;
        pane.view->execute([actor, rgb](vtkGenericOpenGLRenderWindow*) {
            if (actor) {
                actor->GetProperty()->SetColor(rgb[0], rgb[1], rgb[2]);
                actor->Modified();
            }
        });
    }
}

double AsyncSphereController::GetRadius() const {
    return m_state->radius.load(std::memory_order_relaxed);
}

AsyncSphereController::Vec3 AsyncSphereController::GetPosition() const {
    return {
        m_state->x.load(std::memory_order_relaxed),
        m_state->y.load(std::memory_order_relaxed),
        m_state->z.load(std::memory_order_relaxed),
    };
}

AsyncSphereController::Vec3 AsyncSphereController::GetColor() const {
    return m_color;
}

bool AsyncSphereController::IsDragging() const {
    return m_isDragging;
}

vtkActor* AsyncSphereController::ActorFor(vtkRenderer* renderer) const {
    for (auto& entry : m_panes) {
        if (entry.renderer == renderer)
            return entry.actor;
    }
    return nullptr;
}

AsyncSphereController::DragPlane AsyncSphereController::PlaneFor(vtkRenderer* renderer) const {
    for (const auto& e : m_panes) {
        if (e.renderer == renderer)
            return e.plane;
    }
    return DragPlane::Axial;
}

void AsyncSphereController::OnLeftButtonDown(vtkObject* caller, unsigned long, void* clientData, void*) {
    auto* self = static_cast<AsyncSphereController*>(clientData);

    self->m_leftDownCmd->SetAbortFlag(0);

    auto* interactor = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!interactor)
        return;

    int pos[2];
    interactor->GetEventPosition(pos);

    vtkRenderer* renderer = interactor->FindPokedRenderer(pos[0], pos[1]);
    if (!renderer)
        return;

    self->m_picker->Pick(pos[0], pos[1], 0.0, renderer);
    vtkActor* pickedActor = self->m_picker->GetActor();

    auto* expectedActor = self->ActorFor(renderer);
    if (pickedActor != expectedActor)
        return;

    self->m_isDragging = true;
    self->m_activeRenderer = renderer;
    self->m_activePlane = self->PlaneFor(renderer);

    self->m_leftDownCmd->SetAbortFlag(1);
}

void AsyncSphereController::OnMouseMove(vtkObject* caller, unsigned long, void* clientData, void*) {
    auto* self = static_cast<AsyncSphereController*>(clientData);
    if (!self->m_isDragging || !self->m_activeRenderer)
        return;

    auto* interactor = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!interactor)
        return;

    int pos[2];
    interactor->GetEventPosition(pos);

    self->m_activeRenderer->SetDisplayPoint(pos[0], pos[1], 0.0);
    self->m_activeRenderer->DisplayToWorld();
    double near4[4];
    self->m_activeRenderer->GetWorldPoint(near4);

    self->m_activeRenderer->SetDisplayPoint(pos[0], pos[1], 1.0);
    self->m_activeRenderer->DisplayToWorld();
    double far4[4];
    self->m_activeRenderer->GetWorldPoint(far4);

    if (near4[3] == 0.0 || far4[3] == 0.0)
        return;

    const double rayNear[3] = {near4[0] / near4[3], near4[1] / near4[3], near4[2] / near4[3]};
    const double rayFar[3] = {far4[0] / far4[3], far4[1] / far4[3], far4[2] / far4[3]};

    double center[3] = {
        self->m_state->x.load(std::memory_order_relaxed),
        self->m_state->y.load(std::memory_order_relaxed),
        self->m_state->z.load(std::memory_order_relaxed),
    };

    double normal[3] = {0.0, 0.0, 0.0};
    switch (self->m_activePlane) {
        case DragPlane::Axial:
            normal[2] = 1.0;
            break;
        case DragPlane::Coronal:
            normal[1] = 1.0;
            break;
        case DragPlane::Sagittal:
            normal[0] = 1.0;
            break;
    }

    double t = 0.0;
    double intersect[3] = {0.0, 0.0, 0.0};
    if (!vtkPlane::IntersectWithLine(rayNear, rayFar, normal, center, t, intersect))
        return;

    self->SetPosition({intersect[0], intersect[1], intersect[2]});
}

void AsyncSphereController::OnLeftButtonUp(vtkObject*, unsigned long, void* clientData, void*) {
    auto* self = static_cast<AsyncSphereController*>(clientData);
    if (!self->m_isDragging)
        return;

    self->m_isDragging = false;
    self->m_activeRenderer = nullptr;
}

}  // namespace controllers

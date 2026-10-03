#include "controllers/AsyncSphereController.hpp"

#include <AsynQVTK/AsyncRenderView.hpp>
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
#include <vtkRendererCollection.h>
#include <vtkRenderer.h>
#include <vtkSphereSource.h>

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>

namespace controllers {

AsyncSphereController::AsyncSphereController(QObject* parent)
    : IControllerBase(parent) {
    m_sphereSource = vtkSmartPointer<vtkSphereSource>::New();
    m_sphereSource->SetThetaResolution(16);
    m_sphereSource->SetPhiResolution(16);
    m_sphereSource->SetRadius(3.0);

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

void AsyncSphereController::SetMutex(QMutex* mutex) {
    m_mutex = mutex;
}

void AsyncSphereController::AddPane(AsyncRenderView* view, DragPlane plane) {
    if (!view)
        return;

    for (const auto& p : m_panes) {
        if (p.view == view)
            return;
    }

    vtkSmartPointer<vtkSphereSource> source = m_sphereSource;
    vtkSmartPointer<vtkCallbackCommand> leftDown = m_leftDownCmd;
    vtkSmartPointer<vtkCallbackCommand> mouseMove = m_mouseMoveCmd;
    vtkSmartPointer<vtkCallbackCommand> leftUp = m_leftUpCmd;

    PaneEntry entry;
    entry.view = view;
    entry.plane = plane;

    // Safe to capture &entry by reference: executeBlocking() does not return
    // until this lambda has fully run on the pane's worker thread, so entry
    // (a local on this thread's stack) is still alive when it's written.
    view->executeBlocking([&entry, source, leftDown, mouseMove, leftUp](vtkGenericOpenGLRenderWindow* window) {
        vtkRenderer* renderer = window->GetRenderers()->GetFirstRenderer();
        if (!renderer)
            return;

        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputConnection(source->GetOutputPort());

        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(1.0, 0.3, 0.3);
        actor->GetProperty()->SetOpacity(0.85);

        renderer->AddActor(actor);

        entry.renderer = renderer;
        entry.actor = actor;  // kept alive by the renderer's own reference to it

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

        view->execute([renderer, actor, leftDown, mouseMove, leftUp](vtkGenericOpenGLRenderWindow* window) {
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
    if (m_mutex) {
        QMutexLocker lock(m_mutex);
        m_sphereSource->SetCenter(pos[0], pos[1], pos[2]);
        m_sphereSource->Update();
    } else {
        m_sphereSource->SetCenter(pos[0], pos[1], pos[2]);
        m_sphereSource->Update();
    }

    for (auto& pane : m_panes) {
        vtkActor* actor = pane.actor;
        if (!pane.view)
            continue;
        pane.view->execute([actor](vtkGenericOpenGLRenderWindow*) {
            if (actor)
                actor->Modified();
        });
    }

    emit SphereMoved(pos);
}

void AsyncSphereController::SetRadius(double radius) {
    if (m_mutex) {
        QMutexLocker lock(m_mutex);
        m_sphereSource->SetRadius(radius);
        m_sphereSource->Update();
    } else {
        m_sphereSource->SetRadius(radius);
        m_sphereSource->Update();
    }

    for (auto& pane : m_panes) {
        vtkActor* actor = pane.actor;
        if (!pane.view)
            continue;
        pane.view->execute([actor](vtkGenericOpenGLRenderWindow*) {
            if (actor)
                actor->Modified();
        });
    }
}

void AsyncSphereController::SetColor(const Vec3& rgb) {
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
    if (m_mutex) {
        QMutexLocker lock(m_mutex);
        return m_sphereSource->GetRadius();
    }
    return m_sphereSource->GetRadius();
}

AsyncSphereController::Vec3 AsyncSphereController::GetPosition() const {
    double c[3];
    if (m_mutex) {
        QMutexLocker lock(m_mutex);
        m_sphereSource->GetCenter(c);
    } else {
        m_sphereSource->GetCenter(c);
    }
    return {c[0], c[1], c[2]};
}

// Reads a pane-owned actor's property; acceptable relaxation (simple scalar
// read, not a mutation) — same tradeoff as CornerAnnotationOverlay (Task 2).
AsyncSphereController::Vec3 AsyncSphereController::GetColor() const {
    double rgb[3] = {1.0, 0.3, 0.3};
    if (!m_panes.empty() && m_panes.front().actor)
        m_panes.front().actor->GetProperty()->GetColor(rgb);
    return {rgb[0], rgb[1], rgb[2]};
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

    vtkActor* pickedActor = nullptr;
    if (self->m_mutex) {
        QMutexLocker lock(self->m_mutex);
        self->m_picker->Pick(pos[0], pos[1], 0.0, renderer);
        pickedActor = self->m_picker->GetActor();
    } else {
        self->m_picker->Pick(pos[0], pos[1], 0.0, renderer);
        pickedActor = self->m_picker->GetActor();
    }

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

    double center[3];
    if (self->m_mutex) {
        QMutexLocker lock(self->m_mutex);
        self->m_sphereSource->GetCenter(center);
    } else {
        self->m_sphereSource->GetCenter(center);
    }

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

#include <AsynQVTK/AsyncRenderWorker.hpp>

#include <QVTKInteractor.h>
#include <QVTKInteractorAdapter.h>
#include <vtkCallbackCommand.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkOpenGLState.h>

#include <QDebug>
#include <QEvent>
#include <QMutex>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QThread>
#include <algorithm>

namespace {
// NOTE: verified against VTK 8.2.0. Its OpenGL init path (GLEW etc.) touches
// global state, so two workers must not run initialize() concurrently.
// Revisit this if upgrading to a newer VTK that initializes GL state per-context.
QMutex s_glInitMutex;
}  // namespace

AsyncRenderWorker::AsyncRenderWorker(QOffscreenSurface* surface,
                                     QObject* parent)
    : QObject(parent), m_surface(surface) {
    // frameReady is queued across threads; moc looks up the type by this name.
    qRegisterMetaType<quintptr>("quintptr");
}

AsyncRenderWorker::~AsyncRenderWorker() { shutdown(); }

void AsyncRenderWorker::vtkContextCallback(vtkObject*, unsigned long eventId,
                                           void* clientData, void* callData) {
    auto* self = static_cast<AsyncRenderWorker*>(clientData);
    switch (eventId) {
        case vtkCommand::WindowMakeCurrentEvent:
            self->makeCurrent();
            break;
        case vtkCommand::WindowIsCurrentEvent:
            *static_cast<bool*>(callData) =
                QOpenGLContext::currentContext() == self->m_context.get();
            break;
    }
}

void AsyncRenderWorker::makeCurrent() { m_context->makeCurrent(m_surface); }

void AsyncRenderWorker::initialize() {
    m_context = std::make_unique<QOpenGLContext>();
    m_context->setFormat(m_surface->format());
    m_context->setShareContext(QOpenGLContext::globalShareContext());
    if (!m_context->create()) {
        qWarning() << "AsyncRenderWorker: OpenGL context could not be created";
        m_context.reset();
        return;
    }
    makeCurrent();

    qDebug() << "Render worker thread ID:" << QThread::currentThreadId();

    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_renderWindow->SetForceMaximumHardwareLineWidth(1);
    m_renderWindow->SetReadyForRendering(false);  // until the FBO is ready

    vtkNew<vtkCallbackCommand> contextCommand;
    contextCommand->SetClientData(this);
    contextCommand->SetCallback(&AsyncRenderWorker::vtkContextCallback);
    m_renderWindow->AddObserver(vtkCommand::WindowMakeCurrentEvent,
                                contextCommand);
    m_renderWindow->AddObserver(vtkCommand::WindowIsCurrentEvent,
                                contextCommand);

    // Interactor only updates the camera; render is triggered by us.
    // No default renderer or interactor style: the caller builds its own
    // scene and installs it via execute() once initialized() fires.
    m_interactor = vtkSmartPointer<QVTKInteractor>::New();
    m_interactor->SetRenderWindow(m_renderWindow);
    m_interactor->EnableRenderOff();
    m_interactor->Initialize();
    m_interactorAdapter = new QVTKInteractorAdapter(this);

    emit initialized();
}

void AsyncRenderWorker::shutdown() {
    if (!m_context) {
        return;
    }

    makeCurrent();
    if (m_renderFbo) {
        m_renderFbo->bind();
    }
    m_renderWindow->Finalize();
    m_renderWindow->SetReadyForRendering(false);

    auto* f = m_context->extraFunctions();
    for (Slot& slot : m_slots) {
        if (slot.releaseFence) {
            f->glDeleteSync(reinterpret_cast<GLsync>(slot.releaseFence));
        }
        delete slot.fbo;
        slot = Slot{};
    }
    delete m_renderFbo;
    m_renderFbo = nullptr;

    m_interactor = nullptr;
    m_renderWindow = nullptr;

    m_context->doneCurrent();
    m_context.reset();
}

void AsyncRenderWorker::resize(QSize deviceSize, qreal devicePixelRatio) {
    if (!m_context) {
        return;
    }
    m_size = deviceSize;
    m_interactorAdapter->SetDevicePixelRatio(devicePixelRatio);
    m_interactor->SetSize(m_size.width(), m_size.height());
    m_renderWindow->SetSize(m_size.width(), m_size.height());
    requestRender();
}

void AsyncRenderWorker::releaseFrame(int slot, quintptr releaseFence) {
    m_slots[slot].free = true;
    m_slots[slot].releaseFence = releaseFence;
    if (m_waitingForSlot) {
        m_waitingForSlot = false;
        requestRender();
    }
}

void AsyncRenderWorker::processEvent(std::shared_ptr<QEvent> event) {
    if (!m_context) {
        return;
    }
    m_interactorAdapter->ProcessEvent(event.get(), m_interactor);
    requestRender();
}

void AsyncRenderWorker::execute(std::function<void(vtkGenericOpenGLRenderWindow*)> fn) {
    if (!m_context || !fn) {
        return;
    }
    makeCurrent();
    fn(m_renderWindow);
    requestRender();
}

vtkGenericOpenGLRenderWindow* AsyncRenderWorker::renderWindow() const {
    return m_renderWindow;
}

QVTKInteractor* AsyncRenderWorker::interactor() const {
    return m_interactor;
}

void AsyncRenderWorker::requestRender() {
    // Coalesce multiple requests within the same event loop iteration into one render.
    if (m_renderQueued) {
        return;
    }
    m_renderQueued = true;
    QMetaObject::invokeMethod(this, &AsyncRenderWorker::render,
                              Qt::QueuedConnection);
}

void AsyncRenderWorker::ensureRenderTarget() {
    if (m_renderFbo && m_renderFbo->size() == m_size) {
        return;
    }
    delete m_renderFbo;

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::Depth);
    m_renderFbo = new QOpenGLFramebufferObject(m_size, format);
    m_renderFbo->bind();

    // VTK 8.2.0 records the currently bound FBO as its own default framebuffer;
    // this must happen with m_renderFbo bound, and before the first Render().
    QMutexLocker lock(&s_glInitMutex);
    m_renderWindow->SetSize(m_size.width(), m_size.height());
    m_renderWindow->SetReadyForRendering(true);
    m_renderWindow->InitializeFromCurrentContext();
}

void AsyncRenderWorker::render() {
    m_renderQueued = false;
    if (!m_context || m_size.isEmpty()) {
        return;
    }

    auto it = std::find_if(m_slots.begin(), m_slots.end(),
                           [](const Slot& s) { return s.free; });
    if (it == m_slots.end()) {
        // GUI is holding all three frames; retry once one comes back.
        m_waitingForSlot = true;
        return;
    }
    Slot& slot = *it;

    makeCurrent();
    auto* f = m_context->extraFunctions();

    // Don't overwrite this texture before the GUI finishes reading it (wait on the GPU side).
    if (slot.releaseFence) {
        auto sync = reinterpret_cast<GLsync>(slot.releaseFence);
        f->glWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
        f->glDeleteSync(sync);
        slot.releaseFence = 0;
    }
    if (!slot.fbo || slot.fbo->size() != m_size) {
        delete slot.fbo;
        slot.fbo = new QOpenGLFramebufferObject(m_size);
    }

    ensureRenderTarget();
    m_renderFbo->bind();
    m_renderWindow->Render();

    // Keep VTK's cached GL state in sync; scissor would otherwise clip the blit.
    vtkOpenGLState* state = m_renderWindow->GetState();
    state->ResetGlViewportState();
    state->vtkglDisable(GL_SCISSOR_TEST);
    QOpenGLFramebufferObject::blitFramebuffer(slot.fbo, m_renderFbo);
    m_renderFbo->bind();

    GLsync ready = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    f->glFlush();  // make the fence visible from the other context

    slot.free = false;
    emit frameReady(static_cast<int>(it - m_slots.begin()), slot.fbo->texture(),
                    reinterpret_cast<quintptr>(ready), m_size);
}

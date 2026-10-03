#pragma once

#include <vtkSmartPointer.h>

#include <QObject>
#include <QSize>
#include <array>
#include <functional>
#include <memory>

class QEvent;
class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QVTKInteractor;
class QVTKInteractorAdapter;
class vtkGenericOpenGLRenderWindow;
class vtkObject;

// Lives on its own thread and renders a caller-supplied VTK scene into its
// own offscreen GL context. The result is handed off as a texture shared
// with the GUI context.
//
// This class owns only the render window, interactor and GL plumbing. It has
// no opinion about what is rendered: callers build their own renderer(s),
// actors and interactor style and install them via execute(), which runs
// arbitrary code on the worker thread with the GL context current.
//
// renderWindow() and interactor() are only safe to call from code already
// running on the worker thread (i.e. from inside an execute() callback, or
// from another slot of this class) — never from the GUI thread directly.
// execute() also hands the render window to its callback directly, which
// covers most needs (window->GetInteractor(), window->GetRenderers(), ...).
//
// Frame flow (triple buffering):
//   worker: take a free slot -> wait on its release fence -> VTK render ->
//           blit into the slot's FBO -> ready fence -> frameReady(slot, texture, fence)
//   GUI:    in paintGL, wait on the ready fence -> draw the texture -> hand
//           the previous slot back via releaseFrame() with a new release fence
class AsyncRenderWorker : public QObject {
    Q_OBJECT

  public:
    // surface must be created on the GUI thread and must outlive the worker.
    explicit AsyncRenderWorker(QOffscreenSurface* surface,
                               QObject* parent = nullptr);
    ~AsyncRenderWorker() override;

    // Worker-thread-only accessors; valid once initialized() has fired.
    vtkGenericOpenGLRenderWindow* renderWindow() const;
    QVTKInteractor* interactor() const;

  public slots:
    // All of the following run on the worker thread.
    void initialize();
    void shutdown();
    void resize(QSize deviceSize, qreal devicePixelRatio);
    void releaseFrame(int slot, quintptr releaseFence);
    void processEvent(std::shared_ptr<QEvent> event);

    // Runs fn on the worker thread with the GL context current, passing it
    // the render window, then requests a render. Callers configure their
    // scene (renderers, actors, interactor style, observers, ...) through
    // this instead of dedicated methods.
    void execute(std::function<void(vtkGenericOpenGLRenderWindow*)> fn);

  signals:
    void frameReady(int slot, uint textureId, quintptr readyFence, QSize size);

    // Fired on the worker thread once the context, render window and
    // interactor exist and are safe to configure via execute().
    void initialized();

  private:
    struct Slot {
        QOpenGLFramebufferObject* fbo = nullptr;
        quintptr releaseFence = 0;
        bool free = true;
    };

    void requestRender();
    void render();
    void ensureRenderTarget();
    void makeCurrent();
    static void vtkContextCallback(vtkObject*, unsigned long eventId,
                                   void* clientData, void* callData);

    QOffscreenSurface* m_surface;
    std::unique_ptr<QOpenGLContext> m_context;

    // The fixed, depth-enabled FBO that VTK treats as its "default framebuffer".
    // Each frame is blitted from here into the current free slot's texture.
    QOpenGLFramebufferObject* m_renderFbo = nullptr;
    std::array<Slot, 3> m_slots;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<QVTKInteractor> m_interactor;
    QVTKInteractorAdapter* m_interactorAdapter = nullptr;

    QSize m_size;
    bool m_renderQueued = false;
    bool m_waitingForSlot = false;
};

#pragma once

#include <vtkSmartPointer.h>

#include <QObject>
#include <QSize>
#include <array>
#include <memory>

class QEvent;
class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QTimer;
class QVTKInteractor;
class QVTKInteractorAdapter;
class vtkGenericOpenGLRenderWindow;
class vtkObject;
class vtkRenderer;

// Lives on its own thread and renders the VTK scene into its own offscreen
// GL context. The result is handed off as a texture shared with the GUI
// context.
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

  public slots:
    // All of the following run on the worker thread.
    void initialize();
    void shutdown();
    void resize(QSize deviceSize, qreal devicePixelRatio);
    void releaseFrame(int slot, quintptr releaseFence);
    void processEvent(std::shared_ptr<QEvent> event);
    void addSphere();
    void setAnimating(bool animating);

  signals:
    void frameReady(int slot, uint textureId, quintptr readyFence, QSize size);

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
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<QVTKInteractor> m_interactor;
    QVTKInteractorAdapter* m_interactorAdapter = nullptr;
    QTimer* m_animationTimer = nullptr;

    QSize m_size;
    bool m_renderQueued = false;
    bool m_waitingForSlot = false;
};

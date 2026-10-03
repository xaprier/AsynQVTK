#pragma once

#include <QOffscreenSurface>
#include <QOpenGLTextureBlitter>
#include <QOpenGLWidget>
#include <QThread>
#include <functional>

class AsyncRenderWorker;
class vtkGenericOpenGLRenderWindow;

// GUI side: does not render itself, only blits the shared texture produced
// by the worker thread and forwards user input to it.
class AsyncRenderView : public QOpenGLWidget {
    Q_OBJECT

  public:
    explicit AsyncRenderView(QWidget* parent = nullptr);
    ~AsyncRenderView() override;

    // Runs fn on the worker thread (with its GL context current), passing
    // it the render window, and requests a render afterwards. This is how
    // callers build and mutate their VTK scene — renderers, actors,
    // interactor style, observers (window->GetInteractor()), and so on.
    // Queued: returns immediately, fn runs asynchronously.
    void execute(std::function<void(vtkGenericOpenGLRenderWindow*)> fn);

    // Same as execute(), but blocks the calling thread until fn has run.
    // Use for setup that must complete before the next line of GUI code
    // depends on it (e.g. initial pipeline construction).
    void executeBlocking(std::function<void(vtkGenericOpenGLRenderWindow*)> fn);

  signals:
    // Forwarded from the worker thread once its render window and
    // interactor exist and are safe to configure via execute().
    void ready();

  protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

  private:
    struct Frame {
        int slot = -1;
        uint texture = 0;
        quintptr fence = 0;
        bool valid() const { return slot >= 0; }
    };

    void onFrameReady(int slot, uint textureId, quintptr readyFence, QSize size);
    void releaseToWorker(const Frame& frame, quintptr releaseFence);
    void forwardEvent(QEvent* copy);

    QOffscreenSurface m_surface;
    QThread m_thread;
    AsyncRenderWorker* m_worker;

    QOpenGLTextureBlitter m_blitter;
    Frame m_pending;  // arrived but not yet drawn
    Frame m_current;  // currently on screen
};

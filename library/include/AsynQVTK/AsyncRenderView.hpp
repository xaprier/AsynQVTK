#pragma once

#include <QOffscreenSurface>
#include <QOpenGLTextureBlitter>
#include <QOpenGLWidget>
#include <QThread>

class AsyncRenderWorker;

// GUI side: does not render itself, only blits the shared texture produced
// by the worker thread and forwards user input to it.
class AsyncRenderView : public QOpenGLWidget {
    Q_OBJECT

  public:
    explicit AsyncRenderView(QWidget* parent = nullptr);
    ~AsyncRenderView() override;

    // Thread-safe: queued to the worker thread.
    void addSphere();
    void setAnimating(bool animating);

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

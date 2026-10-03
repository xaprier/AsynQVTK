#include "AsyncRenderView.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QWheelEvent>

#include "AsyncRenderWorker.hpp"

AsyncRenderView::AsyncRenderView(QWidget* parent)
    : QOpenGLWidget(parent),
      m_worker(new AsyncRenderWorker(&m_surface)) {
    setFocusPolicy(Qt::StrongFocus);

    // QOffscreenSurface must be created on the GUI thread; the worker only uses it.
    m_surface.setFormat(QSurfaceFormat::defaultFormat());
    m_surface.create();

    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_worker, &AsyncRenderWorker::initialize);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &AsyncRenderWorker::frameReady, this, &AsyncRenderView::onFrameReady);
    m_thread.start();
}

AsyncRenderView::~AsyncRenderView() {
    if (context()) {
        makeCurrent();
        m_blitter.destroy();
        doneCurrent();
    }

    // GL resources belong to the worker's thread and context; release them there.
    QMetaObject::invokeMethod(
        m_worker,
        &AsyncRenderWorker::shutdown,
        Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void AsyncRenderView::addSphere() {
    QMetaObject::invokeMethod(
        m_worker,
        &AsyncRenderWorker::addSphere,
        Qt::QueuedConnection);
}

void AsyncRenderView::setAnimating(bool animating) {
    AsyncRenderWorker* worker = m_worker;
    QMetaObject::invokeMethod(
        worker,
        [worker, animating]() { worker->setAnimating(animating); },
        Qt::QueuedConnection);
}

void AsyncRenderView::initializeGL() { m_blitter.create(); }

void AsyncRenderView::resizeGL(int, int) {
    const qreal dpr = devicePixelRatioF();
    const QSize deviceSize = size() * dpr;
    AsyncRenderWorker* worker = m_worker;
    QMetaObject::invokeMethod(
        worker,
        [worker, deviceSize, dpr]() { worker->resize(deviceSize, dpr); },
        Qt::QueuedConnection);
}

void AsyncRenderView::onFrameReady(int slot, uint textureId, quintptr readyFence, QSize) {
    if (m_pending.valid()) {
        // Previous frame was never drawn (worker outpaced the GUI). Since the GUI
        // never touched it, its own ready fence doubles as the release fence.
        releaseToWorker(m_pending, m_pending.fence);
    }
    m_pending = {slot, textureId, readyFence};
    update();
}

void AsyncRenderView::paintGL() {
    auto* f = context()->extraFunctions();

    Frame previous;
    if (m_pending.valid()) {
        // Wait on the GPU side for the worker's render to finish instead of
        // blocking the GUI thread.
        auto sync = reinterpret_cast<GLsync>(m_pending.fence);
        f->glWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
        f->glDeleteSync(sync);

        previous = m_current;
        m_current = m_pending;
        m_current.fence = 0;
        m_pending = Frame{};
    }

    f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT);

    if (m_current.valid()) {
        m_blitter.bind();
        m_blitter.blit(
            m_current.texture,
            QMatrix4x4(),
            QOpenGLTextureBlitter::OriginBottomLeft);
        m_blitter.release();
    }

    // VTK clears its background with alpha=0; since the window's framebuffer
    // has an alpha channel, the compositor would show the view as transparent.
    // Force the alpha channel back to 1 only.
    f->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    f->glClear(GL_COLOR_BUFFER_BIT);
    f->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    if (previous.valid()) {
        // The previous texture's last use is right before this point; the worker
        // will wait on this fence before writing over it again.
        GLsync done = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        f->glFlush();
        releaseToWorker(previous, reinterpret_cast<quintptr>(done));
    }
}

void AsyncRenderView::releaseToWorker(const Frame& frame,
                                      quintptr releaseFence) {
    AsyncRenderWorker* worker = m_worker;
    const int slot = frame.slot;
    QMetaObject::invokeMethod(
        worker,
        [worker, slot, releaseFence]() {
            worker->releaseFrame(slot, releaseFence);
        },
        Qt::QueuedConnection);
}

void AsyncRenderView::forwardEvent(QEvent* copy) {
    AsyncRenderWorker* worker = m_worker;
    std::shared_ptr<QEvent> event(copy);
    QMetaObject::invokeMethod(
        worker,
        [worker, event]() { worker->processEvent(event); },
        Qt::QueuedConnection);
}

// Qt events can't be queued directly (they're deleted after dispatch), so a
// copy is made before posting to the worker thread.
static QMouseEvent* copyMouseEvent(const QMouseEvent* e) {
    return new QMouseEvent(
        e->type(),
        e->localPos(),
        e->windowPos(),
        e->screenPos(),
        e->button(),
        e->buttons(),
        e->modifiers());
}

static QKeyEvent* copyKeyEvent(const QKeyEvent* e) {
    return new QKeyEvent(
        e->type(),
        e->key(),
        e->modifiers(),
        e->text(),
        e->isAutoRepeat(),
        static_cast<ushort>(e->count()));
}

void AsyncRenderView::mousePressEvent(QMouseEvent* event) {
    forwardEvent(copyMouseEvent(event));
}

void AsyncRenderView::mouseMoveEvent(QMouseEvent* event) {
    forwardEvent(copyMouseEvent(event));
}

void AsyncRenderView::mouseReleaseEvent(QMouseEvent* event) {
    forwardEvent(copyMouseEvent(event));
}

void AsyncRenderView::mouseDoubleClickEvent(QMouseEvent* event) {
    forwardEvent(copyMouseEvent(event));
}

void AsyncRenderView::wheelEvent(QWheelEvent* event) {
    forwardEvent(new QWheelEvent(event->position(), event->globalPosition(),
                                 event->pixelDelta(), event->angleDelta(),
                                 event->buttons(), event->modifiers(),
                                 event->phase(), event->inverted(),
                                 event->source()));
}

void AsyncRenderView::keyPressEvent(QKeyEvent* event) {
    forwardEvent(copyKeyEvent(event));
}

void AsyncRenderView::keyReleaseEvent(QKeyEvent* event) {
    forwardEvent(copyKeyEvent(event));
}

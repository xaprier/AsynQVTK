#include "controllers/AsyncResliceImageViewerInteractorStyle.hpp"

#include <AsynQVTK/AsyncRenderView.hpp>
#include <vtkImageData.h>
#include <vtkResliceCursorWidget.h>
#include <vtkResliceImageViewer.h>

namespace controllers {

void AsyncResliceImageViewerInteractorStyle::Execute(vtkObject* caller, unsigned long eventId, void*) {
    vtkResliceImageViewer* source = nullptr;

    if (auto* riv = vtkResliceImageViewer::SafeDownCast(caller)) {
        source = riv;
    } else if (auto* style = vtkInteractorObserver::SafeDownCast(caller)) {
        auto it = m_styleToViewer.find(style);

        if (it != m_styleToViewer.end())
            source = it->second;
    }

    if (!source)
        return;

    switch (eventId) {
        case vtkCommand::WindowLevelEvent:
        case vtkResliceCursorWidget::WindowLevelEvent: {
            _SyncWindowLevel(source);
            break;
        }

        default:
            break;
    }

    // No explicit render call here: the pane that owns `source` is already
    // mid interactor-dispatch on its own worker thread and will redraw via
    // its normal event-driven path (AsyncRenderWorker::processEvent()
    // already calls requestRender() after dispatching). _SyncWindowLevel()
    // dispatches execute() to the other two panes, which triggers their own
    // redraw as part of execute()'s auto-render.
}

void AsyncResliceImageViewerInteractorStyle::SetViewers(const std::vector<vtkSmartPointer<vtkResliceImageViewer>>& viewers) {
    m_viewers = viewers;
}

void AsyncResliceImageViewerInteractorStyle::SetViews(const std::vector<AsyncRenderView*>& views) {
    m_views = views;
}

void AsyncResliceImageViewerInteractorStyle::RegisterInteractorStyle(vtkInteractorObserver* style, vtkResliceImageViewer* viewer) {
    if (!style || !viewer)
        return;

    m_styleToViewer[style] = viewer;
}

void AsyncResliceImageViewerInteractorStyle::_SyncWindowLevel(vtkResliceImageViewer* source) {
    if (!source)
        return;

    const double ww = source->GetColorWindow();
    const double wl = source->GetColorLevel();

    for (size_t i = 0; i < m_viewers.size(); ++i) {
        vtkResliceImageViewer* viewer = m_viewers[i];
        if (!viewer || viewer == source)
            continue;
        if (i >= m_views.size() || !m_views[i])
            continue;

        vtkResliceImageViewer* target = viewer;
        m_views[i]->execute([target, ww, wl](vtkGenericOpenGLRenderWindow*) {
            if (target) {
                target->SetColorWindow(ww);
                target->SetColorLevel(wl);
            }
        });
    }
}

}  // namespace controllers

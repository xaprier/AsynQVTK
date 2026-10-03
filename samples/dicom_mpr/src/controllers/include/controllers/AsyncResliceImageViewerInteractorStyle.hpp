#ifndef ASYNCRESLICEIMAGEVIEWERINTERACTORSTYLE_HPP
#define ASYNCRESLICEIMAGEVIEWERINTERACTORSTYLE_HPP

#include <vtkCommand.h>
#include <vtkInteractorObserver.h>
#include <vtkResliceImageViewer.h>
#include <vtkSmartPointer.h>

#include <unordered_map>
#include <vector>

class AsyncRenderView;

namespace controllers {

/**
 * @brief Synchronises window/level settings across all vtkResliceImageViewer
 *        instances in AsynQVTK multiwindow mode — fork of
 *        ResliceImageViewerInteractorStyle, adapted so syncing the OTHER
 *        panes' RIVs runs on each of their own AsyncRenderView worker
 *        threads instead of directly.
 *
 * Registered as a vtkCommand observer on each viewer's interactor style.
 * When a WindowLevelEvent fires on any style, Execute() propagates the new
 * colour window and level to every other viewer so all slices stay visually
 * consistent. The pane where the drag originated is already running on its
 * own worker thread (via that pane's AsyncRenderWorker::processEvent()
 * dispatch), so it needs no execute() dispatch for its own render — only the
 * other two panes do.
 */
class AsyncResliceImageViewerInteractorStyle : public vtkCommand {
  public:
    static AsyncResliceImageViewerInteractorStyle* New() {
        return new AsyncResliceImageViewerInteractorStyle();
    }

    void Execute(vtkObject* caller, unsigned long eventId, void* callData) override;

    /**
     * @brief Sets the full list of viewers that should be kept in sync.
     *        Index-parallel with SetViews(). Call once, before any
     *        interaction events fire.
     */
    void SetViewers(const std::vector<vtkSmartPointer<vtkResliceImageViewer>>& viewers);

    /**
     * @brief Sets the AsyncRenderView owning each viewer in SetViewers(),
     *        index-parallel. Used to dispatch cross-pane sync via
     *        execute() instead of a render scheduler.
     */
    void SetViews(const std::vector<AsyncRenderView*>& views);

    /**
     * @brief Maps an interactor style to its associated viewer.
     *
     * Required so Execute() can resolve the source viewer when the caller is a
     * vtkInteractorObserver rather than a vtkResliceImageViewer directly.
     */
    void RegisterInteractorStyle(vtkInteractorObserver* style, vtkResliceImageViewer* viewer);

  private:
    /** @brief Copies the colour window and level from @p source to all other viewers, via execute(). */
    void _SyncWindowLevel(vtkResliceImageViewer* source);

    std::vector<vtkSmartPointer<vtkResliceImageViewer>> m_viewers;
    std::vector<AsyncRenderView*> m_views;

    std::unordered_map<vtkInteractorObserver*, vtkResliceImageViewer*> m_styleToViewer;
};

}  // namespace controllers

#endif  // ASYNCRESLICEIMAGEVIEWERINTERACTORSTYLE_HPP

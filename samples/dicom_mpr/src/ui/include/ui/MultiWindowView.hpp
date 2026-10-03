#ifndef MULTIWINDOWVIEW_HPP
#define MULTIWINDOWVIEW_HPP

#include <memory>
#include <vector>

#include "ui/ILayoutTarget.hpp"
#include "ui/IView.hpp"

class QGridLayout;
class AsyncRenderView;

namespace controllers {
class AsyncMultiWindowController;
}

namespace ui {

/**
 * @brief View widget for multi-window DICOM display, built on AsynQVTK.
 *
 * Creates three AsyncRenderView instances, each on its own worker thread.
 * ApplyLayout() rebuilds the QGridLayout geometry to match the selected
 * layout type (horizontal, vertical, 2+1, 1+2, singles).
 */
class MultiWindowView : public IView, public ILayoutTarget {
    Q_OBJECT
  public:
    explicit MultiWindowView(QWidget* parent = nullptr);
    ~MultiWindowView() override;

    /** @brief Returns the underlying controller (e.g. to connect signals or forward commands). */
    [[nodiscard]] controllers::AsyncMultiWindowController* GetController() const {
        return m_multiWindowController.get();
    }

    // ILayoutTarget
    void ApplyLayout(const ViewportLayoutDefinition& def) override;

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    void _setupUI() override;
    void _RebuildGrid(const ViewportLayoutDefinition& def);

    std::unique_ptr<controllers::AsyncMultiWindowController> m_multiWindowController{};
    std::vector<AsyncRenderView*> m_views;
    QGridLayout* m_grid{nullptr};
};

}  // namespace ui

#endif  // MULTIWINDOWVIEW_HPP

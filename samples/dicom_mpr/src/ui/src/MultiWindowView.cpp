#include "ui/MultiWindowView.hpp"

#include <AsynQVTK/AsyncRenderView.hpp>
#include <vtkResliceImageViewer.h>

#include <QGridLayout>
#include <QShowEvent>
#include <memory>

#include "controllers/AsyncMultiWindowController.hpp"
#include "controllers/AsyncSliceController.hpp"
#include "overlays/CornerAnnotationOverlay.hpp"
#include "overlays/FPSOverlay.hpp"
#include "overlays/OrientationMarkerOverlay.hpp"
#include "ui/ViewportLayoutTypes.hpp"

namespace ui {

MultiWindowView::MultiWindowView(QWidget* parent)
    : IView(parent),
      m_multiWindowController(std::make_unique<controllers::AsyncMultiWindowController>()) {
    _setupUI();
}

MultiWindowView::~MultiWindowView() = default;

void MultiWindowView::_setupUI() {
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(2);

    m_views.resize(3);
    m_fpsOverlays.resize(3);
    m_orientationMarkerOverlays.resize(3);

    for (int i = 0; i < 3; ++i) {
        m_views[i] = new AsyncRenderView(this);

        m_fpsOverlays[i] = new overlays::FPSOverlay(m_views[i]);
        m_orientationMarkerOverlays[i] = new overlays::OrientationMarkerOverlay(
            m_views[i], overlays::OrientationMarkerOverlay::kSliceOrientations[i]);
    }

    static const QString kViewNames[3] = {"Axial", "Coronal", "Sagittal"};
    m_cornerAnnotationOverlays.resize(3);
    for (int i = 0; i < 3; ++i)
        m_cornerAnnotationOverlays[i] = new overlays::CornerAnnotationOverlay(m_views[i], kViewNames[i]);

    m_multiWindowController->Initialize(m_views);
    connect(m_multiWindowController.get(), &controllers::AsyncMultiWindowController::StatusChanged,
            this, &MultiWindowView::StatusChanged);

    const auto& viewers = m_multiWindowController->GetSliceController()->GetViewers();
    for (int i = 0; i < static_cast<int>(m_cornerAnnotationOverlays.size()); ++i) {
        if (i < static_cast<int>(viewers.size()))
            m_cornerAnnotationOverlays[i]->SetViewer(viewers[i].Get());
    }

    // Start with horizontal split (default).
    _RebuildGrid(MakeLayoutDefinition(ViewportLayoutType::HorizontalSplit));
}

void MultiWindowView::showEvent(QShowEvent* event) {
    IView::showEvent(event);
    // The widget may have been hidden when image data was first loaded, causing
    // FitToView to compute with size [0,0]. Re-run it now that the windows are
    // actually on-screen and have valid pixel dimensions.
    if (!m_multiWindowController->IsInitialized())
        return;
    auto* sc = m_multiWindowController->GetSliceController();
    if (!sc)
        return;
    sc->FitToView();
}

void MultiWindowView::ApplyLayout(const ViewportLayoutDefinition& def) {
    _RebuildGrid(def);

    if (!m_multiWindowController->IsInitialized())
        return;

    auto* sc = m_multiWindowController->GetSliceController();
    if (!sc)
        return;

    // FitToView recomputes each plane's camera after widget resize, and
    // self-triggers a render per pane (see AsyncSliceController::FitToView).
    sc->FitToView();
}

void MultiWindowView::_RebuildGrid(const ViewportLayoutDefinition& def) {
    // Remove all widgets from the grid without deleting them.
    for (int i = 0; i < 3; ++i) {
        if (m_views[i]) {
            m_grid->removeWidget(m_views[i]);
            m_views[i]->hide();
        }
    }

    // Clear stretch factors.
    for (int r = 0; r < m_grid->rowCount(); ++r)
        m_grid->setRowStretch(r, 0);
    for (int c = 0; c < m_grid->columnCount(); ++c)
        m_grid->setColumnStretch(c, 0);

    const ViewportLayoutType type = def.type;

    switch (type) {
        case ViewportLayoutType::SingleAxial:
        case ViewportLayoutType::SingleCoronal:
        case ViewportLayoutType::SingleSagittal: {
            int plane = def.panes.empty() ? 0 : def.panes[0].planeIndex;
            if (plane < 0 || plane > 2) plane = 0;
            m_grid->addWidget(m_views[plane], 0, 0, 1, 1);
            m_views[plane]->show();
            m_grid->setRowStretch(0, 1);
            m_grid->setColumnStretch(0, 1);
            break;
        }

        case ViewportLayoutType::HorizontalSplit: {
            for (const auto& pane : def.panes) {
                const int plane = pane.planeIndex;
                const int col   = pane.slotIndex;
                if (plane < 0 || plane > 2 || col < 0 || col > 2) continue;
                m_grid->addWidget(m_views[plane], 0, col, 1, 1);
                m_views[plane]->show();
                m_grid->setColumnStretch(col, 1);
            }
            m_grid->setRowStretch(0, 1);
            break;
        }

        case ViewportLayoutType::VerticalSplit: {
            for (const auto& pane : def.panes) {
                const int plane  = pane.planeIndex;
                const int qtRow  = 2 - pane.slotIndex;
                if (plane < 0 || plane > 2 || qtRow < 0 || qtRow > 2) continue;
                m_grid->addWidget(m_views[plane], qtRow, 0, 1, 1);
                m_views[plane]->show();
                m_grid->setRowStretch(qtRow, 1);
            }
            m_grid->setColumnStretch(0, 1);
            break;
        }

        case ViewportLayoutType::TwoPlusOne: {
            for (const auto& pane : def.panes) {
                const int plane = pane.planeIndex;
                if (plane < 0 || plane > 2) continue;
                if (pane.slotIndex == 0) {
                    m_grid->addWidget(m_views[plane], 0, 0, 2, 1);
                } else if (pane.slotIndex == 1) {
                    m_grid->addWidget(m_views[plane], 0, 1, 1, 1);
                } else if (pane.slotIndex == 2) {
                    m_grid->addWidget(m_views[plane], 1, 1, 1, 1);
                }
                m_views[plane]->show();
            }
            m_grid->setColumnStretch(0, 1);
            m_grid->setColumnStretch(1, 1);
            m_grid->setRowStretch(0, 1);
            m_grid->setRowStretch(1, 1);
            break;
        }

        case ViewportLayoutType::TwoPlusOneReversed: {
            for (const auto& pane : def.panes) {
                const int plane = pane.planeIndex;
                if (plane < 0 || plane > 2) continue;
                if (pane.slotIndex == 0) {
                    m_grid->addWidget(m_views[plane], 0, 0, 1, 1);
                } else if (pane.slotIndex == 1) {
                    m_grid->addWidget(m_views[plane], 0, 1, 1, 1);
                } else if (pane.slotIndex == 2) {
                    m_grid->addWidget(m_views[plane], 1, 0, 1, 2);
                }
                m_views[plane]->show();
            }
            m_grid->setColumnStretch(0, 1);
            m_grid->setColumnStretch(1, 1);
            m_grid->setRowStretch(0, 1);
            m_grid->setRowStretch(1, 1);
            break;
        }
    }
}

}  // namespace ui

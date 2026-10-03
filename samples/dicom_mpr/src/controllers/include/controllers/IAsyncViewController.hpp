#ifndef IASYNCVIEWCONTROLLER_HPP
#define IASYNCVIEWCONTROLLER_HPP

#include <vtkImageData.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <array>
#include <vector>

#include "controllers/IControllerBase.hpp"

class AsyncRenderView;

namespace controllers {

/**
 * @brief Abstract base for AsynQVTK-backed view controllers that manage DICOM
 *        slice and sphere rendering across one AsyncRenderView per pane.
 *
 * Mirrors IViewController's shape and shared-instance broadcast mechanism
 * (see IViewController.hpp), generalized for AsyncRenderView-based
 * controllers so a future AsynQVTK port of viewport mode can implement this
 * same base rather than a bespoke one-off. See
 * docs/superpowers/specs/2026-10-03-dicom-mpr-asynqvtk-port-design.md.
 *
 * Unlike IViewController, this base holds no raw vtkRenderWindow /
 * vtkRenderWindowInteractor / vtkRenderer pointers: nothing outside a
 * pane's own worker thread may dereference VTK objects directly. Code that
 * needs them gets them from inside an AsyncRenderView::execute() /
 * executeBlocking() callback via the render window handed to it. There is
 * also no _Render() pure virtual — every mutation already triggers its own
 * render via execute()'s auto-render, so nothing needs a separate flush.
 */
class IAsyncViewController : public IControllerBase {
    Q_OBJECT

  public:
    explicit IAsyncViewController(QObject* parent = nullptr) : IControllerBase(parent) {
        m_instances.push_back(this);
    }

    ~IAsyncViewController() override {
        auto it = std::find(m_instances.begin(), m_instances.end(), this);
        if (it != m_instances.end()) {
            m_instances.erase(it);
        }
    }

    /**
     * @brief Initialises the controller with one AsyncRenderView per pane.
     *        No-op if already initialised.
     */
    void Initialize(const std::vector<AsyncRenderView*>& views) {
        if (!m_initialized) {
            _Initialize(views);
        }
    }

    /** @brief Returns true if Initialize() has been called successfully. */
    bool IsInitialized() const { return m_initialized; }

    /** @brief Adds a sphere actor to every registered controller instance. No-op if already added. */
    void AddSphere() {
        if (!m_sphereAdded) {
            for (auto* instance : m_instances) {
                instance->_AddSphere();
            }
        }
    }

    /** @brief Removes the sphere from every registered controller instance. */
    void RemoveSphere() {
        if (m_sphereAdded)
            for (auto* instance : m_instances)
                instance->_RemoveSphere();
    }

    /** @brief Returns true if a sphere actor is currently active. */
    bool IsSphereAdded() const { return m_sphereAdded; }

    /** @brief Toggles the sphere on or off across all instances. */
    void ToggleSphere() {
        if (m_sphereAdded) {
            RemoveSphere();
        } else {
            AddSphere();
        }
    }

    /** @brief Sets the sphere radius on all instances. No-op if @p radius is unchanged or <= 0. */
    void SetSphereRadius(double radius) {
        if (radius > 0 && radius != m_sphereRadius) {
            for (auto* instance : m_instances) {
                instance->_SetSphereRadius(radius);
            }
        }
    }

    /** @brief Returns the current sphere radius. */
    double GetSphereRadius() const { return m_sphereRadius; }

    /** @brief Sets the sphere RGB colour [0,1] on all instances. No-op if colour is unchanged or out of range. */
    void SetSphereColor(const std::array<double, 3> color) {
        if ((color[0] != m_sphereColor[0] || color[1] != m_sphereColor[1] || color[2] != m_sphereColor[2]) &&
            color[0] >= 0 && color[0] <= 1 && color[1] >= 0 && color[1] <= 1 && color[2] >= 0 && color[2] <= 1) {
            for (auto* instance : m_instances) {
                instance->_SetSphereColor(color);
            }
        }
    }

    /** @brief Returns the current sphere colour components. */
    void GetSphereColor(double& r, double& g, double& b) const {
        r = m_sphereColor[0];
        g = m_sphereColor[1];
        b = m_sphereColor[2];
    }

  Q_SIGNALS:
    /**
     * @brief Emitted once all panes' vtkResliceImageViewer instances are
     *        created and ready. Concrete subclasses emit this at the end of
     *        _Initialize(). Connect to this before relying on a concrete
     *        subclass's own typed slice-controller accessor.
     */
    void ViewersReady();

  public slots:
    /** @brief Pushes @p data into every registered controller instance. No-op if @p data is null. */
    void SetImageData(vtkImageData* data) {
        if (data) {
            for (auto* instance : m_instances) {
                instance->_SetImageData(data);
            }
        }
    }

  protected:
    virtual void _Initialize(const std::vector<AsyncRenderView*>& views) = 0;
    virtual void _AddSphere() = 0;
    virtual void _RemoveSphere() = 0;
    virtual void _SetSphereRadius(double radius) = 0;
    virtual void _SetSphereColor(const std::array<double, 3> color) = 0;
    virtual void _SetImageData(vtkImageData* data) = 0;
    virtual void _SetupPipeline(vtkImageData* imageData) = 0;

    bool m_initialized{false};
    bool m_sphereAdded{false};
    bool m_dicomLoaded{false};
    double m_sphereRadius{3.0};
    std::array<double, 3> m_sphereColor{1.0, 0.3, 0.3};
    vtkSmartPointer<vtkImageData> m_imageData;
    std::vector<AsyncRenderView*> m_views;

  private:
    static std::vector<IAsyncViewController*> m_instances;
};

}  // namespace controllers

#endif  // IASYNCVIEWCONTROLLER_HPP

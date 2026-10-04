# AsynQVTK

<p align="center">
  <a href="https://github.com/xaprier/AsynQVTK/blob/main/LICENSE" target="blank">
    <img src="https://img.shields.io/github/license/xaprier/AsynQVTK" alt="license" />
  </a>
</p>

A Qt library for rendering VTK scenes off the GUI thread, one worker thread
per render view. Includes two sample apps: a minimal usage demo, and a
full DICOM MPR viewer that runs the same three-plane layout two different
ways side by side, so the difference between synchronous and async
rendering is visible rather than theoretical.

---

## Why

`vtkRenderWindow::Render()` runs on whatever thread calls it. In a normal
Qt app that's the GUI thread, so a heavy scene (large volumes, many
actors, reslice pipelines) blocks input and repaint while it renders.
AsynQVTK moves that work onto its own thread per view, so multiple views
render independently and the GUI thread is never blocked by VTK.

## Library (AsynQVTK)

- `AsyncRenderView` — `QOpenGLWidget` on the GUI thread; blits the
  worker's finished frame, forwards mouse/keyboard events to it
- `AsyncRenderWorker` — owns the `vtkGenericOpenGLRenderWindow`,
  interactor, and GL context on its own thread
- `execute()` / `executeBlocking()` — run arbitrary code on the worker
  thread (async or blocking), scene setup included; the library has no
  opinion about what you render
- Triple-buffered frame hand-off between worker and GUI thread with GPU
  fences, no GUI-thread stall waiting on the worker

```cpp
AsyncRenderView* view = new AsyncRenderView(parent);

view->executeBlocking([](vtkGenericOpenGLRenderWindow* window) {
    vtkNew<vtkRenderer> renderer;
    window->AddRenderer(renderer);
});

view->execute([](vtkGenericOpenGLRenderWindow* window) {
    // mutate the scene; a render is requested automatically afterward
});
```

`renderWindow()`/`interactor()` on the worker are only valid from inside
an `execute()`/`executeBlocking()` callback, never from the GUI thread
directly.

---

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                           GUI thread                           │
│                                                                │
│   AsyncRenderView : QOpenGLWidget                              │
│       - blits the latest finished texture                      │
│       - forwards QMouseEvent/QKeyEvent copies to the worker    │
│       - execute()/executeBlocking() post work to the worker    │
└───────────────────────────┬────────────────────────────────────┘
       Qt::QueuedConnection / BlockingQueuedConnection
                            ▼
┌────────────────────────────────────────────────────────────────┐
│                   one AsyncRenderWorker thread                 │
│                                                                │
│   vtkGenericOpenGLRenderWindow, vtkRenderWindowInteractor,     │
│   its own GL context (sharing the GUI context's texture group) │
│   render() → blit into a free triple-buffer slot → fence       │
└────────────────────────────────────────────────────────────────┘
```

Each `AsyncRenderView` gets its own worker thread and GL context. Nothing
about the scene (renderer, actors, interactor style) is shared between
views unless you share it yourself — and if you do, you own the
synchronization (see the dicom_mpr sample for what that costs).

---

## Samples

### sphere_grid

3x3 grid of independent `AsyncRenderView`s, each with its own spinning
camera and a button to drop spheres into all of them. The minimal usage
example for the library.

### dicom_mpr

A multi-planar reslice (MPR) DICOM viewer, forked from
[XQMprViewer](https://github.com/xaprier/XQMprViewer). It shows axial,
coronal, and sagittal planes with a draggable sphere annotation
synchronized across all three, and has two tabs rendering the same data
two different ways:

- **Viewport Mode** — one `vtkRenderWindow` split into three
  `vtkRenderer` viewports via `XQVtkViewport::ViewportManager`, rendered
  synchronously on the GUI thread. This is XQMprViewer's original
  controller/scheduler stack, unmodified.
- **MultiWindow Mode** — three `AsyncRenderView`s, one worker thread per
  plane. Rebuilt on this library (`Async*`-prefixed controllers in
  `controllers/`) as part of this repo; the original synchronous
  multi-window controllers are still present but unused by the running
  app, kept only until this gets ported the same way.

Both tabs load the same series and stay in sync, so switching between
them shows the actual difference: Viewport Mode serializes all three
planes through one `Render()` call per frame; MultiWindow Mode renders
them concurrently on three threads. Dragging the sphere fast enough shows
it directly — viewport mode has to finish one plane's render before
starting the next, multiwindow mode doesn't.

---

## Tech Stack

| Component | Version                                                                              |
| --------- | ------------------------------------------------------------------------------------ |
| C++       | 20 (library), 17 (dicom_mpr)                                                         |
| CMake     | >= 3.5 (root), >= 3.20 (dicom_mpr)                                                   |
| Qt        | 5.15                                                                                 |
| VTK       | 8.2.0 — later versions changed the render window / interactor APIs this library uses |

---

## Project Structure

```
AsynQVTK/
├── CMakeLists.txt                  Root build: library + both samples
│
├── library/                        AsynQVTK — the async render library
│   ├── CMakeLists.txt
│   ├── include/AsynQVTK/
│   │   ├── AsyncRenderView.hpp     GUI-thread widget: blit, input, execute()
│   │   └── AsyncRenderWorker.hpp   Worker-thread owner of window/interactor/GL
│   └── src/
│       ├── AsyncRenderView.cpp
│       └── AsyncRenderWorker.cpp
│
└── samples/
    ├── sphere_grid/
    │   ├── CMakeLists.txt
    │   └── main.cpp                 3x3 AsyncRenderView grid, minimal demo
    │
    └── dicom_mpr/                   DICOM MPR viewer (forked from XQMprViewer)
        ├── CMakeLists.txt
        │
        ├── lib/                     XQVtkViewport — single-window multi-viewport lib
        │   ├── CMakeLists.txt
        │   ├── include/XQVtkViewport/
        │   │   ├── IViewportObserver.hpp
        │   │   ├── RenderStats.hpp
        │   │   ├── RenderStatsOverlay.hpp
        │   │   ├── ViewportConfig.hpp
        │   │   ├── ViewportLayout.hpp
        │   │   └── ViewportManager.hpp
        │   └── src/
        │       ├── RenderStats.cpp
        │       ├── RenderStatsOverlay.cpp
        │       ├── ViewportLayout.cpp
        │       └── ViewportManager.cpp
        │
        └── src/                     Demo application
            ├── CMakeLists.txt
            │
            ├── app/src/main.cpp     Entry point: QApplication + MainWindow
            │
            ├── adapters/            Data / layout bridge layer
            │   └── include+src/     ColorAdapter, DicomMetaDataAdapter, OverlayLayoutAdapter
            │
            ├── controllers/         Interaction logic and render coordination
            │   ├── IViewController / MultiWindowController / SliceController /
            │   │   SphereController / ResliceImageViewerInteractorStyle /
            │   │   ViewportController / ViewportInteractorStyle
            │   │       — original XQMprViewer stack, drives Viewport Mode
            │   │         and the (now unused) sync MultiWindow Mode
            │   │
            │   └── IAsyncViewController / AsyncMultiWindowController /
            │       AsyncSliceController / AsyncSphereController /
            │       AsyncResliceImageViewerInteractorStyle
            │           — AsynQVTK-backed fork, drives MultiWindow Mode.
            │             Each VTK mutation runs via execute()/
            │             executeBlocking() on that pane's own worker thread;
            │             the sphere actor is per-pane (no VTK object shared
            │             across threads) after an earlier shared-source
            │             version raced under ThreadSanitizer+ASan
            │
            ├── overlays/             Per-viewport overlays
            │   └── IOverlay / FPSOverlay / CornerAnnotationOverlay /
            │       OrientationMarkerOverlay
            │
            ├── render/               Viewport Mode's render scheduler
            │   └── IRenderTarget / RenderScheduler / RivRenderTarget /
            │       WindowRenderTarget — RequestRender()/Flush() dedup,
            │       unused by MultiWindow Mode (execute() self-renders)
            │
            └── ui/                   Qt Widgets layer
                ├── MainWindow / ControllerPanel(+Item widgets) /
                │   DicomMetaDataModel / DicomMetaDataPanel
                ├── ViewportView            Viewport Mode tab
                ├── MultiWindowView         MultiWindow Mode tab (AsyncRenderView-based)
                └── ViewportLayoutManager / ViewportLayoutSelector /
                    ViewportLayoutItem / ViewportLayoutTypes
```

---

## Build

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

Builds `AsynQVTK`, `sphere_grid`, and `AsynQtMprViewer` (the dicom_mpr
binary — the CMake target name doesn't match the sample's directory name).

## License

MIT, see [LICENSE](LICENSE).

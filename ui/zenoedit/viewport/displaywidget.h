#ifndef __DISPLAY_WIDGET_H__
#define __DISPLAY_WIDGET_H__

#include <QtWidgets>
#include "common.h"
#include "recordvideomgr.h"
#include "viewportinteraction/picker.h"
#include "launch/corelaunch.h"
#include "dock/docktabcontent.h"

class ViewportWidget;
#ifdef ZENO_OPTIX_PROC
class ZOptixProcViewport;
#else
class ZOptixViewport;
#endif
class CameraKeyframeWidget;

class DisplayWidget : public QWidget
{
    Q_OBJECT
public:
    DisplayWidget(bool bGLView, QWidget* parent = nullptr);
    ~DisplayWidget();
    void init();
    QSize sizeHint() const override;
    Zenovis* getZenoVis() const;
    void runAndRecord(const VideoRecInfo& info);
    void testCleanUp();
    void beforeRun();
    void afterRun();
    void changeTransformOperation(const QString &node);
    void changeTransformOperation(int mode);
    QSize viewportSize() const;
    void resizeViewport(QSize sz);
    std::shared_ptr<zeno::Picker> picker() const;
    void updateCameraProp(float aperture, float disPlane);
    void updatePerspective();
    void setNumSamples(int samples);
    void setSafeFrames(bool bLock, int nx, int ny);
    void setCameraRes(const QVector2D& res);
    void setSimpleRenderOption();
    void setRenderSeparately(bool updateLightCameraOnly, bool updateMatlOnly);
    bool isCameraMoving() const;
    bool isPlaying() const;
    bool isGLViewport() const;
    void setViewWidgetInfo(DockContentWidgetInfo& info);
#ifdef ZENO_OPTIX_PROC
    ZOptixProcViewport* optixViewport() const;
#else
    ZOptixViewport* optixViewport() const;
#endif
    void killOptix();
    void moveToFrame(int frame);
    void setIsCurrent(bool isCurrent);
    bool isCurrent();
    void setLoopPlaying(bool enable);
protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
public slots:
    void updateFrame(const QString& action = "");
    void onRun();
    void onRun(LAUNCH_PARAM launchParam);
    void onRecord();
    void onRecord_slient(const VideoRecInfo& recInfo);
    bool onRecord_cmd(const VideoRecInfo& recInfo);
    void onScreenShoot();
    void onKill();
    void onPlayClicked(bool);
    void onSliderValueChanged(int);
    void onRunFinished();
    void onCommandDispatched(int actionType, bool bTriggered);
    void onNodeSelected(const QModelIndex& subgIdx, const QModelIndexList& nodes, bool select);
    void onMouseHoverMoved();

signals:
    void frameUpdated(int new_frame);
    void frameRunFinished(int frame);
    void optixProcStartRecord();

private:
    bool isOptxRendering() const;
    void initRecordMgr();

    ViewportWidget* m_glView;
#ifdef ZENO_OPTIX_PROC
    ZOptixProcViewport* m_optixView;
#else
    ZOptixViewport* m_optixView;
#endif
    CameraKeyframeWidget* m_camera_keyframe;
    QTimer* m_pTimer;
    RecordVideoMgr m_recordMgr;
    bool m_bRecordRun;
    const bool m_bGLView;
    static const int m_sliderFeq = 16;
    bool bIsCurrent = false;
};

#endif
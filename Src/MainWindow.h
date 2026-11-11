#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>
#include <QStringList>

class AudioTrack;
class ViewerWidget;
class QAction;
class QComboBox;
class QLabel;
class QListWidget;
class QMenu;
class QSlider;
class QTimer;

// 主窗口：菜单、工具栏、时间轴、表情面板与播放控制
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* Parent = nullptr);
    ~MainWindow() override;

    // 根据扩展名（及 VMD 内容）自动判断文件类型并打开
    bool OpenFile(const QString& FilePath);
    // 批量打开（拖放或命令行）：自动区分角色、舞台、主动作与追加动作
    void OpenFiles(const QStringList& Files);
    bool OpenModel(const QString& FilePath);
    bool OpenMotion(const QString& FilePath);
    bool AppendMotion(const QString& FilePath);
    bool OpenStage(const QString& FilePath);
    bool OpenCameraMotion(const QString& FilePath);
    bool OpenAudio(const QString& FilePath);

    void Play();
    void Pause();
    void Stop();
    void SeekToFrame(float Frame);
    ViewerWidget* Viewer() const { return ViewerPtr; }

protected:
    void dragEnterEvent(QDragEnterEvent* Event) override;
    void dropEvent(QDropEvent* Event) override;

private:
    void CreateActions();
    void CreateMenus();
    void CreateToolBars();
    void CreateDocks();
    void OnTick();
    void TogglePlay();
    void UpdateTimelineRange();
    void UpdateFrameLabel();
    void UpdateWindowTitle();
    void RefreshMorphList();
    void RefreshModelInfo();
    void OnMorphSelected();
    void OnMorphSliderChanged(int Value);
    void AddRecentFile(const QString& FilePath);
    void UpdateRecentMenu();
    void ShowAbout();
    void ShowError(const QString& Title, const QString& Message);
    float TotalFrames() const;
    void OpenModelDialog();
    void OpenMotionDialog();
    void AppendMotionDialog();
    void OpenStageDialog();
    void OpenCameraDialog();
    void OpenAudioDialog();

    ViewerWidget* ViewerPtr;
    AudioTrack* Audio;
    QTimer* TickTimer;
    QElapsedTimer Clock;
    qint64 LastTickMs;
    bool Playing;
    float Frame;
    float PlaybackSpeed;
    bool SliderUpdating;
    QString ModelPath;
    QString MotionPath;
    QString CameraPath;
    QString StagePath;
    QStringList ExtraMotionPaths;

    QAction* OpenModelAction;
    QAction* OpenMotionAction;
    QAction* AppendMotionAction;
    QAction* OpenStageAction;
    QAction* ClearStageAction;
    QAction* OpenCameraAction;
    QAction* OpenAudioAction;
    QAction* ClearMotionAction;
    QAction* ClearCameraAction;
    QAction* ClearAudioAction;
    QAction* ExitAction;
    QAction* PlayAction;
    QAction* StopAction;
    QAction* LoopAction;
    QAction* PhysicsAction;
    QAction* EdgeAction;
    QAction* GridAction;
    QAction* ShadowAction;
    QAction* CameraFollowAction;
    QAction* ResetCameraAction;
    QAction* AboutAction;
    QMenu* RecentMenu;

    QSlider* TimelineSlider;
    QLabel* FrameLabel;
    QComboBox* SpeedBox;
    QSlider* VolumeSlider;
    QListWidget* MorphList;
    QSlider* MorphSlider;
    QLabel* MorphValueLabel;
    QLabel* InfoLabel;
};

#endif

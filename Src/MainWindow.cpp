#include "MainWindow.h"

#include "AudioTrack.h"
#include "ViewerWidget.h"

#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
const float MotionFps = 30.0f; // MMD 动作固定为每秒 30 帧
const int MaxRecentFiles = 10;

bool IsAudioFile(const QString& Suffix)
{
    static const QStringList AudioSuffixes = {
        QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("ogg"),
        QStringLiteral("m4a"), QStringLiteral("flac"), QStringLiteral("aac"),
    };
    return AudioSuffixes.contains(Suffix);
}
}

MainWindow::MainWindow(QWidget* Parent)
    : QMainWindow(Parent)
    , LastTickMs(0)
    , Playing(false)
    , Frame(0.0f)
    , PlaybackSpeed(1.0f)
    , SliderUpdating(false)
{
    ViewerPtr = new ViewerWidget(this);
    setCentralWidget(ViewerPtr);
    Audio = new AudioTrack(this);
    connect(Audio, &AudioTrack::DurationChanged, this, [this](double) { UpdateTimelineRange(); });

    CreateActions();
    CreateMenus();
    CreateToolBars();
    CreateDocks();
    UpdateRecentMenu();
    UpdateWindowTitle();
    setAcceptDrops(true);
    resize(1280, 800);
    statusBar()->showMessage(QStringLiteral("就绪。请通过“文件”菜单打开 PMX 模型，或直接把文件拖到窗口中。"));

    TickTimer = new QTimer(this);
    TickTimer->setTimerType(Qt::PreciseTimer);
    connect(TickTimer, &QTimer::timeout, this, &MainWindow::OnTick);
    Clock.start();
    LastTickMs = Clock.elapsed();
    TickTimer->start(16);
}

MainWindow::~MainWindow()
{
}

// ======================== 界面构建 ========================

void MainWindow::CreateActions()
{
    OpenModelAction = new QAction(QStringLiteral("打开模型(&M)..."), this);
    OpenModelAction->setShortcut(QKeySequence::Open);
    connect(OpenModelAction, &QAction::triggered, this, &MainWindow::OpenModelDialog);

    OpenMotionAction = new QAction(QStringLiteral("打开动作(&V)..."), this);
    OpenMotionAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(OpenMotionAction, &QAction::triggered, this, &MainWindow::OpenMotionDialog);

    AppendMotionAction = new QAction(QStringLiteral("追加动作（合并口型/表情等）..."), this);
    AppendMotionAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    connect(AppendMotionAction, &QAction::triggered, this, &MainWindow::AppendMotionDialog);

    OpenStageAction = new QAction(QStringLiteral("打开舞台(&S)..."), this);
    OpenStageAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    connect(OpenStageAction, &QAction::triggered, this, &MainWindow::OpenStageDialog);

    ClearStageAction = new QAction(QStringLiteral("卸载舞台"), this);
    connect(ClearStageAction, &QAction::triggered, this, [this]() {
        ViewerPtr->ClearStage();
        StagePath.clear();
        RefreshModelInfo();
        statusBar()->showMessage(QStringLiteral("已卸载舞台。"), 3000);
    });

    OpenCameraAction = new QAction(QStringLiteral("打开镜头动作(&C)..."), this);
    OpenCameraAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    connect(OpenCameraAction, &QAction::triggered, this, &MainWindow::OpenCameraDialog);

    OpenAudioAction = new QAction(QStringLiteral("打开音频(&A)..."), this);
    OpenAudioAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+U")));
    connect(OpenAudioAction, &QAction::triggered, this, &MainWindow::OpenAudioDialog);
    if (!AudioTrack::IsSupported()) {
        OpenAudioAction->setEnabled(false);
        OpenAudioAction->setToolTip(QStringLiteral("编译时未启用 Qt Multimedia，音频不可用"));
    }

    ClearMotionAction = new QAction(QStringLiteral("卸载动作"), this);
    connect(ClearMotionAction, &QAction::triggered, this, [this]() {
        ViewerPtr->ClearMotion();
        MotionPath.clear();
        ExtraMotionPaths.clear();
        RefreshModelInfo();
        UpdateTimelineRange();
        statusBar()->showMessage(QStringLiteral("已卸载动作。"), 3000);
    });
    ClearCameraAction = new QAction(QStringLiteral("卸载镜头动作"), this);
    connect(ClearCameraAction, &QAction::triggered, this, [this]() {
        ViewerPtr->ClearCameraMotion();
        CameraPath.clear();
        UpdateTimelineRange();
        statusBar()->showMessage(QStringLiteral("已卸载镜头动作。"), 3000);
    });
    ClearAudioAction = new QAction(QStringLiteral("卸载音频"), this);
    connect(ClearAudioAction, &QAction::triggered, this, [this]() {
        Audio->Unload();
        UpdateTimelineRange();
        statusBar()->showMessage(QStringLiteral("已卸载音频。"), 3000);
    });

    ExitAction = new QAction(QStringLiteral("退出(&X)"), this);
    ExitAction->setShortcut(QKeySequence::Quit);
    connect(ExitAction, &QAction::triggered, this, &QWidget::close);

    PlayAction = new QAction(QStringLiteral("播放"), this);
    PlayAction->setShortcut(QKeySequence(Qt::Key_Space));
    connect(PlayAction, &QAction::triggered, this, &MainWindow::TogglePlay);

    StopAction = new QAction(QStringLiteral("停止"), this);
    StopAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(StopAction, &QAction::triggered, this, &MainWindow::Stop);

    LoopAction = new QAction(QStringLiteral("循环播放"), this);
    LoopAction->setCheckable(true);
    LoopAction->setChecked(true);

    PhysicsAction = new QAction(QStringLiteral("物理模拟"), this);
    PhysicsAction->setCheckable(true);
    PhysicsAction->setChecked(true);
    connect(PhysicsAction, &QAction::toggled, this, [this](bool On) {
        ViewerPtr->SetPhysicsEnabled(On);
        statusBar()->showMessage(On ? QStringLiteral("物理模拟：开") : QStringLiteral("物理模拟：关"), 2000);
    });

    EdgeAction = new QAction(QStringLiteral("描边"), this);
    EdgeAction->setCheckable(true);
    EdgeAction->setChecked(true);
    connect(EdgeAction, &QAction::toggled, ViewerPtr, &ViewerWidget::SetEdgeEnabled);

    GridAction = new QAction(QStringLiteral("地面网格"), this);
    GridAction->setCheckable(true);
    GridAction->setChecked(true);
    connect(GridAction, &QAction::toggled, ViewerPtr, &ViewerWidget::SetGridEnabled);

    ShadowAction = new QAction(QStringLiteral("阴影"), this);
    ShadowAction->setCheckable(true);
    ShadowAction->setChecked(true);
    connect(ShadowAction, &QAction::toggled, ViewerPtr, &ViewerWidget::SetShadowEnabled);

    CameraFollowAction = new QAction(QStringLiteral("使用镜头动作"), this);
    CameraFollowAction->setCheckable(true);
    CameraFollowAction->setChecked(true);
    connect(CameraFollowAction, &QAction::toggled, ViewerPtr, &ViewerWidget::SetCameraMotionEnabled);

    ResetCameraAction = new QAction(QStringLiteral("重置视角"), this);
    ResetCameraAction->setShortcut(QKeySequence(QStringLiteral("R")));
    connect(ResetCameraAction, &QAction::triggered, ViewerPtr, &ViewerWidget::ResetCamera);

    AboutAction = new QAction(QStringLiteral("关于(&A)"), this);
    connect(AboutAction, &QAction::triggered, this, &MainWindow::ShowAbout);
}

void MainWindow::CreateMenus()
{
    QMenu* FileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    FileMenu->addAction(OpenModelAction);
    FileMenu->addAction(OpenStageAction);
    FileMenu->addAction(OpenMotionAction);
    FileMenu->addAction(AppendMotionAction);
    FileMenu->addAction(OpenCameraAction);
    FileMenu->addAction(OpenAudioAction);
    FileMenu->addSeparator();
    FileMenu->addAction(ClearMotionAction);
    FileMenu->addAction(ClearCameraAction);
    FileMenu->addAction(ClearAudioAction);
    FileMenu->addAction(ClearStageAction);
    FileMenu->addSeparator();
    RecentMenu = FileMenu->addMenu(QStringLiteral("最近打开的文件"));
    FileMenu->addSeparator();
    FileMenu->addAction(ExitAction);

    QMenu* PlayMenu = menuBar()->addMenu(QStringLiteral("播放(&P)"));
    PlayMenu->addAction(PlayAction);
    PlayMenu->addAction(StopAction);
    PlayMenu->addAction(LoopAction);

    QMenu* ViewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    ViewMenu->addAction(PhysicsAction);
    ViewMenu->addAction(EdgeAction);
    ViewMenu->addAction(GridAction);
    ViewMenu->addAction(ShadowAction);
    ViewMenu->addAction(CameraFollowAction);
    ViewMenu->addSeparator();
    ViewMenu->addAction(ResetCameraAction);

    QMenu* HelpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    HelpMenu->addAction(AboutAction);
}

void MainWindow::CreateToolBars()
{
    QToolBar* FileBar = addToolBar(QStringLiteral("文件"));
    FileBar->setObjectName(QStringLiteral("FileToolBar"));
    FileBar->addAction(OpenModelAction);
    FileBar->addAction(OpenStageAction);
    FileBar->addAction(OpenMotionAction);
    FileBar->addAction(OpenCameraAction);
    FileBar->addAction(OpenAudioAction);

    QToolBar* OptionBar = addToolBar(QStringLiteral("显示"));
    OptionBar->setObjectName(QStringLiteral("OptionToolBar"));
    OptionBar->addAction(PhysicsAction);
    OptionBar->addAction(EdgeAction);
    OptionBar->addAction(GridAction);
    OptionBar->addAction(ShadowAction);
    OptionBar->addAction(CameraFollowAction);
    OptionBar->addAction(ResetCameraAction);

    addToolBarBreak(Qt::BottomToolBarArea);
    QToolBar* PlayBar = new QToolBar(QStringLiteral("播放控制"), this);
    PlayBar->setObjectName(QStringLiteral("PlayToolBar"));
    addToolBar(Qt::BottomToolBarArea, PlayBar);
    PlayBar->addAction(PlayAction);
    PlayBar->addAction(StopAction);
    PlayBar->addAction(LoopAction);

    TimelineSlider = new QSlider(Qt::Horizontal, PlayBar);
    TimelineSlider->setRange(0, 0);
    TimelineSlider->setMinimumWidth(300);
    TimelineSlider->setToolTip(QStringLiteral("时间轴（拖动以跳转）"));
    connect(TimelineSlider, &QSlider::valueChanged, this, [this](int Value) {
        if (!SliderUpdating) {
            SeekToFrame(float(Value));
        }
    });
    PlayBar->addWidget(TimelineSlider);

    FrameLabel = new QLabel(PlayBar);
    FrameLabel->setMinimumWidth(170);
    FrameLabel->setAlignment(Qt::AlignCenter);
    PlayBar->addWidget(FrameLabel);

    PlayBar->addWidget(new QLabel(QStringLiteral(" 速度："), PlayBar));
    SpeedBox = new QComboBox(PlayBar);
    const float Speeds[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    for (float Speed : Speeds) {
        SpeedBox->addItem(QStringLiteral("%1 倍").arg(double(Speed)), double(Speed));
    }
    SpeedBox->setCurrentIndex(3);
    connect(SpeedBox, &QComboBox::currentIndexChanged, this, [this](int Index) {
        PlaybackSpeed = float(SpeedBox->itemData(Index).toDouble());
        Audio->SetRate(PlaybackSpeed);
    });
    PlayBar->addWidget(SpeedBox);

    PlayBar->addWidget(new QLabel(QStringLiteral(" 音量："), PlayBar));
    VolumeSlider = new QSlider(Qt::Horizontal, PlayBar);
    VolumeSlider->setRange(0, 100);
    VolumeSlider->setValue(50);
    VolumeSlider->setMaximumWidth(100);
    VolumeSlider->setEnabled(AudioTrack::IsSupported());
    connect(VolumeSlider, &QSlider::valueChanged, this, [this](int Value) { Audio->SetVolume(float(Value) / 100.0f); });
    PlayBar->addWidget(VolumeSlider);
    UpdateFrameLabel();
}

void MainWindow::CreateDocks()
{
    QDockWidget* Dock = new QDockWidget(QStringLiteral("模型"), this);
    Dock->setObjectName(QStringLiteral("ModelDock"));
    QTabWidget* Tabs = new QTabWidget(Dock);

    // 表情面板：选择表情后用滑块手动调整权重（与动作中的权重取较大值）
    QWidget* MorphPage = new QWidget(Tabs);
    QVBoxLayout* MorphLayout = new QVBoxLayout(MorphPage);
    MorphList = new QListWidget(MorphPage);
    connect(MorphList, &QListWidget::currentRowChanged, this, [this](int) { OnMorphSelected(); });
    MorphLayout->addWidget(MorphList);
    QHBoxLayout* SliderLayout = new QHBoxLayout();
    MorphSlider = new QSlider(Qt::Horizontal, MorphPage);
    MorphSlider->setRange(0, 100);
    MorphSlider->setEnabled(false);
    connect(MorphSlider, &QSlider::valueChanged, this, &MainWindow::OnMorphSliderChanged);
    MorphValueLabel = new QLabel(QStringLiteral("0.00"), MorphPage);
    SliderLayout->addWidget(new QLabel(QStringLiteral("权重："), MorphPage));
    SliderLayout->addWidget(MorphSlider);
    SliderLayout->addWidget(MorphValueLabel);
    MorphLayout->addLayout(SliderLayout);
    Tabs->addTab(MorphPage, QStringLiteral("表情"));

    // 信息面板
    InfoLabel = new QLabel(Tabs);
    InfoLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    InfoLabel->setWordWrap(true);
    InfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    InfoLabel->setMargin(8);
    Tabs->addTab(InfoLabel, QStringLiteral("信息"));

    Dock->setWidget(Tabs);
    addDockWidget(Qt::RightDockWidgetArea, Dock);
    RefreshModelInfo();
}

// ======================== 文件打开 ========================

void MainWindow::OpenModelDialog()
{
    QString Path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 PMX 模型"), QFileInfo(ModelPath).absolutePath(),
                                                QStringLiteral("PMX 模型 (*.pmx);;所有文件 (*)"));
    if (!Path.isEmpty()) {
        OpenModel(Path);
    }
}

void MainWindow::OpenMotionDialog()
{
    QString Path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 VMD 动作"), QFileInfo(MotionPath).absolutePath(),
                                                QStringLiteral("VMD 动作 (*.vmd);;所有文件 (*)"));
    if (!Path.isEmpty()) {
        OpenMotion(Path);
    }
}

void MainWindow::AppendMotionDialog()
{
    QStringList Paths = QFileDialog::getOpenFileNames(this, QStringLiteral("追加 VMD 动作"), QFileInfo(MotionPath).absolutePath(),
                                                      QStringLiteral("VMD 动作 (*.vmd);;所有文件 (*)"));
    for (const QString& Path : Paths) {
        AppendMotion(Path);
    }
}

void MainWindow::OpenStageDialog()
{
    QString Path = QFileDialog::getOpenFileName(this, QStringLiteral("打开舞台模型"), QFileInfo(StagePath).absolutePath(),
                                                QStringLiteral("PMX 模型 (*.pmx);;所有文件 (*)"));
    if (!Path.isEmpty()) {
        OpenStage(Path);
    }
}

void MainWindow::OpenCameraDialog()
{
    QString Path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 VMD 镜头动作"), QFileInfo(CameraPath).absolutePath(),
                                                QStringLiteral("VMD 镜头动作 (*.vmd);;所有文件 (*)"));
    if (!Path.isEmpty()) {
        OpenCameraMotion(Path);
    }
}

void MainWindow::OpenAudioDialog()
{
    QString Path = QFileDialog::getOpenFileName(this, QStringLiteral("打开音频"), QString(),
                                                QStringLiteral("音频文件 (*.wav *.mp3 *.ogg *.m4a *.flac *.aac);;所有文件 (*)"));
    if (!Path.isEmpty()) {
        OpenAudio(Path);
    }
}

bool MainWindow::OpenFile(const QString& FilePath)
{
    QString Suffix = QFileInfo(FilePath).suffix().toLower();
    if (Suffix == QLatin1String("pmx")) {
        return OpenModel(FilePath);
    }
    if (Suffix == QLatin1String("vmd")) {
        // 只含镜头关键帧的 VMD 作为镜头动作，否则作为模型动作
        VmdMotion Probe;
        QString Error;
        if (!Probe.Load(FilePath, &Error)) {
            ShowError(QStringLiteral("打开动作失败"), Error);
            return false;
        }
        if (Probe.HasCamera() && !Probe.HasBodyMotion()) {
            return OpenCameraMotion(FilePath);
        }
        return OpenMotion(FilePath);
    }
    if (IsAudioFile(Suffix)) {
        return OpenAudio(FilePath);
    }
    if (Suffix == QLatin1String("bvmd") || Suffix == QLatin1String("bpmx")) {
        ShowError(QStringLiteral("不支持的格式"), QStringLiteral(".bvmd / .bpmx 是 babylon-mmd 专用格式，请使用原始的 .vmd / .pmx 文件。"));
        return false;
    }
    ShowError(QStringLiteral("不支持的文件"), QStringLiteral("无法识别的文件类型：%1").arg(FilePath));
    return false;
}

bool MainWindow::OpenModel(const QString& FilePath)
{
    QString Error;
    statusBar()->showMessage(QStringLiteral("正在加载模型……"));
    if (!ViewerPtr->LoadModel(FilePath, &Error)) {
        ShowError(QStringLiteral("加载模型失败"), Error);
        return false;
    }
    ModelPath = FilePath;
    AddRecentFile(FilePath);
    RefreshMorphList();
    RefreshModelInfo();
    UpdateWindowTitle();
    statusBar()->showMessage(QStringLiteral("模型已加载：%1").arg(ViewerPtr->Model().Model().Name), 5000);
    return true;
}

bool MainWindow::OpenMotion(const QString& FilePath)
{
    QString Error;
    if (!ViewerPtr->LoadMotion(FilePath, &Error)) {
        ShowError(QStringLiteral("加载动作失败"), Error);
        return false;
    }
    MotionPath = FilePath;
    ExtraMotionPaths.clear();
    AddRecentFile(FilePath);
    UpdateTimelineRange();
    RefreshModelInfo();
    statusBar()->showMessage(QStringLiteral("动作已加载：%1（共 %2 帧）")
                                 .arg(QFileInfo(FilePath).fileName())
                                 .arg(int(ViewerPtr->BodyMotion().MaxFrame)),
                             5000);
    return true;
}

bool MainWindow::AppendMotion(const QString& FilePath)
{
    if (!ViewerPtr->HasMotion()) {
        return OpenMotion(FilePath);
    }
    QString Error;
    if (!ViewerPtr->AppendMotion(FilePath, &Error)) {
        ShowError(QStringLiteral("追加动作失败"), Error);
        return false;
    }
    ExtraMotionPaths.append(FilePath);
    UpdateTimelineRange();
    RefreshModelInfo();
    statusBar()->showMessage(QStringLiteral("已追加动作：%1").arg(QFileInfo(FilePath).fileName()), 5000);
    return true;
}

bool MainWindow::OpenStage(const QString& FilePath)
{
    QString Error;
    statusBar()->showMessage(QStringLiteral("正在加载舞台……"));
    if (!ViewerPtr->LoadStage(FilePath, &Error)) {
        ShowError(QStringLiteral("加载舞台失败"), Error);
        return false;
    }
    StagePath = FilePath;
    RefreshModelInfo();
    statusBar()->showMessage(QStringLiteral("舞台已加载：%1").arg(QFileInfo(FilePath).fileName()), 5000);
    return true;
}

bool MainWindow::OpenCameraMotion(const QString& FilePath)
{
    QString Error;
    if (!ViewerPtr->LoadCameraMotion(FilePath, &Error)) {
        ShowError(QStringLiteral("加载镜头动作失败"), Error);
        return false;
    }
    CameraPath = FilePath;
    AddRecentFile(FilePath);
    UpdateTimelineRange();
    RefreshModelInfo();
    statusBar()->showMessage(QStringLiteral("镜头动作已加载：%1").arg(QFileInfo(FilePath).fileName()), 5000);
    return true;
}

bool MainWindow::OpenAudio(const QString& FilePath)
{
    QString Error;
    if (!Audio->Load(FilePath, &Error)) {
        ShowError(QStringLiteral("加载音频失败"), Error);
        return false;
    }
    Audio->SetVolume(float(VolumeSlider->value()) / 100.0f);
    Audio->SetRate(PlaybackSpeed);
    Audio->Seek(double(Frame) / MotionFps);
    if (Playing) {
        Audio->Play();
    }
    AddRecentFile(FilePath);
    UpdateTimelineRange();
    RefreshModelInfo();
    statusBar()->showMessage(QStringLiteral("音频已加载：%1").arg(QFileInfo(FilePath).fileName()), 5000);
    return true;
}

void MainWindow::AddRecentFile(const QString& FilePath)
{
    QSettings Settings;
    QStringList Files = Settings.value(QStringLiteral("RecentFiles")).toStringList();
    QString Absolute = QFileInfo(FilePath).absoluteFilePath();
    Files.removeAll(Absolute);
    Files.prepend(Absolute);
    while (Files.size() > MaxRecentFiles) {
        Files.removeLast();
    }
    Settings.setValue(QStringLiteral("RecentFiles"), Files);
    UpdateRecentMenu();
}

void MainWindow::UpdateRecentMenu()
{
    RecentMenu->clear();
    QSettings Settings;
    QStringList Files = Settings.value(QStringLiteral("RecentFiles")).toStringList();
    if (Files.isEmpty()) {
        QAction* Empty = RecentMenu->addAction(QStringLiteral("（无）"));
        Empty->setEnabled(false);
        return;
    }
    for (const QString& Path : Files) {
        QAction* Item = RecentMenu->addAction(QFileInfo(Path).fileName());
        Item->setToolTip(Path);
        Item->setStatusTip(Path);
        connect(Item, &QAction::triggered, this, [this, Path]() { OpenFile(Path); });
    }
    RecentMenu->addSeparator();
    QAction* ClearItem = RecentMenu->addAction(QStringLiteral("清空列表"));
    connect(ClearItem, &QAction::triggered, this, [this]() {
        QSettings Settings;
        Settings.remove(QStringLiteral("RecentFiles"));
        UpdateRecentMenu();
    });
}

void MainWindow::dragEnterEvent(QDragEnterEvent* Event)
{
    if (Event->mimeData()->hasUrls()) {
        Event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* Event)
{
    // 先打开模型，再打开动作等其它文件，保证顺序合理
    QStringList Files;
    for (const QUrl& Url : Event->mimeData()->urls()) {
        if (Url.isLocalFile()) {
            Files.append(Url.toLocalFile());
        }
    }
    OpenFiles(Files);
}

void MainWindow::OpenFiles(const QStringList& Files)
{
    // 第一个 PMX 作为角色，第二个作为舞台；第一个动作 VMD 作为主动作，其余动作 VMD 追加合并
    int ModelCount = 0;
    for (const QString& Path : Files) {
        if (Path.endsWith(QStringLiteral(".pmx"), Qt::CaseInsensitive)) {
            if (ModelCount == 0) {
                OpenModel(Path);
            } else {
                OpenStage(Path);
            }
            ++ModelCount;
        }
    }
    bool FirstMotion = true;
    for (const QString& Path : Files) {
        if (Path.endsWith(QStringLiteral(".pmx"), Qt::CaseInsensitive)) {
            continue;
        }
        if (Path.endsWith(QStringLiteral(".vmd"), Qt::CaseInsensitive)) {
            VmdMotion Probe;
            QString Error;
            if (Probe.Load(Path, &Error) && Probe.HasBodyMotion()) {
                if (FirstMotion) {
                    OpenMotion(Path);
                    FirstMotion = false;
                } else {
                    AppendMotion(Path);
                }
                continue;
            }
        }
        OpenFile(Path);
    }
}

// ======================== 播放控制 ========================

float MainWindow::TotalFrames() const
{
    float Result = ViewerPtr->MaxFrame();
    if (Audio->HasAudio()) {
        Result = qMax(Result, float(Audio->DurationSeconds() * MotionFps));
    }
    return Result;
}

void MainWindow::Play()
{
    if (Playing) {
        return;
    }
    if (Frame >= TotalFrames() && TotalFrames() > 0.0f) {
        SeekToFrame(0.0f);
    }
    Playing = true;
    Audio->Seek(double(Frame) / MotionFps);
    Audio->Play();
    PlayAction->setText(QStringLiteral("暂停"));
}

void MainWindow::Pause()
{
    Playing = false;
    Audio->Pause();
    PlayAction->setText(QStringLiteral("播放"));
}

void MainWindow::TogglePlay()
{
    if (Playing) {
        Pause();
    } else {
        Play();
    }
}

void MainWindow::Stop()
{
    Pause();
    Audio->Stop();
    SeekToFrame(0.0f);
}

void MainWindow::SeekToFrame(float NewFrame)
{
    Frame = qMax(0.0f, NewFrame);
    Audio->Seek(double(Frame) / MotionFps);
    ViewerPtr->SetFrame(Frame, 0.0f, true);
    UpdateFrameLabel();
}

void MainWindow::OnTick()
{
    qint64 Now = Clock.elapsed();
    float Delta = float(Now - LastTickMs) / 1000.0f;
    LastTickMs = Now;
    Delta = qBound(0.0f, Delta, 0.1f);

    bool Jumped = false;
    if (Playing) {
        Frame += Delta * MotionFps * PlaybackSpeed;
        if (Audio->IsPlaying()) {
            // 以音频时间为准，偏差超过 2 帧时校正
            float AudioFrame = float(Audio->PositionSeconds() * MotionFps);
            if (qAbs(AudioFrame - Frame) > 2.0f) {
                Frame = AudioFrame;
            }
        }
        float Total = TotalFrames();
        if (Total > 0.0f && Frame > Total) {
            if (LoopAction->isChecked()) {
                Frame = 0.0f;
                Audio->Seek(0.0);
                if (!Audio->IsPlaying()) {
                    Audio->Play();
                }
                Jumped = true;
            } else {
                Frame = Total;
                Pause();
            }
        }
    }
    // 暂停时也持续推进物理，使头发、裙摆能自然静止下来
    ViewerPtr->SetFrame(Frame, Delta * (Playing ? PlaybackSpeed : 1.0f), Jumped);
    UpdateFrameLabel();
}

void MainWindow::UpdateTimelineRange()
{
    SliderUpdating = true;
    TimelineSlider->setRange(0, int(TotalFrames()));
    SliderUpdating = false;
    UpdateFrameLabel();
}

void MainWindow::UpdateFrameLabel()
{
    if (!TimelineSlider->isSliderDown()) {
        SliderUpdating = true;
        if (TimelineSlider->maximum() < int(TotalFrames())) {
            TimelineSlider->setMaximum(int(TotalFrames()));
        }
        TimelineSlider->setValue(int(Frame));
        SliderUpdating = false;
    }
    int Seconds = int(Frame / MotionFps);
    FrameLabel->setText(QStringLiteral("帧 %1 / %2  (%3:%4)")
                            .arg(int(Frame))
                            .arg(int(TotalFrames()))
                            .arg(Seconds / 60)
                            .arg(Seconds % 60, 2, 10, QLatin1Char('0')));
}

void MainWindow::UpdateWindowTitle()
{
    QString Title = QStringLiteral("Qt MMD 模型查看器");
    if (ViewerPtr->HasModel()) {
        Title = QStringLiteral("%1 - %2").arg(ViewerPtr->Model().Model().Name, Title);
    }
    setWindowTitle(Title);
}

// ======================== 表情与信息面板 ========================

void MainWindow::RefreshMorphList()
{
    MorphList->clear();
    if (!ViewerPtr->HasModel()) {
        MorphSlider->setEnabled(false);
        return;
    }
    const QVector<PmxMorph>& Morphs = ViewerPtr->Model().Model().Morphs;
    const QStringList PanelNames = {
        QStringLiteral("系统"), QStringLiteral("眉"), QStringLiteral("眼"), QStringLiteral("口"), QStringLiteral("其他"),
    };
    for (const PmxMorph& Morph : Morphs) {
        QString Panel = (Morph.Panel >= 0 && Morph.Panel < PanelNames.size()) ? PanelNames.at(Morph.Panel) : PanelNames.last();
        MorphList->addItem(QStringLiteral("[%1] %2").arg(Panel, Morph.Name));
    }
    MorphSlider->setEnabled(false);
}

void MainWindow::OnMorphSelected()
{
    int Row = MorphList->currentRow();
    bool Valid = ViewerPtr->HasModel() && Row >= 0 && Row < ViewerPtr->Model().MorphCount();
    MorphSlider->setEnabled(Valid);
    MorphSlider->blockSignals(true);
    MorphSlider->setValue(Valid ? int(ViewerPtr->Model().ManualMorphWeight(Row) * 100.0f + 0.5f) : 0);
    MorphSlider->blockSignals(false);
    MorphValueLabel->setText(QString::number(double(MorphSlider->value()) / 100.0, 'f', 2));
}

void MainWindow::OnMorphSliderChanged(int Value)
{
    int Row = MorphList->currentRow();
    if (!ViewerPtr->HasModel() || Row < 0) {
        return;
    }
    ViewerPtr->Model().SetManualMorphWeight(Row, float(Value) / 100.0f);
    MorphValueLabel->setText(QString::number(double(Value) / 100.0, 'f', 2));
    ViewerPtr->SetFrame(Frame, 0.0f, false);
}

void MainWindow::RefreshModelInfo()
{
    QString Text;
    if (ViewerPtr->HasModel()) {
        const PmxModel& Model = ViewerPtr->Model().Model();
        Text += QStringLiteral("<b>模型：</b>%1<br>").arg(Model.Name.toHtmlEscaped());
        Text += QStringLiteral("<b>PMX 版本：</b>%1<br>").arg(double(Model.Version), 0, 'f', 1);
        Text += QStringLiteral("顶点：%1　面：%2<br>").arg(Model.Vertices.size()).arg(Model.Indices.size() / 3);
        Text += QStringLiteral("材质：%1　纹理：%2<br>").arg(Model.Materials.size()).arg(Model.Textures.size());
        Text += QStringLiteral("骨骼：%1　表情：%2<br>").arg(Model.Bones.size()).arg(Model.Morphs.size());
        Text += QStringLiteral("刚体：%1　关节：%2<br><br>").arg(Model.RigidBodies.size()).arg(Model.Joints.size());
        Text += QStringLiteral("<b>说明：</b><br>%1<br><br>").arg(Model.Comment.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    } else {
        Text += QStringLiteral("尚未加载模型。<br><br>");
    }
    Text += QStringLiteral("<b>动作：</b>%1<br>").arg(MotionPath.isEmpty() ? QStringLiteral("无") : QFileInfo(MotionPath).fileName().toHtmlEscaped());
    for (const QString& Extra : ExtraMotionPaths) {
        Text += QStringLiteral("　＋ %1<br>").arg(QFileInfo(Extra).fileName().toHtmlEscaped());
    }
    Text += QStringLiteral("<b>舞台：</b>%1<br>").arg(StagePath.isEmpty() ? QStringLiteral("无") : QFileInfo(StagePath).fileName().toHtmlEscaped());
    Text += QStringLiteral("<b>镜头：</b>%1<br>").arg(CameraPath.isEmpty() ? QStringLiteral("无") : QFileInfo(CameraPath).fileName().toHtmlEscaped());
    Text += QStringLiteral("<b>音频：</b>%1<br><br>").arg(Audio->HasAudio() ? QFileInfo(Audio->FilePath()).fileName().toHtmlEscaped() : QStringLiteral("无"));
    Text += QStringLiteral("<b>操作：</b><br>左键拖动：旋转视角<br>右键/中键拖动：平移<br>滚轮：缩放<br>空格：播放/暂停　R：重置视角");
    InfoLabel->setText(Text);
}

void MainWindow::ShowAbout()
{
    QMessageBox::about(this, QStringLiteral("关于"),
                       QStringLiteral("<h3>Qt MMD 模型查看器</h3>"
                                      "<p>使用 Qt6 C++ 与 OpenGL 3.3 实现的 MMD（MikuMikuDance）模型与动作查看器。</p>"
                                      "<p>支持 PMX 模型、VMD 动作 / 镜头动作、IK、付与、表情、卡通着色、描边、阴影，"
                                      "以及自研的简化物理引擎。</p>"
                                      "<p>灵感来自 Phoshco 的 model-viewer 项目（Babylon.js + babylon-mmd）。</p>"));
}

void MainWindow::ShowError(const QString& Title, const QString& Message)
{
    statusBar()->showMessage(Message, 8000);
    QMessageBox::warning(this, Title, Message);
}

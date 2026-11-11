#include "ViewerWidget.h"

#include "ImageLoader.h"
#include "MathUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

namespace
{
// 纹理单元分配
const int DiffuseUnit = 0;
const int SphereUnit = 1;
const int ToonUnit = 2;
const int ShadowUnit = 3;
const int BoneUnit = 4;

// MMD 默认光源：方向 (-0.5, -1, 0.5)（左手系），颜色 154/255
const QVector3D DefaultLightDirection = QVector3D(-0.5f, -1.0f, -0.5f).normalized();
const QVector3D DefaultLightColor(0.6f, 0.6f, 0.6f);
}

ViewerWidget::ViewerWidget(QWidget* Parent)
    : QOpenGLWidget(Parent)
    , MotionLoaded(false)
    , CameraLoaded(false)
    , Frame(0.0f)
    , PhysicsOn(true)
    , EdgeOn(true)
    , GridOn(true)
    , ShadowOn(true)
    , CameraMotionOn(true)
    , ActiveButtons(Qt::NoButton)
    , GlReady(false)
    , ModelProgram(nullptr)
    , EdgeProgram(nullptr)
    , ShadowProgram(nullptr)
    , GroundProgram(nullptr)
    , GroundVao(0)
    , GroundBuffer(0)
    , WhiteTexture(0)
    , ShadowFramebuffer(0)
    , ShadowTexture(0)
    , ShadowSize(2048)
    , BoneTextureWidth(1024)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
}

ViewerWidget::~ViewerWidget()
{
    ReleaseAll();
}

void ViewerWidget::ReleaseAll()
{
    if (!GlReady) {
        return;
    }
    makeCurrent();
    ReleaseModelGpu(Character);
    ReleaseModelGpu(Stage);
    if (GroundBuffer) {
        glDeleteBuffers(1, &GroundBuffer);
    }
    if (GroundVao) {
        glDeleteVertexArrays(1, &GroundVao);
    }
    if (WhiteTexture) {
        glDeleteTextures(1, &WhiteTexture);
    }
    for (GLuint Texture : InternalToons) {
        glDeleteTextures(1, &Texture);
    }
    if (ShadowTexture) {
        glDeleteTextures(1, &ShadowTexture);
    }
    if (ShadowFramebuffer) {
        glDeleteFramebuffers(1, &ShadowFramebuffer);
    }
    delete ModelProgram;
    delete EdgeProgram;
    delete ShadowProgram;
    delete GroundProgram;
    ModelProgram = EdgeProgram = ShadowProgram = GroundProgram = nullptr;
    GroundBuffer = GroundVao = WhiteTexture = ShadowTexture = ShadowFramebuffer = 0;
    InternalToons.clear();
    GlReady = false;
    doneCurrent();
}

// ======================== 资源加载 ========================

bool ViewerWidget::LoadModel(const QString& FilePath, QString* ErrorMessage)
{
    if (!Character.Runtime.Load(FilePath, ErrorMessage)) {
        Character.GpuDirty = true;
        update();
        return false;
    }
    if (MotionLoaded) {
        Character.Runtime.SetMotion(&Motion);
    }
    Character.Runtime.ResetPhysics();
    Character.Runtime.Update(Frame, 0.0f, PhysicsOn);
    Character.GpuDirty = true;
    update();
    return true;
}

bool ViewerWidget::LoadStage(const QString& FilePath, QString* ErrorMessage)
{
    if (!Stage.Runtime.Load(FilePath, ErrorMessage)) {
        Stage.GpuDirty = true;
        update();
        return false;
    }
    Stage.Runtime.ResetPhysics();
    Stage.Runtime.Update(Frame, 0.0f, PhysicsOn);
    Stage.GpuDirty = true;
    update();
    return true;
}

void ViewerWidget::ClearStage()
{
    Stage.Runtime.Clear();
    Stage.GpuDirty = true;
    update();
}

bool ViewerWidget::LoadMotion(const QString& FilePath, QString* ErrorMessage)
{
    VmdMotion NewMotion;
    if (!NewMotion.Load(FilePath, ErrorMessage)) {
        return false;
    }
    Character.Runtime.SetMotion(nullptr);
    Motion = NewMotion;
    MotionLoaded = true;
    Character.Runtime.SetMotion(&Motion);
    Character.Runtime.ResetPhysics();
    Character.Runtime.Update(Frame, 0.0f, PhysicsOn);
    update();
    return true;
}

bool ViewerWidget::AppendMotion(const QString& FilePath, QString* ErrorMessage)
{
    if (!MotionLoaded) {
        return LoadMotion(FilePath, ErrorMessage);
    }
    VmdMotion Extra;
    if (!Extra.Load(FilePath, ErrorMessage)) {
        return false;
    }
    Character.Runtime.SetMotion(nullptr);
    Motion.Merge(Extra);
    Character.Runtime.SetMotion(&Motion);
    Character.Runtime.Update(Frame, 0.0f, PhysicsOn);
    update();
    return true;
}

bool ViewerWidget::LoadCameraMotion(const QString& FilePath, QString* ErrorMessage)
{
    VmdMotion NewMotion;
    if (!NewMotion.Load(FilePath, ErrorMessage)) {
        return false;
    }
    if (!NewMotion.HasCamera()) {
        if (ErrorMessage) {
            *ErrorMessage = QStringLiteral("该 VMD 文件中没有镜头关键帧。");
        }
        return false;
    }
    Camera = NewMotion;
    CameraLoaded = true;
    update();
    return true;
}

void ViewerWidget::ClearMotion()
{
    Character.Runtime.SetMotion(nullptr);
    Motion.Clear();
    MotionLoaded = false;
    Character.Runtime.ResetPhysics();
    Character.Runtime.Update(Frame, 0.0f, PhysicsOn);
    update();
}

void ViewerWidget::ClearCameraMotion()
{
    Camera.Clear();
    CameraLoaded = false;
    update();
}

float ViewerWidget::MaxFrame() const
{
    float Result = 0.0f;
    if (MotionLoaded) {
        Result = qMax(Result, Motion.MaxFrame);
    }
    if (CameraLoaded) {
        Result = qMax(Result, Camera.MaxFrame);
    }
    return Result;
}

void ViewerWidget::SetFrame(float NewFrame, float DeltaSeconds, bool ResetPhysics)
{
    Frame = NewFrame;
    SceneModel* Scenes[] = { &Character, &Stage };
    for (SceneModel* Scene : Scenes) {
        if (Scene->Runtime.IsLoaded()) {
            if (ResetPhysics) {
                Scene->Runtime.ResetPhysics();
            }
            Scene->Runtime.Update(Frame, DeltaSeconds, PhysicsOn);
        }
    }
    update();
}

void ViewerWidget::SetPhysicsEnabled(bool Enabled)
{
    PhysicsOn = Enabled;
    SetFrame(Frame, 0.0f, true);
}

void ViewerWidget::SetEdgeEnabled(bool Enabled)
{
    EdgeOn = Enabled;
    update();
}

void ViewerWidget::SetGridEnabled(bool Enabled)
{
    GridOn = Enabled;
    update();
}

void ViewerWidget::SetShadowEnabled(bool Enabled)
{
    ShadowOn = Enabled;
    update();
}

void ViewerWidget::SetCameraMotionEnabled(bool Enabled)
{
    CameraMotionOn = Enabled;
    update();
}

void ViewerWidget::ResetCamera()
{
    Orbit.Reset();
    emit ViewChanged();
    update();
}

// ======================== OpenGL 初始化 ========================

bool ViewerWidget::BuildProgram(QOpenGLShaderProgram* Program, const QString& Name)
{
    bool Ok = Program->addShaderFromSourceFile(QOpenGLShader::Vertex, QStringLiteral(":/Shaders/%1.vert").arg(Name))
        && Program->addShaderFromSourceFile(QOpenGLShader::Fragment, QStringLiteral(":/Shaders/%1.frag").arg(Name))
        && Program->link();
    if (!Ok) {
        qWarning("着色器 %s 编译或链接失败：%s", qPrintable(Name), qPrintable(Program->log()));
    }
    return Ok;
}

void ViewerWidget::initializeGL()
{
    initializeOpenGLFunctions();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &ViewerWidget::ReleaseAll, Qt::DirectConnection);

    ModelProgram = new QOpenGLShaderProgram();
    EdgeProgram = new QOpenGLShaderProgram();
    ShadowProgram = new QOpenGLShaderProgram();
    GroundProgram = new QOpenGLShaderProgram();
    BuildProgram(ModelProgram, QStringLiteral("Model"));
    BuildProgram(EdgeProgram, QStringLiteral("Edge"));
    BuildProgram(ShadowProgram, QStringLiteral("Shadow"));
    BuildProgram(GroundProgram, QStringLiteral("Ground"));

    // 地面：一个大的四边形（两个三角形）
    const float Size = 100.0f;
    const float Vertices[] = {
        -Size, 0.0f, -Size, -Size, 0.0f, Size, Size, 0.0f, Size,
        -Size, 0.0f, -Size, Size, 0.0f, Size, Size, 0.0f, -Size,
    };
    glGenVertexArrays(1, &GroundVao);
    glBindVertexArray(GroundVao);
    glGenBuffers(1, &GroundBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, GroundBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertices), Vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    QImage White(4, 4, QImage::Format_RGBA8888);
    White.fill(Qt::white);
    WhiteTexture = CreateTexture(White, false);
    CreateInternalToons();
    CreateShadowMap();

    GlReady = true;
    Character.GpuDirty = true;
    Stage.GpuDirty = true;
}

void ViewerWidget::CreateInternalToons()
{
    // 内置 toon01~toon10 的近似：上半部分为白色，下半部分为阴影色
    const float Shades[10][3] = {
        {0.80f, 0.80f, 0.80f}, {0.96f, 0.86f, 0.86f}, {0.70f, 0.70f, 0.70f}, {0.94f, 0.88f, 0.80f},
        {0.86f, 0.89f, 0.96f}, {0.72f, 0.72f, 0.82f}, {0.60f, 0.60f, 0.60f}, {0.96f, 0.80f, 0.80f},
        {0.82f, 0.91f, 0.82f}, {0.86f, 0.86f, 0.96f},
    };
    for (int I = 0; I < 10; ++I) {
        QImage Image(4, 32, QImage::Format_RGBA8888);
        for (int Y = 0; Y < 32; ++Y) {
            float T = MathUtil::Clamp((float(Y) / 31.0f - 0.45f) / 0.1f, 0.0f, 1.0f);
            int R = int((1.0f + (Shades[I][0] - 1.0f) * T) * 255.0f);
            int G = int((1.0f + (Shades[I][1] - 1.0f) * T) * 255.0f);
            int B = int((1.0f + (Shades[I][2] - 1.0f) * T) * 255.0f);
            for (int X = 0; X < 4; ++X) {
                Image.setPixelColor(X, Y, QColor(R, G, B));
            }
        }
        InternalToons.append(CreateTexture(Image, true));
    }
}

void ViewerWidget::CreateShadowMap()
{
    glGenTextures(1, &ShadowTexture);
    glBindTexture(GL_TEXTURE_2D, ShadowTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, ShadowSize, ShadowSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &ShadowFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, ShadowFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ShadowTexture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        qWarning("阴影帧缓冲创建失败，阴影将不可用。");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glBindTexture(GL_TEXTURE_2D, 0);
}

GLuint ViewerWidget::CreateTexture(const QImage& Image, bool ClampToEdge)
{
    QImage Rgba = Image.convertToFormat(QImage::Format_RGBA8888);
    GLuint Texture = 0;
    glGenTextures(1, &Texture);
    glBindTexture(GL_TEXTURE_2D, Texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    // 直接按 QImage 的行顺序上传（第一行在 v=0），与 PMX 的 UV 约定（v=0 在顶部）一致
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Rgba.width(), Rgba.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, Rgba.constBits());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLint Wrap = ClampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, Wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, Wrap);
    glBindTexture(GL_TEXTURE_2D, 0);
    return Texture;
}

GLuint ViewerWidget::LoadTextureFile(const SceneModel& Scene, const QString& RelativePath)
{
    QString Path = ImageLoader::ResolvePath(Scene.Runtime.Model().Directory, RelativePath);
    if (Path.isEmpty()) {
        qWarning("找不到纹理：%s", qPrintable(RelativePath));
        return 0;
    }
    QImage Image = ImageLoader::Load(Path);
    if (Image.isNull()) {
        qWarning("无法解码纹理：%s", qPrintable(Path));
        return 0;
    }
    return CreateTexture(Image, false);
}

void ViewerWidget::ReleaseModelGpu(SceneModel& Scene)
{
    GLuint Buffers[] = { Scene.Gpu.PositionBuffer, Scene.Gpu.NormalBuffer, Scene.Gpu.UvBuffer, Scene.Gpu.BoneIndexBuffer,
                         Scene.Gpu.BoneWeightBuffer, Scene.Gpu.EdgeScaleBuffer, Scene.Gpu.IndexBuffer };
    for (GLuint Buffer : Buffers) {
        if (Buffer) {
            glDeleteBuffers(1, &Buffer);
        }
    }
    if (Scene.Gpu.Vao) {
        glDeleteVertexArrays(1, &Scene.Gpu.Vao);
    }
    if (Scene.Gpu.BoneTexture) {
        glDeleteTextures(1, &Scene.Gpu.BoneTexture);
    }
    for (GLuint Texture : Scene.Gpu.Textures) {
        if (Texture) {
            glDeleteTextures(1, &Texture);
        }
    }
    Scene.Gpu = ModelGpu();
}

void ViewerWidget::UploadModel(SceneModel& Scene)
{
    ReleaseModelGpu(Scene);
    Scene.GpuDirty = false;
    if (!Scene.Runtime.IsLoaded()) {
        return;
    }
    const PmxModel& Model = Scene.Runtime.Model();
    const int Count = Model.Vertices.size();
    QVector<float> Normals(Count * 3);
    QVector<float> Weights(Count * 4);
    QVector<float> EdgeScales(Count);
    QVector<qint32> BoneIndices(Count * 4);
    const int BoneCount = qMax(1, Model.Bones.size());
    for (int I = 0; I < Count; ++I) {
        const PmxVertex& Vertex = Model.Vertices.at(I);
        Normals[I * 3 + 0] = Vertex.Normal.x();
        Normals[I * 3 + 1] = Vertex.Normal.y();
        Normals[I * 3 + 2] = Vertex.Normal.z();
        for (int K = 0; K < 4; ++K) {
            int Bone = Vertex.BoneIndex[K];
            BoneIndices[I * 4 + K] = (Bone >= 0 && Bone < BoneCount) ? Bone : 0;
            Weights[I * 4 + K] = Vertex.BoneWeight[K];
        }
        EdgeScales[I] = Vertex.EdgeScale;
    }

    glGenVertexArrays(1, &Scene.Gpu.Vao);
    glBindVertexArray(Scene.Gpu.Vao);

    const QVector<QVector3D>& Positions = Scene.Runtime.Positions();
    glGenBuffers(1, &Scene.Gpu.PositionBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, Scene.Gpu.PositionBuffer);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(Count) * 3 * sizeof(float), Positions.constData(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    glGenBuffers(1, &Scene.Gpu.NormalBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, Scene.Gpu.NormalBuffer);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(Normals.size()) * sizeof(float), Normals.constData(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    const QVector<QVector2D>& Uvs = Scene.Runtime.Uvs();
    glGenBuffers(1, &Scene.Gpu.UvBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, Scene.Gpu.UvBuffer);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(Count) * 2 * sizeof(float), Uvs.constData(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glGenBuffers(1, &Scene.Gpu.BoneIndexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, Scene.Gpu.BoneIndexBuffer);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(BoneIndices.size()) * sizeof(qint32), BoneIndices.constData(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_INT, 0, nullptr);

    glGenBuffers(1, &Scene.Gpu.BoneWeightBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, Scene.Gpu.BoneWeightBuffer);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(Weights.size()) * sizeof(float), Weights.constData(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 0, nullptr);

    glGenBuffers(1, &Scene.Gpu.EdgeScaleBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, Scene.Gpu.EdgeScaleBuffer);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(EdgeScales.size()) * sizeof(float), EdgeScales.constData(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, 0, nullptr);

    glGenBuffers(1, &Scene.Gpu.IndexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Scene.Gpu.IndexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(Model.Indices.size()) * sizeof(quint32), Model.Indices.constData(), GL_STATIC_DRAW);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    Scene.Runtime.TakePositionsDirty();
    Scene.Runtime.TakeUvsDirty();

    // 骨骼矩阵纹理（RGBA32F，每根骨骼 4 个像素）
    glGenTextures(1, &Scene.Gpu.BoneTexture);
    Scene.Gpu.BoneTextureHeight = qMax(1, (BoneCount * 4 + BoneTextureWidth - 1) / BoneTextureWidth);
    glBindTexture(GL_TEXTURE_2D, Scene.Gpu.BoneTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, BoneTextureWidth, Scene.Gpu.BoneTextureHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    UploadBoneTexture(Scene);

    // 材质纹理
    for (const QString& Path : Model.Textures) {
        Scene.Gpu.Textures.append(LoadTextureFile(Scene, Path));
    }
}

void ViewerWidget::UploadBoneTexture(SceneModel& Scene)
{
    if (!Scene.Gpu.BoneTexture) {
        return;
    }
    const QVector<QMatrix4x4>& Skins = Scene.Runtime.SkinMatrices();
    QVector<float> Data(BoneTextureWidth * Scene.Gpu.BoneTextureHeight * 4, 0.0f);
    for (int I = 0; I < Skins.size() && (I + 1) * 16 <= Data.size(); ++I) {
        const float* Source = Skins.at(I).constData(); // 列主序
        for (int K = 0; K < 16; ++K) {
            Data[I * 16 + K] = Source[K];
        }
    }
    glBindTexture(GL_TEXTURE_2D, Scene.Gpu.BoneTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BoneTextureWidth, Scene.Gpu.BoneTextureHeight, GL_RGBA, GL_FLOAT, Data.constData());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ViewerWidget::resizeGL(int Width, int Height)
{
    Q_UNUSED(Width);
    Q_UNUSED(Height);
}

// ======================== 绘制 ========================

void ViewerWidget::ComputeCamera(QMatrix4x4* View, QMatrix4x4* Projection, QVector3D* Eye)
{
    float Aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
    CameraPose Pose;
    if (CameraLoaded && CameraMotionOn && Camera.SampleCamera(Frame, &Pose)) {
        // MMD 镜头 -> 右手坐标系：中心 Z 取反，旋转 R = Ry(ry) * Rx(rx) * Rz(-rz)
        QVector3D Center(Pose.Center.x(), Pose.Center.y(), -Pose.Center.z());
        QQuaternion Rotation = MathUtil::FromAxisRadians(QVector3D(0, 1, 0), Pose.Rotation.y())
            * MathUtil::FromAxisRadians(QVector3D(1, 0, 0), Pose.Rotation.x())
            * MathUtil::FromAxisRadians(QVector3D(0, 0, 1), -Pose.Rotation.z());
        *Eye = Center + Rotation.rotatedVector(QVector3D(0.0f, 0.0f, -Pose.Distance));
        QVector3D Up = Rotation.rotatedVector(QVector3D(0.0f, 1.0f, 0.0f));
        View->setToIdentity();
        View->lookAt(*Eye, Center, Up);
        Projection->setToIdentity();
        Projection->perspective(qBound(1.0f, Pose.Fov, 175.0f), Aspect, 0.5f, 5000.0f);
        return;
    }
    *View = Orbit.ViewMatrix();
    *Eye = Orbit.EyePosition();
    Projection->setToIdentity();
    Projection->perspective(Orbit.Fov, Aspect, 0.5f, 5000.0f);
}

QMatrix4x4 ViewerWidget::ComputeLightMatrix()
{
    QVector3D Center(0.0f, 10.0f, 0.0f);
    if (Character.Runtime.IsLoaded()) {
        int CenterBone = Character.Runtime.FindBone(QStringLiteral("センター"));
        if (CenterBone >= 0) {
            Center = Character.Runtime.BonePosition(CenterBone);
            Center.setY(qMax(Center.y(), 8.0f));
        }
    }
    QMatrix4x4 LightView;
    LightView.lookAt(Center - DefaultLightDirection * 60.0f, Center, QVector3D(0.0f, 1.0f, 0.0f));
    QMatrix4x4 LightProjection;
    LightProjection.ortho(-28.0f, 28.0f, -28.0f, 28.0f, 1.0f, 160.0f);
    return LightProjection * LightView;
}

void ViewerWidget::BindBoneTexture(QOpenGLShaderProgram* Program, const SceneModel& Scene)
{
    glActiveTexture(GL_TEXTURE0 + BoneUnit);
    glBindTexture(GL_TEXTURE_2D, Scene.Gpu.BoneTexture);
    Program->setUniformValue("BoneTexture", BoneUnit);
    Program->setUniformValue("BoneTextureWidth", BoneTextureWidth);
}

void ViewerWidget::DrawShadowPass(const QMatrix4x4& LightMatrix)
{
    glBindFramebuffer(GL_FRAMEBUFFER, ShadowFramebuffer);
    glViewport(0, 0, ShadowSize, ShadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    if (Character.Runtime.IsLoaded() && Character.Gpu.Vao && ShadowProgram->isLinked()) {
        ShadowProgram->bind();
        ShadowProgram->setUniformValue("LightMatrix", LightMatrix);
        BindBoneTexture(ShadowProgram, Character);
        glBindVertexArray(Character.Gpu.Vao);
        const QVector<PmxMaterial>& Materials = Character.Runtime.Model().Materials;
        const QVector<MaterialState>& States = Character.Runtime.Materials();
        for (int I = 0; I < Materials.size(); ++I) {
            const PmxMaterial& Material = Materials.at(I);
            if (!(Material.Flags & (MaterialFlagCastShadow | MaterialFlagGroundShadow)) || Material.IndexCount <= 0) {
                continue;
            }
            if (States.at(I).Diffuse.w() < 0.01f) {
                continue;
            }
            glDrawElements(GL_TRIANGLES, Material.IndexCount, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(qintptr(Material.IndexOffset) * sizeof(quint32)));
        }
        glBindVertexArray(0);
        ShadowProgram->release();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    const qreal Ratio = devicePixelRatioF();
    glViewport(0, 0, int(width() * Ratio), int(height() * Ratio));
}

void ViewerWidget::DrawGround(const QMatrix4x4& View, const QMatrix4x4& Projection, const QMatrix4x4& LightMatrix)
{
    // 加载舞台后，地面只在显示网格时绘制（阴影直接投射到舞台上）
    bool Visible = GridOn || (ShadowOn && !Stage.Runtime.IsLoaded());
    if (!Visible || !GroundProgram->isLinked()) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    GroundProgram->bind();
    GroundProgram->setUniformValue("ViewMatrix", View);
    GroundProgram->setUniformValue("ProjectionMatrix", Projection);
    GroundProgram->setUniformValue("LightMatrix", LightMatrix);
    GroundProgram->setUniformValue("ShowGrid", GridOn ? 1 : 0);
    GroundProgram->setUniformValue("UseShadow", (ShadowOn && Character.Runtime.IsLoaded()) ? 1 : 0);
    GroundProgram->setUniformValue("GroundColor", QVector3D(0.24f, 0.25f, 0.28f));
    glActiveTexture(GL_TEXTURE0 + ShadowUnit);
    glBindTexture(GL_TEXTURE_2D, ShadowTexture);
    GroundProgram->setUniformValue("ShadowMap", ShadowUnit);
    glBindVertexArray(GroundVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    GroundProgram->release();
    glDepthMask(GL_TRUE);
}

void ViewerWidget::DrawModel(SceneModel& Scene, const QMatrix4x4& View, const QMatrix4x4& Projection, const QMatrix4x4& LightMatrix, const QVector3D& Eye)
{
    if (!Scene.Runtime.IsLoaded() || !Scene.Gpu.Vao || !ModelProgram->isLinked()) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ModelProgram->bind();
    ModelProgram->setUniformValue("ViewMatrix", View);
    ModelProgram->setUniformValue("ProjectionMatrix", Projection);
    ModelProgram->setUniformValue("LightMatrix", LightMatrix);
    ModelProgram->setUniformValue("LightDirection", DefaultLightDirection);
    ModelProgram->setUniformValue("LightColor", DefaultLightColor);
    ModelProgram->setUniformValue("CameraPosition", Eye);
    ModelProgram->setUniformValue("DiffuseTexture", DiffuseUnit);
    ModelProgram->setUniformValue("SphereTexture", SphereUnit);
    ModelProgram->setUniformValue("ToonTexture", ToonUnit);
    ModelProgram->setUniformValue("ShadowMap", ShadowUnit);
    BindBoneTexture(ModelProgram, Scene);
    glActiveTexture(GL_TEXTURE0 + ShadowUnit);
    glBindTexture(GL_TEXTURE_2D, ShadowTexture);
    glBindVertexArray(Scene.Gpu.Vao);

    const PmxModel& Model = Scene.Runtime.Model();
    const QVector<MaterialState>& States = Scene.Runtime.Materials();
    auto TextureAt = [&Scene](int Index) -> GLuint {
        return (Index >= 0 && Index < Scene.Gpu.Textures.size()) ? Scene.Gpu.Textures.at(Index) : 0;
    };
    for (int I = 0; I < Model.Materials.size(); ++I) {
        const PmxMaterial& Material = Model.Materials.at(I);
        const MaterialState& State = States.at(I);
        if (Material.IndexCount <= 0 || State.Diffuse.w() < 0.001f) {
            continue;
        }
        if (Material.Flags & MaterialFlagDoubleSided) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }
        ModelProgram->setUniformValue("DiffuseColor", State.Diffuse);
        ModelProgram->setUniformValue("AmbientColor", State.Ambient);
        ModelProgram->setUniformValue("SpecularColor", State.Specular);
        ModelProgram->setUniformValue("SpecularPower", State.SpecularPower);
        ModelProgram->setUniformValue("TextureMul", State.TextureMul);
        ModelProgram->setUniformValue("TextureAdd", State.TextureAdd);
        ModelProgram->setUniformValue("SphereMul", State.SphereMul);
        ModelProgram->setUniformValue("SphereAdd", State.SphereAdd);
        ModelProgram->setUniformValue("ToonMul", State.ToonMul);
        ModelProgram->setUniformValue("ToonAdd", State.ToonAdd);

        GLuint Diffuse = TextureAt(Material.TextureIndex);
        glActiveTexture(GL_TEXTURE0 + DiffuseUnit);
        glBindTexture(GL_TEXTURE_2D, Diffuse ? Diffuse : WhiteTexture);
        ModelProgram->setUniformValue("UseTexture", Diffuse ? 1 : 0);

        GLuint Sphere = TextureAt(Material.SphereTextureIndex);
        int SphereMode = (Sphere && Material.SphereMode >= 1 && Material.SphereMode <= 2) ? Material.SphereMode : 0;
        glActiveTexture(GL_TEXTURE0 + SphereUnit);
        glBindTexture(GL_TEXTURE_2D, Sphere ? Sphere : WhiteTexture);
        ModelProgram->setUniformValue("SphereMode", SphereMode);

        GLuint Toon = 0;
        if (Material.SharedToon) {
            if (Material.ToonIndex >= 0 && Material.ToonIndex < InternalToons.size()) {
                Toon = InternalToons.at(Material.ToonIndex);
            }
        } else {
            Toon = TextureAt(Material.ToonIndex);
        }
        glActiveTexture(GL_TEXTURE0 + ToonUnit);
        glBindTexture(GL_TEXTURE_2D, Toon ? Toon : WhiteTexture);
        ModelProgram->setUniformValue("UseToon", Toon ? 1 : 0);
        ModelProgram->setUniformValue("UseShadow", (ShadowOn && (Material.Flags & MaterialFlagReceiveShadow)) ? 1 : 0);

        glDrawElements(GL_TRIANGLES, Material.IndexCount, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(qintptr(Material.IndexOffset) * sizeof(quint32)));
    }
    glBindVertexArray(0);
    ModelProgram->release();
    glActiveTexture(GL_TEXTURE0);
}

void ViewerWidget::DrawEdges(SceneModel& Scene, const QMatrix4x4& View, const QMatrix4x4& Projection, const QVector3D& Eye)
{
    if (!EdgeOn || !Scene.Runtime.IsLoaded() || !Scene.Gpu.Vao || !EdgeProgram->isLinked()) {
        return;
    }
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT); // 只画外扩后的背面，形成轮廓
    glEnable(GL_BLEND);
    EdgeProgram->bind();
    EdgeProgram->setUniformValue("ViewMatrix", View);
    EdgeProgram->setUniformValue("ProjectionMatrix", Projection);
    EdgeProgram->setUniformValue("CameraPosition", Eye);
    EdgeProgram->setUniformValue("EdgeFactor", 0.0012f);
    BindBoneTexture(EdgeProgram, Scene);
    glBindVertexArray(Scene.Gpu.Vao);
    const PmxModel& Model = Scene.Runtime.Model();
    const QVector<MaterialState>& States = Scene.Runtime.Materials();
    for (int I = 0; I < Model.Materials.size(); ++I) {
        const PmxMaterial& Material = Model.Materials.at(I);
        const MaterialState& State = States.at(I);
        if (!(Material.Flags & MaterialFlagEdge) || State.EdgeSize <= 0.0f || Material.IndexCount <= 0
            || State.Diffuse.w() < 0.001f) {
            continue;
        }
        EdgeProgram->setUniformValue("EdgeSize", State.EdgeSize);
        EdgeProgram->setUniformValue("EdgeColor", State.EdgeColor);
        glDrawElements(GL_TRIANGLES, Material.IndexCount, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(qintptr(Material.IndexOffset) * sizeof(quint32)));
    }
    glBindVertexArray(0);
    EdgeProgram->release();
    glCullFace(GL_BACK);
}

void ViewerWidget::UploadDynamicData(SceneModel& Scene)
{
    if (Scene.GpuDirty) {
        UploadModel(Scene);
    }
    if (!Scene.Runtime.IsLoaded() || !Scene.Gpu.Vao) {
        return;
    }
    if (Scene.Runtime.TakePositionsDirty()) {
        glBindBuffer(GL_ARRAY_BUFFER, Scene.Gpu.PositionBuffer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(Scene.Runtime.Positions().size()) * 3 * sizeof(float),
                        Scene.Runtime.Positions().constData());
    }
    if (Scene.Runtime.TakeUvsDirty()) {
        glBindBuffer(GL_ARRAY_BUFFER, Scene.Gpu.UvBuffer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(Scene.Runtime.Uvs().size()) * 2 * sizeof(float),
                        Scene.Runtime.Uvs().constData());
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    UploadBoneTexture(Scene);
}

void ViewerWidget::paintGL()
{
    if (!GlReady) {
        return;
    }
    UploadDynamicData(Character);
    UploadDynamicData(Stage);

    QMatrix4x4 View;
    QMatrix4x4 Projection;
    QVector3D Eye;
    ComputeCamera(&View, &Projection, &Eye);
    QMatrix4x4 LightMatrix = ComputeLightMatrix();

    if (ShadowOn) {
        DrawShadowPass(LightMatrix);
    }

    glClearColor(0.16f, 0.17f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // 舞台先画，然后是地面与角色
    DrawModel(Stage, View, Projection, LightMatrix, Eye);
    DrawEdges(Stage, View, Projection, Eye);
    DrawGround(View, Projection, LightMatrix);
    DrawModel(Character, View, Projection, LightMatrix, Eye);
    DrawEdges(Character, View, Projection, Eye);
    glDisable(GL_CULL_FACE);
}

// ======================== 鼠标操作 ========================

void ViewerWidget::mousePressEvent(QMouseEvent* Event)
{
    LastMousePosition = Event->position().toPoint();
    ActiveButtons = Event->buttons();
}

void ViewerWidget::mouseMoveEvent(QMouseEvent* Event)
{
    QPoint Position = Event->position().toPoint();
    QPoint Delta = Position - LastMousePosition;
    LastMousePosition = Position;
    if (Event->buttons() & Qt::LeftButton) {
        Orbit.Rotate(float(Delta.x()), float(Delta.y()));
    } else if (Event->buttons() & (Qt::RightButton | Qt::MiddleButton)) {
        Orbit.Pan(float(Delta.x()), float(Delta.y()));
    } else {
        return;
    }
    emit ViewChanged();
    update();
}

void ViewerWidget::mouseReleaseEvent(QMouseEvent* Event)
{
    ActiveButtons = Event->buttons();
}

void ViewerWidget::wheelEvent(QWheelEvent* Event)
{
    Orbit.Zoom(float(Event->angleDelta().y()) / 120.0f);
    emit ViewChanged();
    update();
}

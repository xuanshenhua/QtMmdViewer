#ifndef VIEWERWIDGET_H
#define VIEWERWIDGET_H

#include "ModelRuntime.h"
#include "OrbitCamera.h"
#include "VmdMotion.h"

#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector>

// 模型在 GPU 上的资源
struct ModelGpu
{
    GLuint Vao = 0;
    GLuint PositionBuffer = 0;
    GLuint NormalBuffer = 0;
    GLuint UvBuffer = 0;
    GLuint BoneIndexBuffer = 0;
    GLuint BoneWeightBuffer = 0;
    GLuint EdgeScaleBuffer = 0;
    GLuint IndexBuffer = 0;
    GLuint BoneTexture = 0;
    int BoneTextureHeight = 1;
    QVector<GLuint> Textures; // 与 PMX 纹理表一一对应，加载失败为 0
};

// 场景中的一个模型（角色或舞台）：CPU 运行时 + GPU 资源
struct SceneModel
{
    ModelRuntime Runtime;
    ModelGpu Gpu;
    bool GpuDirty = false;
};

// OpenGL 视口：负责渲染模型、描边、地面网格与阴影，并处理鼠标操作的轨道相机。
// 注意：initializeGL / paintGL / mousePressEvent 等是 Qt 规定的虚函数名，必须保持原样。
class ViewerWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit ViewerWidget(QWidget* Parent = nullptr);
    ~ViewerWidget() override;

    bool LoadModel(const QString& FilePath, QString* ErrorMessage);
    bool LoadMotion(const QString& FilePath, QString* ErrorMessage);
    bool LoadCameraMotion(const QString& FilePath, QString* ErrorMessage);
    // 追加动作：把另一个 VMD 合并到当前动作中（例如口型、表情动作）
    bool AppendMotion(const QString& FilePath, QString* ErrorMessage);
    void ClearMotion();
    void ClearCameraMotion();
    // 舞台模型：只渲染，不受动作控制
    bool LoadStage(const QString& FilePath, QString* ErrorMessage);
    void ClearStage();
    bool HasStage() const { return Stage.Runtime.IsLoaded(); }

    bool HasModel() const { return Character.Runtime.IsLoaded(); }
    bool HasMotion() const { return MotionLoaded; }
    bool HasCameraMotion() const { return CameraLoaded; }
    ModelRuntime& Model() { return Character.Runtime; }
    const VmdMotion& BodyMotion() const { return Motion; }
    const VmdMotion& CameraMotion() const { return Camera; }
    float MaxFrame() const;

    // 设置当前帧并推进物理（DeltaSeconds 为距上次更新的真实时间）
    void SetFrame(float Frame, float DeltaSeconds, bool ResetPhysics);
    float CurrentFrame() const { return Frame; }

    void SetPhysicsEnabled(bool Enabled);
    void SetEdgeEnabled(bool Enabled);
    void SetGridEnabled(bool Enabled);
    void SetShadowEnabled(bool Enabled);
    void SetCameraMotionEnabled(bool Enabled);
    bool PhysicsEnabled() const { return PhysicsOn; }
    void ResetCamera();

signals:
    void ViewChanged();

protected:
    void initializeGL() override;
    void resizeGL(int Width, int Height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* Event) override;
    void mouseMoveEvent(QMouseEvent* Event) override;
    void mouseReleaseEvent(QMouseEvent* Event) override;
    void wheelEvent(QWheelEvent* Event) override;

private:
    bool BuildProgram(QOpenGLShaderProgram* Program, const QString& Name);
    void UploadModel(SceneModel& Scene);
    void ReleaseModelGpu(SceneModel& Scene);
    void ReleaseAll();
    void UploadDynamicData(SceneModel& Scene);
    void UploadBoneTexture(SceneModel& Scene);
    GLuint CreateTexture(const QImage& Image, bool ClampToEdge);
    GLuint LoadTextureFile(const SceneModel& Scene, const QString& RelativePath);
    void CreateInternalToons();
    void CreateShadowMap();
    void ComputeCamera(QMatrix4x4* View, QMatrix4x4* Projection, QVector3D* Eye);
    QMatrix4x4 ComputeLightMatrix();
    void DrawShadowPass(const QMatrix4x4& LightMatrix);
    void DrawGround(const QMatrix4x4& View, const QMatrix4x4& Projection, const QMatrix4x4& LightMatrix);
    void DrawModel(SceneModel& Scene, const QMatrix4x4& View, const QMatrix4x4& Projection, const QMatrix4x4& LightMatrix, const QVector3D& Eye);
    void DrawEdges(SceneModel& Scene, const QMatrix4x4& View, const QMatrix4x4& Projection, const QVector3D& Eye);
    void BindBoneTexture(QOpenGLShaderProgram* Program, const SceneModel& Scene);

    SceneModel Character;
    SceneModel Stage;
    VmdMotion Motion;
    VmdMotion Camera;
    bool MotionLoaded;
    bool CameraLoaded;
    float Frame;

    bool PhysicsOn;
    bool EdgeOn;
    bool GridOn;
    bool ShadowOn;
    bool CameraMotionOn;

    OrbitCamera Orbit;
    QPoint LastMousePosition;
    Qt::MouseButtons ActiveButtons;

    bool GlReady;
    QOpenGLShaderProgram* ModelProgram;
    QOpenGLShaderProgram* EdgeProgram;
    QOpenGLShaderProgram* ShadowProgram;
    QOpenGLShaderProgram* GroundProgram;
    GLuint GroundVao;
    GLuint GroundBuffer;
    GLuint WhiteTexture;
    QVector<GLuint> InternalToons;
    GLuint ShadowFramebuffer;
    GLuint ShadowTexture;
    int ShadowSize;
    int BoneTextureWidth;
};

#endif

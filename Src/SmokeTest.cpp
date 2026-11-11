#include "SmokeTest.h"

#include "MathUtil.h"
#include "ModelRuntime.h"
#include "VmdMotion.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>
#include <QtMath>

namespace
{
bool IsFiniteMatrix(const QMatrix4x4& Matrix)
{
    const float* Data = Matrix.constData();
    for (int I = 0; I < 16; ++I) {
        if (!qIsFinite(Data[I]) || qAbs(Data[I]) > 1e6f) {
            return false;
        }
    }
    return true;
}
}

int RunSmokeTest(const QStringList& Files, int FrameCount)
{
    QTextStream Out(stdout);
    ModelRuntime Runtime;
    VmdMotion Motion;
    VmdMotion Camera;
    bool HasMotion = false;
    QString Error;

    for (const QString& Path : Files) {
        if (Path.endsWith(QStringLiteral(".pmx"), Qt::CaseInsensitive)) {
            if (!Runtime.Load(Path, &Error)) {
                Out << QStringLiteral("模型加载失败：") << Error << Qt::endl;
                return 2;
            }
            const PmxModel& Model = Runtime.Model();
            Out << QStringLiteral("模型：") << Model.Name << QStringLiteral("  顶点 ") << Model.Vertices.size()
                << QStringLiteral("  面 ") << Model.Indices.size() / 3 << QStringLiteral("  材质 ") << Model.Materials.size()
                << QStringLiteral("  骨骼 ") << Model.Bones.size() << QStringLiteral("  表情 ") << Model.Morphs.size()
                << QStringLiteral("  刚体 ") << Model.RigidBodies.size() << QStringLiteral("  关节 ") << Model.Joints.size()
                << Qt::endl;
        }
    }
    for (const QString& Path : Files) {
        if (!Path.endsWith(QStringLiteral(".vmd"), Qt::CaseInsensitive)) {
            continue;
        }
        VmdMotion Loaded;
        if (!Loaded.Load(Path, &Error)) {
            Out << QStringLiteral("动作加载失败：") << Error << Qt::endl;
            return 3;
        }
        Out << QStringLiteral("动作：") << QFileInfo(Path).fileName() << QStringLiteral("  骨骼轨道 ") << Loaded.BoneTracks.size()
            << QStringLiteral("  表情轨道 ") << Loaded.MorphTracks.size() << QStringLiteral("  镜头帧 ")
            << Loaded.CameraKeys.size() << QStringLiteral("  IK 轨道 ") << Loaded.IkTracks.size()
            << QStringLiteral("  最大帧 ") << Loaded.MaxFrame << Qt::endl;
        if (Loaded.HasBodyMotion()) {
            Motion = Loaded;
            HasMotion = true;
        } else if (Loaded.HasCamera()) {
            Camera = Loaded;
        }
    }
    if (!Runtime.IsLoaded()) {
        Out << QStringLiteral("未指定 PMX 模型。") << Qt::endl;
        return 4;
    }
    if (HasMotion) {
        Runtime.SetMotion(&Motion);
    }

    QElapsedTimer Timer;
    Timer.start();
    Runtime.ResetPhysics();
    for (int Frame = 0; Frame <= FrameCount; ++Frame) {
        Runtime.Update(float(Frame), 1.0f / 30.0f, true);
        for (const QMatrix4x4& Skin : Runtime.SkinMatrices()) {
            if (!IsFiniteMatrix(Skin)) {
                Out << QStringLiteral("第 ") << Frame << QStringLiteral(" 帧出现无效的蒙皮矩阵！") << Qt::endl;
                return 5;
            }
        }
        if (!Camera.IsEmpty()) {
            CameraPose Pose;
            Camera.SampleCamera(float(Frame), &Pose);
            if (!qIsFinite(Pose.Distance) || !qIsFinite(Pose.Fov)) {
                Out << QStringLiteral("镜头采样无效！") << Qt::endl;
                return 6;
            }
        }
    }
    qint64 Elapsed = Timer.elapsed();

    // 输出若干骨骼的最终位置，便于人工检查
    const PmxModel& Model = Runtime.Model();
    int Shown = 0;
    for (int I = 0; I < Model.Bones.size() && Shown < 12; ++I) {
        QVector3D Position = Runtime.BonePosition(I);
        Out << QStringLiteral("  骨骼 ") << I << QStringLiteral(" ") << Model.Bones.at(I).Name << QStringLiteral(" : (")
            << QString::number(double(Position.x()), 'f', 3) << QStringLiteral(", ")
            << QString::number(double(Position.y()), 'f', 3) << QStringLiteral(", ")
            << QString::number(double(Position.z()), 'f', 3) << QStringLiteral(")")
            << (Runtime.Bone(I).PhysicsDriven ? QStringLiteral(" [物理]") : QString()) << Qt::endl;
        ++Shown;
    }
    Out << QStringLiteral("冒烟测试通过：") << FrameCount + 1 << QStringLiteral(" 帧，用时 ") << Elapsed
        << QStringLiteral(" 毫秒（含物理）。") << Qt::endl;
    return 0;
}

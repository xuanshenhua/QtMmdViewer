#ifndef MODELRUNTIME_H
#define MODELRUNTIME_H

#include "PhysicsWorld.h"
#include "PmxModel.h"
#include "VmdMotion.h"

#include <QByteArray>
#include <QMatrix4x4>
#include <QVector>

// 材质的当前状态（应用材质表情之后）
struct MaterialState
{
    QVector4D Diffuse;
    QVector3D Specular;
    float SpecularPower;
    QVector3D Ambient;
    QVector4D EdgeColor;
    float EdgeSize;
    QVector4D TextureMul;
    QVector4D TextureAdd;
    QVector4D SphereMul;
    QVector4D SphereAdd;
    QVector4D ToonMul;
    QVector4D ToonAdd;
};

// 骨骼的当前状态
struct BoneState
{
    QVector3D LocalOffset; // 相对父骨骼的初始偏移
    QVector3D AnimTranslation;
    QQuaternion AnimRotation;
    QVector3D MorphTranslation;
    QQuaternion MorphRotation;
    QQuaternion AppendRotation; // 付与（继承）旋转
    QVector3D AppendTranslation; // 付与（继承）平移
    QQuaternion IkRotation;
    QQuaternion BaseAppendRotation;
    QQuaternion FinalRotation;
    QVector3D FinalTranslation;
    QMatrix4x4 Local;
    QMatrix4x4 World;
    bool IkEnabled;
    bool PhysicsDriven;
    QVector<int> Children;
};

// 模型运行时：负责动作采样、表情、骨骼层级、付与、IK、物理与蒙皮矩阵计算。
// 完全在 CPU 上运行，不依赖 OpenGL，因此也可用于无界面的冒烟测试。
class ModelRuntime
{
public:
    ModelRuntime();

    bool Load(const QString& FilePath, QString* ErrorMessage);
    void Clear();
    bool IsLoaded() const { return Loaded; }
    const PmxModel& Model() const { return ModelData; }

    void SetMotion(const VmdMotion* Motion);
    void Update(float Frame, float DeltaSeconds, bool PhysicsEnabled);
    void ResetPhysics() { NeedPhysicsReset = true; }

    const QVector<QMatrix4x4>& SkinMatrices() const { return Skins; }
    const QVector<QVector3D>& Positions() const { return CurrentPositions; }
    const QVector<QVector2D>& Uvs() const { return CurrentUvs; }
    bool TakePositionsDirty();
    bool TakeUvsDirty();
    const QVector<MaterialState>& Materials() const { return CurrentMaterials; }

    int MorphCount() const { return ModelData.Morphs.size(); }
    float MorphWeight(int Index) const;
    float ManualMorphWeight(int Index) const;
    void SetManualMorphWeight(int Index, float Weight);

    int FindBone(const QString& Name) const;
    QVector3D BonePosition(int Index) const;
    const BoneState& Bone(int Index) const { return States.at(Index); }
    const PhysicsWorld& Physics() const { return World; }

private:
    void EvaluateMotion(float Frame);
    void AccumulateMorph(int Index, float Weight, int Depth);
    void ApplyMorphs();
    void UpdateLocal(int Index);
    void UpdateWorldTree(int Index, int Depth);
    void SolveIk(int Index);
    QVector<QMatrix4x4> CollectWorlds() const;

    PmxModel ModelData;
    bool Loaded;
    const VmdMotion* Motion;

    QVector<BoneState> States;
    QVector<int> BeforePhysicsOrder;
    QVector<int> AfterPhysicsOrder;
    QVector<int> RootBones;
    QVector<QMatrix4x4> Skins;

    QVector<QByteArray> BoneKeys; // 骨骼名的 Shift-JIS 编码
    QVector<QByteArray> MorphKeys;
    QVector<const QVector<VmdBoneKey>*> BoneTracks;
    QVector<const QVector<VmdMorphKey>*> MorphTracks;
    QVector<const QVector<VmdIkKey>*> IkTracks;

    QVector<float> MotionWeights;
    QVector<float> ManualWeights;
    QVector<float> EffectiveWeights;
    QVector<float> LastEffectiveWeights;
    QVector<QVector3D> BasePositions;
    QVector<QVector2D> BaseUvs;
    QVector<QVector3D> CurrentPositions;
    QVector<QVector2D> CurrentUvs;
    bool PositionsDirty;
    bool UvsDirty;
    QVector<MaterialState> BaseMaterials;
    QVector<MaterialState> CurrentMaterials;

    PhysicsWorld World;
    bool NeedPhysicsReset;
};

#endif

#include "ModelRuntime.h"

#include "MathUtil.h"
#include "TextCodec.h"

#include <QtMath>

namespace
{
const int MaxIkIterations = 256;
const int MaxTreeDepth = 1024;

QVector4D LerpOne(const QVector4D& Factor, float Weight)
{
    return QVector4D(1, 1, 1, 1) + (Factor - QVector4D(1, 1, 1, 1)) * Weight;
}

QVector3D LerpOne(const QVector3D& Factor, float Weight)
{
    return QVector3D(1, 1, 1) + (Factor - QVector3D(1, 1, 1)) * Weight;
}
}

ModelRuntime::ModelRuntime()
    : Loaded(false), Motion(nullptr), PositionsDirty(false), UvsDirty(false), NeedPhysicsReset(true)
{
}

void ModelRuntime::Clear()
{
    ModelData.Clear();
    Loaded = false;
    Motion = nullptr;
    States.clear();
    BeforePhysicsOrder.clear();
    AfterPhysicsOrder.clear();
    RootBones.clear();
    Skins.clear();
    BoneKeys.clear();
    MorphKeys.clear();
    BoneTracks.clear();
    MorphTracks.clear();
    IkTracks.clear();
    MotionWeights.clear();
    ManualWeights.clear();
    EffectiveWeights.clear();
    LastEffectiveWeights.clear();
    BasePositions.clear();
    BaseUvs.clear();
    CurrentPositions.clear();
    CurrentUvs.clear();
    BaseMaterials.clear();
    CurrentMaterials.clear();
    World.Clear();
    NeedPhysicsReset = true;
}

bool ModelRuntime::Load(const QString& FilePath, QString* ErrorMessage)
{
    Clear();
    if (!ModelData.Load(FilePath, ErrorMessage)) {
        ModelData.Clear();
        return false;
    }
    const int BoneCount = ModelData.Bones.size();
    States.resize(BoneCount);
    for (int I = 0; I < BoneCount; ++I) {
        const PmxBone& Bone = ModelData.Bones.at(I);
        BoneState& State = States[I];
        int Parent = Bone.ParentIndex;
        if (Parent >= 0 && Parent < BoneCount && Parent != I) {
            State.LocalOffset = Bone.Position - ModelData.Bones.at(Parent).Position;
            States[Parent].Children.append(I);
        } else {
            ModelData.Bones[I].ParentIndex = -1;
            State.LocalOffset = Bone.Position;
            RootBones.append(I);
        }
        State.IkEnabled = true;
        State.PhysicsDriven = false;
        BoneKeys.append(TextCodec::EncodeShiftJis(Bone.Name));
    }

    // 计算变形顺序：按（物理后变形，变形阶层，索引）排序（插入排序，骨骼数量通常不大）
    QVector<int> Order;
    for (int I = 0; I < BoneCount; ++I) {
        Order.append(I);
    }
    auto Less = [this](int A, int B) -> bool {
        const PmxBone& BoneA = ModelData.Bones.at(A);
        const PmxBone& BoneB = ModelData.Bones.at(B);
        if (BoneA.Layer != BoneB.Layer) {
            return BoneA.Layer < BoneB.Layer;
        }
        return A < B;
    };
    for (int I = 1; I < Order.size(); ++I) {
        int Value = Order.at(I);
        int J = I - 1;
        while (J >= 0 && Less(Value, Order.at(J))) {
            Order[J + 1] = Order.at(J);
            --J;
        }
        Order[J + 1] = Value;
    }
    for (int Index : Order) {
        if (ModelData.Bones.at(Index).Flags & BoneFlagAfterPhysics) {
            AfterPhysicsOrder.append(Index);
        } else {
            BeforePhysicsOrder.append(Index);
        }
    }

    for (const PmxMorph& Morph : ModelData.Morphs) {
        MorphKeys.append(TextCodec::EncodeShiftJis(Morph.Name));
    }
    const int MorphTotal = ModelData.Morphs.size();
    MotionWeights.fill(0.0f, MorphTotal);
    ManualWeights.fill(0.0f, MorphTotal);
    EffectiveWeights.fill(0.0f, MorphTotal);
    LastEffectiveWeights.fill(0.0f, MorphTotal);

    for (const PmxVertex& Vertex : ModelData.Vertices) {
        BasePositions.append(Vertex.Position);
        BaseUvs.append(Vertex.Uv);
    }
    CurrentPositions = BasePositions;
    CurrentUvs = BaseUvs;
    PositionsDirty = true;
    UvsDirty = true;

    for (const PmxMaterial& Material : ModelData.Materials) {
        MaterialState State;
        State.Diffuse = Material.Diffuse;
        State.Specular = Material.Specular;
        State.SpecularPower = Material.SpecularPower;
        State.Ambient = Material.Ambient;
        State.EdgeColor = Material.EdgeColor;
        State.EdgeSize = Material.EdgeSize;
        State.TextureMul = State.SphereMul = State.ToonMul = QVector4D(1, 1, 1, 1);
        State.TextureAdd = State.SphereAdd = State.ToonAdd = QVector4D(0, 0, 0, 0);
        BaseMaterials.append(State);
    }
    CurrentMaterials = BaseMaterials;

    Skins.resize(BoneCount);
    World.Build(ModelData);
    Loaded = true;
    SetMotion(nullptr);
    Update(0.0f, 0.0f, false);
    return true;
}

void ModelRuntime::SetMotion(const VmdMotion* NewMotion)
{
    Motion = NewMotion;
    const int BoneCount = ModelData.Bones.size();
    BoneTracks.fill(nullptr, BoneCount);
    IkTracks.fill(nullptr, BoneCount);
    MorphTracks.fill(nullptr, ModelData.Morphs.size());
    if (!Motion) {
        MotionWeights.fill(0.0f);
        return;
    }
    // VMD 中骨骼/表情名称最长 15 字节，IK 名称最长 20 字节
    for (int I = 0; I < BoneCount; ++I) {
        auto BoneIt = Motion->BoneTracks.constFind(BoneKeys.at(I).left(15));
        if (BoneIt != Motion->BoneTracks.constEnd()) {
            BoneTracks[I] = &BoneIt.value();
        }
        auto IkIt = Motion->IkTracks.constFind(BoneKeys.at(I).left(20));
        if (IkIt != Motion->IkTracks.constEnd()) {
            IkTracks[I] = &IkIt.value();
        }
    }
    for (int I = 0; I < ModelData.Morphs.size(); ++I) {
        auto MorphIt = Motion->MorphTracks.constFind(MorphKeys.at(I).left(15));
        if (MorphIt != Motion->MorphTracks.constEnd()) {
            MorphTracks[I] = &MorphIt.value();
        }
    }
}

float ModelRuntime::MorphWeight(int Index) const
{
    return (Index >= 0 && Index < EffectiveWeights.size()) ? EffectiveWeights.at(Index) : 0.0f;
}

float ModelRuntime::ManualMorphWeight(int Index) const
{
    return (Index >= 0 && Index < ManualWeights.size()) ? ManualWeights.at(Index) : 0.0f;
}

void ModelRuntime::SetManualMorphWeight(int Index, float Weight)
{
    if (Index >= 0 && Index < ManualWeights.size()) {
        ManualWeights[Index] = MathUtil::Clamp(Weight, 0.0f, 1.0f);
    }
}

int ModelRuntime::FindBone(const QString& Name) const
{
    for (int I = 0; I < ModelData.Bones.size(); ++I) {
        if (ModelData.Bones.at(I).Name == Name) {
            return I;
        }
    }
    return -1;
}

QVector3D ModelRuntime::BonePosition(int Index) const
{
    if (Index < 0 || Index >= States.size()) {
        return QVector3D();
    }
    return MathUtil::Translation(States.at(Index).World);
}

bool ModelRuntime::TakePositionsDirty()
{
    bool Result = PositionsDirty;
    PositionsDirty = false;
    return Result;
}

bool ModelRuntime::TakeUvsDirty()
{
    bool Result = UvsDirty;
    UvsDirty = false;
    return Result;
}

void ModelRuntime::EvaluateMotion(float Frame)
{
    for (int I = 0; I < States.size(); ++I) {
        BoneState& State = States[I];
        if (I < BoneTracks.size() && BoneTracks.at(I)) {
            VmdMotion::SampleBone(*BoneTracks.at(I), Frame, &State.AnimTranslation, &State.AnimRotation);
        } else {
            State.AnimTranslation = QVector3D();
            State.AnimRotation = QQuaternion();
        }
        State.IkEnabled = (I < IkTracks.size() && IkTracks.at(I)) ? VmdMotion::SampleIk(*IkTracks.at(I), Frame) : true;
        State.MorphTranslation = QVector3D();
        State.MorphRotation = QQuaternion();
        State.IkRotation = QQuaternion();
        State.AppendRotation = QQuaternion();
        State.AppendTranslation = QVector3D();
        State.PhysicsDriven = false;
    }
    for (int I = 0; I < MotionWeights.size(); ++I) {
        MotionWeights[I] = (I < MorphTracks.size() && MorphTracks.at(I)) ? VmdMotion::SampleMorph(*MorphTracks.at(I), Frame) : 0.0f;
    }
}

void ModelRuntime::AccumulateMorph(int Index, float Weight, int Depth)
{
    if (Index < 0 || Index >= ModelData.Morphs.size() || Depth > 8) {
        return;
    }
    const PmxMorph& Morph = ModelData.Morphs.at(Index);
    if (Morph.Type == MorphTypeGroup || Morph.Type == MorphTypeFlip) {
        // 组合表情与翻转表情：展开为子表情（翻转表情简化为组合表情）
        for (const PmxGroupMorphOffset& Item : Morph.GroupOffsets) {
            AccumulateMorph(Item.MorphIndex, Weight * Item.Weight, Depth + 1);
        }
        return;
    }
    EffectiveWeights[Index] += Weight;
}

void ModelRuntime::ApplyMorphs()
{
    const int Count = ModelData.Morphs.size();
    EffectiveWeights.fill(0.0f);
    for (int I = 0; I < Count; ++I) {
        float Weight = qMax(MotionWeights.at(I), ManualWeights.at(I));
        if (Weight != 0.0f) {
            AccumulateMorph(I, Weight, 0);
        }
    }

    bool VertexChanged = false;
    bool UvChanged = false;
    for (int I = 0; I < Count; ++I) {
        if (qAbs(EffectiveWeights.at(I) - LastEffectiveWeights.at(I)) < 1e-6f) {
            continue;
        }
        int Type = ModelData.Morphs.at(I).Type;
        if (Type == MorphTypeVertex) {
            VertexChanged = true;
        } else if (Type == MorphTypeUv) {
            UvChanged = true;
        }
    }

    // 顶点表情
    if (VertexChanged) {
        CurrentPositions = BasePositions;
        for (int I = 0; I < Count; ++I) {
            const PmxMorph& Morph = ModelData.Morphs.at(I);
            float Weight = EffectiveWeights.at(I);
            if (Morph.Type != MorphTypeVertex || Weight == 0.0f) {
                continue;
            }
            for (const PmxVertexMorphOffset& Item : Morph.VertexOffsets) {
                if (Item.VertexIndex >= 0 && Item.VertexIndex < CurrentPositions.size()) {
                    CurrentPositions[Item.VertexIndex] += Item.Offset * Weight;
                }
            }
        }
        PositionsDirty = true;
    }

    // UV 表情（仅基础 UV）
    if (UvChanged) {
        CurrentUvs = BaseUvs;
        for (int I = 0; I < Count; ++I) {
            const PmxMorph& Morph = ModelData.Morphs.at(I);
            float Weight = EffectiveWeights.at(I);
            if (Morph.Type != MorphTypeUv || Weight == 0.0f) {
                continue;
            }
            for (const PmxUvMorphOffset& Item : Morph.UvOffsets) {
                if (Item.VertexIndex >= 0 && Item.VertexIndex < CurrentUvs.size()) {
                    CurrentUvs[Item.VertexIndex] += QVector2D(Item.Offset.x(), Item.Offset.y()) * Weight;
                }
            }
        }
        UvsDirty = true;
    }

    // 骨骼表情与材质表情
    CurrentMaterials = BaseMaterials;
    for (int I = 0; I < Count; ++I) {
        const PmxMorph& Morph = ModelData.Morphs.at(I);
        float Weight = EffectiveWeights.at(I);
        if (Weight == 0.0f) {
            continue;
        }
        if (Morph.Type == MorphTypeBone) {
            for (const PmxBoneMorphOffset& Item : Morph.BoneOffsets) {
                if (Item.BoneIndex < 0 || Item.BoneIndex >= States.size()) {
                    continue;
                }
                BoneState& State = States[Item.BoneIndex];
                State.MorphTranslation += Item.Translation * Weight;
                State.MorphRotation = State.MorphRotation * MathUtil::WeightedRotation(Item.Rotation, Weight);
            }
        } else if (Morph.Type == MorphTypeMaterial) {
            for (const PmxMaterialMorphOffset& Item : Morph.MaterialOffsets) {
                int First = Item.MaterialIndex;
                int Last = Item.MaterialIndex;
                if (Item.MaterialIndex < 0) {
                    First = 0;
                    Last = CurrentMaterials.size() - 1;
                }
                for (int M = qMax(First, 0); M <= Last && M < CurrentMaterials.size(); ++M) {
                    MaterialState& State = CurrentMaterials[M];
                    if (Item.Operation == 0) {
                        State.Diffuse *= LerpOne(Item.Diffuse, Weight);
                        State.Specular *= LerpOne(Item.Specular, Weight);
                        State.SpecularPower *= 1.0f + (Item.SpecularPower - 1.0f) * Weight;
                        State.Ambient *= LerpOne(Item.Ambient, Weight);
                        State.EdgeColor *= LerpOne(Item.EdgeColor, Weight);
                        State.EdgeSize *= 1.0f + (Item.EdgeSize - 1.0f) * Weight;
                        State.TextureMul *= LerpOne(Item.TextureTint, Weight);
                        State.SphereMul *= LerpOne(Item.SphereTint, Weight);
                        State.ToonMul *= LerpOne(Item.ToonTint, Weight);
                    } else {
                        State.Diffuse += Item.Diffuse * Weight;
                        State.Specular += Item.Specular * Weight;
                        State.SpecularPower += Item.SpecularPower * Weight;
                        State.Ambient += Item.Ambient * Weight;
                        State.EdgeColor += Item.EdgeColor * Weight;
                        State.EdgeSize += Item.EdgeSize * Weight;
                        State.TextureAdd += Item.TextureTint * Weight;
                        State.SphereAdd += Item.SphereTint * Weight;
                        State.ToonAdd += Item.ToonTint * Weight;
                    }
                }
            }
        }
    }
    LastEffectiveWeights = EffectiveWeights;
}

void ModelRuntime::UpdateLocal(int Index)
{
    const PmxBone& Bone = ModelData.Bones.at(Index);
    BoneState& State = States[Index];
    QQuaternion Base = State.AnimRotation * State.MorphRotation;
    QVector3D Translation = State.AnimTranslation + State.MorphTranslation;
    State.AppendRotation = QQuaternion();
    State.AppendTranslation = QVector3D();
    int Source = Bone.InheritParentIndex;
    if (Source >= 0 && Source < States.size() && Source != Index) {
        const PmxBone& SourceBone = ModelData.Bones.at(Source);
        const BoneState& SourceState = States.at(Source);
        if (Bone.Flags & BoneFlagInheritRotation) {
            QQuaternion SourceRotation = (SourceBone.Flags & BoneFlagInheritRotation)
                ? SourceState.AppendRotation
                : SourceState.AnimRotation * SourceState.MorphRotation;
            SourceRotation = SourceState.IkRotation * SourceRotation;
            State.AppendRotation = MathUtil::WeightedRotation(SourceRotation, Bone.InheritWeight);
        }
        if (Bone.Flags & BoneFlagInheritTranslation) {
            QVector3D SourceTranslation = (SourceBone.Flags & BoneFlagInheritTranslation)
                ? SourceState.AppendTranslation
                : SourceState.AnimTranslation + SourceState.MorphTranslation;
            State.AppendTranslation = SourceTranslation * Bone.InheritWeight;
        }
    }
    State.BaseAppendRotation = Base * State.AppendRotation;
    State.FinalRotation = (State.IkRotation * State.BaseAppendRotation).normalized();
    State.FinalTranslation = Translation + State.AppendTranslation;
    State.Local = MathUtil::Compose(State.LocalOffset + State.FinalTranslation, State.FinalRotation);
}

void ModelRuntime::UpdateWorldTree(int Index, int Depth)
{
    if (Depth > MaxTreeDepth) {
        return;
    }
    BoneState& State = States[Index];
    if (!State.PhysicsDriven) {
        int Parent = ModelData.Bones.at(Index).ParentIndex;
        State.World = Parent >= 0 ? States.at(Parent).World * State.Local : State.Local;
    }
    for (int Child : State.Children) {
        UpdateWorldTree(Child, Depth + 1);
    }
}

void ModelRuntime::SolveIk(int Index)
{
    const PmxBone& Bone = ModelData.Bones.at(Index);
    if (!States.at(Index).IkEnabled || Bone.IkTargetIndex < 0 || Bone.IkTargetIndex >= States.size()
        || Bone.IkLinks.isEmpty()) {
        return;
    }
    const int Target = Bone.IkTargetIndex;
    const QVector3D IkPosition = MathUtil::Translation(States.at(Index).World);
    const int Loops = qBound(1, Bone.IkLoopCount, MaxIkIterations);
    const float LimitAngle = Bone.IkLimitAngle;

    for (int Iteration = 0; Iteration < Loops; ++Iteration) {
        for (int LinkIndex = 0; LinkIndex < Bone.IkLinks.size(); ++LinkIndex) {
            const PmxIkLink& Link = Bone.IkLinks.at(LinkIndex);
            const int LinkBone = Link.BoneIndex;
            if (LinkBone < 0 || LinkBone >= States.size() || LinkBone == Target) {
                continue;
            }
            BoneState& LinkState = States[LinkBone];
            QMatrix4x4 Inverse = LinkState.World.inverted();
            QVector3D TargetPosition = MathUtil::Translation(States.at(Target).World);
            QVector3D V1 = Inverse.map(TargetPosition).normalized();
            QVector3D V2 = Inverse.map(IkPosition).normalized();
            float Dot = MathUtil::Clamp(QVector3D::dotProduct(V1, V2), -1.0f, 1.0f);
            float Angle = qAcos(Dot);
            if (Angle < 1e-5f) {
                continue;
            }
            if (LimitAngle > 0.0f) {
                Angle = qMin(Angle, LimitAngle);
            }
            QVector3D Axis = QVector3D::crossProduct(V1, V2);
            if (Axis.lengthSquared() < 1e-12f) {
                continue;
            }
            Axis.normalize();
            bool XAxisOnly = Link.HasLimit
                && qAbs(Link.LimitMin.y()) < 1e-6f && qAbs(Link.LimitMax.y()) < 1e-6f
                && qAbs(Link.LimitMin.z()) < 1e-6f && qAbs(Link.LimitMax.z()) < 1e-6f;
            if (XAxisOnly) {
                // 膝盖类关节：只允许绕 X 轴旋转
                Axis = QVector3D(Axis.x() >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
            }
            QQuaternion NewRotation = LinkState.FinalRotation * MathUtil::FromAxisRadians(Axis, Angle);
            if (Link.HasLimit) {
                QVector3D Angles = MathUtil::DecomposeXyz(NewRotation);
                Angles = QVector3D(MathUtil::Clamp(Angles.x(), Link.LimitMin.x(), Link.LimitMax.x()),
                                   MathUtil::Clamp(Angles.y(), Link.LimitMin.y(), Link.LimitMax.y()),
                                   MathUtil::Clamp(Angles.z(), Link.LimitMin.z(), Link.LimitMax.z()));
                NewRotation = MathUtil::ComposeXyz(Angles);
            }
            LinkState.IkRotation = (NewRotation * LinkState.BaseAppendRotation.inverted()).normalized();
            UpdateLocal(LinkBone);
            UpdateWorldTree(LinkBone, 0);
        }
        QVector3D TargetPosition = MathUtil::Translation(States.at(Target).World);
        if ((TargetPosition - IkPosition).lengthSquared() < 1e-8f) {
            break;
        }
    }
}

QVector<QMatrix4x4> ModelRuntime::CollectWorlds() const
{
    QVector<QMatrix4x4> Result;
    Result.reserve(States.size());
    for (const BoneState& State : States) {
        Result.append(State.World);
    }
    return Result;
}

void ModelRuntime::Update(float Frame, float DeltaSeconds, bool PhysicsEnabled)
{
    if (!Loaded) {
        return;
    }
    // 1. 动作采样
    EvaluateMotion(Frame);
    // 2. 表情
    ApplyMorphs();

    // 3. 物理前变形：局部变换 -> 全局变换 -> 付与 -> IK
    for (int Index : BeforePhysicsOrder) {
        UpdateLocal(Index);
    }
    for (int Index : AfterPhysicsOrder) {
        UpdateLocal(Index);
    }
    for (int Root : RootBones) {
        UpdateWorldTree(Root, 0);
    }
    for (int Index : BeforePhysicsOrder) {
        const PmxBone& Bone = ModelData.Bones.at(Index);
        if (Bone.Flags & (BoneFlagInheritRotation | BoneFlagInheritTranslation)) {
            UpdateLocal(Index);
            UpdateWorldTree(Index, 0);
        }
        if (Bone.Flags & BoneFlagIk) {
            SolveIk(Index);
        }
    }

    // 4. 物理模拟
    if (PhysicsEnabled && !World.IsEmpty()) {
        QVector<QMatrix4x4> Worlds = CollectWorlds();
        if (NeedPhysicsReset) {
            World.Reset(Worlds);
            NeedPhysicsReset = false;
        }
        World.Step(DeltaSeconds, Worlds);
        QVector<bool> Driven(States.size(), false);
        World.ApplyToBones(&Worlds, &Driven);
        for (int I = 0; I < States.size(); ++I) {
            if (Driven.at(I)) {
                States[I].World = Worlds.at(I);
                States[I].PhysicsDriven = true;
            }
        }
        // 刷新不受物理驱动的子骨骼
        for (int Root : RootBones) {
            UpdateWorldTree(Root, 0);
        }
    } else {
        NeedPhysicsReset = true;
    }

    // 5. 物理后变形
    for (int Index : AfterPhysicsOrder) {
        const PmxBone& Bone = ModelData.Bones.at(Index);
        UpdateLocal(Index);
        UpdateWorldTree(Index, 0);
        if (Bone.Flags & BoneFlagIk) {
            SolveIk(Index);
        }
    }

    // 6. 蒙皮矩阵 = 世界矩阵 * 初始位置的逆平移
    for (int I = 0; I < States.size(); ++I) {
        QMatrix4x4 Skin = States.at(I).World;
        Skin.translate(-ModelData.Bones.at(I).Position);
        Skins[I] = Skin;
    }
}

#include "PhysicsWorld.h"

#include "MathUtil.h"
#include "PmxModel.h"

#include <QtMath>

namespace
{
const float FixedStep = 1.0f / 60.0f;
const int MaxStepsPerUpdate = 3;
const float MaxLinearSpeed = 400.0f;
const float MaxAngularSpeed = 60.0f;

quint64 PairKey(int A, int B)
{
    if (A > B) {
        int Temp = A;
        A = B;
        B = Temp;
    }
    return (quint64(quint32(A)) << 32) | quint64(quint32(B));
}

QVector3D ClampVector(const QVector3D& Value, float MaxLength)
{
    float Length = Value.length();
    if (Length > MaxLength && Length > 0.0f) {
        return Value * (MaxLength / Length);
    }
    return Value;
}

// 上下限颠倒（Min > Max）时视为该轴不受限制
float ClampAxis(float Value, float Min, float Max)
{
    if (Min > Max) {
        return Value;
    }
    return MathUtil::Clamp(Value, Min, Max);
}
}

PhysicsWorld::PhysicsWorld()
    : Gravity(0.0f, -98.0f, 0.0f), SubSteps(4), SolverIterations(2), Accumulator(0.0f), HasKinematicHistory(false)
{
}

void PhysicsWorld::Clear()
{
    Bodies.clear();
    Joints.clear();
    JointPairs.clear();
    Accumulator = 0.0f;
    HasKinematicHistory = false;
}

void PhysicsWorld::Build(const PmxModel& Model)
{
    Clear();
    const int BoneCount = Model.Bones.size();
    for (const PmxRigidBody& Source : Model.RigidBodies) {
        PhysicsBody Body;
        Body.BoneIndex = (Source.BoneIndex >= 0 && Source.BoneIndex < BoneCount) ? Source.BoneIndex : -1;
        Body.Mode = Source.Mode;
        Body.Shape = Source.Shape;
        Body.Size = Source.Size;
        Body.LinearDamping = MathUtil::Clamp(Source.LinearDamping, 0.0f, 0.999f);
        Body.AngularDamping = MathUtil::Clamp(Source.AngularDamping, 0.0f, 0.999f);
        Body.GroupBit = 1 << qBound(0, Source.Group, 15);
        Body.CollisionMask = Source.CollisionMask;

        // 碰撞胶囊近似
        if (Source.Shape == 0) {
            Body.Radius = qMax(Source.Size.x(), 0.01f);
            Body.HalfLength = 0.0f;
            Body.LocalAxis = QVector3D(0, 1, 0);
        } else if (Source.Shape == 2) {
            Body.Radius = qMax(Source.Size.x(), 0.01f);
            Body.HalfLength = qMax(Source.Size.y() * 0.5f, 0.0f);
            Body.LocalAxis = QVector3D(0, 1, 0);
        } else {
            QVector3D Half(qAbs(Source.Size.x()), qAbs(Source.Size.y()), qAbs(Source.Size.z()));
            int Axis = 0;
            if (Half.y() >= Half.x() && Half.y() >= Half.z()) {
                Axis = 1;
            } else if (Half.z() >= Half.x() && Half.z() >= Half.y()) {
                Axis = 2;
            }
            float Long = Half[Axis];
            float Other1 = Half[(Axis + 1) % 3];
            float Other2 = Half[(Axis + 2) % 3];
            Body.Radius = qMax((Other1 + Other2) * 0.5f, 0.01f);
            Body.HalfLength = qMax(Long - Body.Radius, 0.0f);
            Body.LocalAxis = QVector3D(Axis == 0 ? 1 : 0, Axis == 1 ? 1 : 0, Axis == 2 ? 1 : 0);
        }

        // 质量与转动惯量（标量近似）
        float Mass = Source.Mass > 0.0f ? Source.Mass : 1.0f;
        float Extent = Body.Radius + Body.HalfLength;
        float Inertia = Mass * (0.4f * Body.Radius * Body.Radius + Extent * Extent / 3.0f);
        Inertia = qMax(Inertia, 1e-4f);
        if (Body.Mode == 0) {
            Body.InvMass = 0.0f;
            Body.InvInertia = 0.0f;
        } else {
            Body.InvMass = 1.0f / Mass;
            Body.InvInertia = 1.0f / Inertia;
        }

        // 刚体初始世界变换与骨骼初始变换（骨骼初始无旋转）
        QQuaternion Rotation = MathUtil::ComposeYxz(Source.Rotation);
        QMatrix4x4 BodyRest = MathUtil::Compose(Source.Position, Rotation);
        QMatrix4x4 BoneRest;
        if (Body.BoneIndex >= 0) {
            BoneRest.translate(Model.Bones.at(Body.BoneIndex).Position);
        }
        Body.OffsetFromBone = BoneRest.inverted() * BodyRest;
        Body.BoneFromBody = Body.OffsetFromBone.inverted();
        Body.Position = Source.Position;
        Body.Rotation = Rotation;
        Body.PreviousPosition = Body.Position;
        Body.PreviousRotation = Body.Rotation;
        Body.KinematicStartPosition = Body.Position;
        Body.KinematicEndPosition = Body.Position;
        Body.KinematicStartRotation = Body.Rotation;
        Body.KinematicEndRotation = Body.Rotation;
        Bodies.append(Body);
    }

    for (const PmxJoint& Source : Model.Joints) {
        if (Source.RigidBodyA < 0 || Source.RigidBodyB < 0 || Source.RigidBodyA >= Bodies.size()
            || Source.RigidBodyB >= Bodies.size() || Source.RigidBodyA == Source.RigidBodyB) {
            continue;
        }
        PhysicsJoint Joint;
        Joint.BodyA = Source.RigidBodyA;
        Joint.BodyB = Source.RigidBodyB;
        const PhysicsBody& A = Bodies.at(Joint.BodyA);
        const PhysicsBody& B = Bodies.at(Joint.BodyB);
        QQuaternion JointRotation = MathUtil::ComposeYxz(Source.Rotation);
        Joint.AnchorA = A.Rotation.conjugated().rotatedVector(Source.Position - A.Position);
        Joint.AnchorB = B.Rotation.conjugated().rotatedVector(Source.Position - B.Position);
        Joint.FrameA = (A.Rotation.conjugated() * JointRotation).normalized();
        Joint.FrameB = (B.Rotation.conjugated() * JointRotation).normalized();
        Joint.LinearMin = Source.LinearMin;
        Joint.LinearMax = Source.LinearMax;
        Joint.AngularMin = Source.AngularMin;
        Joint.AngularMax = Source.AngularMax;
        Joint.LinearSpring = Source.LinearSpring;
        Joint.AngularSpring = Source.AngularSpring;
        Joints.append(Joint);
        JointPairs.insert(PairKey(Joint.BodyA, Joint.BodyB));
    }
}

QMatrix4x4 PhysicsWorld::BodyMatrix(int Index) const
{
    const PhysicsBody& Body = Bodies.at(Index);
    return MathUtil::Compose(Body.Position, Body.Rotation);
}

void PhysicsWorld::Reset(const QVector<QMatrix4x4>& BoneWorlds)
{
    for (PhysicsBody& Body : Bodies) {
        if (Body.BoneIndex >= 0 && Body.BoneIndex < BoneWorlds.size()) {
            QMatrix4x4 World = BoneWorlds.at(Body.BoneIndex) * Body.OffsetFromBone;
            Body.Position = MathUtil::Translation(World);
            Body.Rotation = MathUtil::Rotation(World);
        }
        Body.PreviousPosition = Body.Position;
        Body.PreviousRotation = Body.Rotation;
        Body.KinematicStartPosition = Body.Position;
        Body.KinematicEndPosition = Body.Position;
        Body.KinematicStartRotation = Body.Rotation;
        Body.KinematicEndRotation = Body.Rotation;
        Body.Velocity = QVector3D();
        Body.AngularVelocity = QVector3D();
    }
    Accumulator = 0.0f;
    HasKinematicHistory = true;
}

void PhysicsWorld::Step(float DeltaSeconds, const QVector<QMatrix4x4>& BoneWorlds)
{
    if (Bodies.isEmpty()) {
        return;
    }
    if (!HasKinematicHistory) {
        Reset(BoneWorlds);
    }
    // 更新跟随骨骼的目标位置（在子步进之间插值，避免高速运动时穿透）
    for (PhysicsBody& Body : Bodies) {
        Body.KinematicStartPosition = Body.KinematicEndPosition;
        Body.KinematicStartRotation = Body.KinematicEndRotation;
        if (Body.BoneIndex >= 0 && Body.BoneIndex < BoneWorlds.size()) {
            QMatrix4x4 World = BoneWorlds.at(Body.BoneIndex) * Body.OffsetFromBone;
            Body.KinematicEndPosition = MathUtil::Translation(World);
            Body.KinematicEndRotation = MathUtil::Rotation(World);
        }
    }

    Accumulator += qBound(0.0f, DeltaSeconds, 0.25f);
    int Steps = int(Accumulator / FixedStep);
    if (Steps > MaxStepsPerUpdate) {
        Steps = MaxStepsPerUpdate;
        Accumulator = 0.0f;
    } else {
        Accumulator -= float(Steps) * FixedStep;
    }
    if (Steps <= 0) {
        return;
    }
    const int Total = Steps * SubSteps;
    const float SubStep = FixedStep / float(SubSteps);
    for (int I = 0; I < Total; ++I) {
        Simulate(SubStep, float(I + 1) / float(Total));
    }
}

float PhysicsWorld::EffectiveInvMass(const PhysicsBody& Body)
{
    // 模式 2 的刚体位置由骨骼决定，只允许旋转
    return Body.Mode == 2 ? 0.0f : Body.InvMass;
}

void PhysicsWorld::RotateBody(PhysicsBody& Body, const QVector3D& Theta)
{
    QQuaternion Delta(0.0f, Theta * 0.5f);
    Body.Rotation = (Body.Rotation + Delta * Body.Rotation).normalized();
}

void PhysicsWorld::Simulate(float Step, float Blend)
{
    // 1. 预测：运动学刚体插值到目标，动态刚体做半隐式积分
    for (PhysicsBody& Body : Bodies) {
        QVector3D KinematicPosition = Body.KinematicStartPosition
            + (Body.KinematicEndPosition - Body.KinematicStartPosition) * Blend;
        QQuaternion KinematicRotation = QQuaternion::slerp(Body.KinematicStartRotation, Body.KinematicEndRotation, Blend);
        Body.PreviousPosition = Body.Position;
        Body.PreviousRotation = Body.Rotation;
        if (Body.Mode == 0 || Body.InvMass <= 0.0f) {
            Body.Position = KinematicPosition;
            Body.Rotation = KinematicRotation;
            continue;
        }
        Body.Velocity += Gravity * Step;
        Body.Velocity *= qPow(1.0f - Body.LinearDamping, Step);
        Body.AngularVelocity *= qPow(1.0f - Body.AngularDamping, Step);
        Body.Position += Body.Velocity * Step;
        RotateBody(Body, Body.AngularVelocity * Step);
        if (Body.Mode == 2) {
            Body.Position = KinematicPosition;
        }
    }

    // 2. 约束求解
    for (int Iteration = 0; Iteration < SolverIterations; ++Iteration) {
        for (PhysicsJoint& Joint : Joints) {
            SolveJoint(Joint, Step);
        }
        SolveCollisions();
    }

    // 3. 由位置变化反推速度
    for (PhysicsBody& Body : Bodies) {
        if (Body.Mode == 0 || Body.InvMass <= 0.0f) {
            Body.Velocity = QVector3D();
            Body.AngularVelocity = QVector3D();
            continue;
        }
        Body.Velocity = ClampVector((Body.Position - Body.PreviousPosition) / Step, MaxLinearSpeed);
        QQuaternion Delta = Body.Rotation * Body.PreviousRotation.conjugated();
        QVector3D Omega = Delta.vector() * (2.0f / Step);
        if (Delta.scalar() < 0.0f) {
            Omega = -Omega;
        }
        Body.AngularVelocity = ClampVector(Omega, MaxAngularSpeed);
        if (Body.Mode == 2) {
            Body.Velocity = QVector3D();
        }
    }
}

void PhysicsWorld::ApplyPositionalCorrection(PhysicsBody& A, PhysicsBody& B, const QVector3D& Correction,
                                             const QVector3D& ArmA, const QVector3D& ArmB, float Stiffness)
{
    float C = Correction.length();
    if (C < 1e-6f) {
        return;
    }
    QVector3D N = Correction / C;
    float InvMassA = EffectiveInvMass(A);
    float InvMassB = EffectiveInvMass(B);
    float WeightA = InvMassA + A.InvInertia * QVector3D::crossProduct(ArmA, N).lengthSquared();
    float WeightB = InvMassB + B.InvInertia * QVector3D::crossProduct(ArmB, N).lengthSquared();
    float Weight = WeightA + WeightB;
    if (Weight <= 1e-8f) {
        return;
    }
    QVector3D Impulse = N * (C / Weight * Stiffness);
    A.Position += Impulse * InvMassA;
    B.Position -= Impulse * InvMassB;
    if (A.InvInertia > 0.0f) {
        RotateBody(A, QVector3D::crossProduct(ArmA, Impulse) * A.InvInertia);
    }
    if (B.InvInertia > 0.0f) {
        RotateBody(B, -QVector3D::crossProduct(ArmB, Impulse) * B.InvInertia);
    }
}

void PhysicsWorld::ApplyAngularCorrection(PhysicsBody& A, PhysicsBody& B, const QVector3D& Theta, float Stiffness)
{
    float Weight = A.InvInertia + B.InvInertia;
    if (Weight <= 1e-8f) {
        return;
    }
    if (A.InvInertia > 0.0f) {
        RotateBody(A, -Theta * (A.InvInertia / Weight * Stiffness));
    }
    if (B.InvInertia > 0.0f) {
        RotateBody(B, Theta * (B.InvInertia / Weight * Stiffness));
    }
}

void PhysicsWorld::SolveJoint(PhysicsJoint& Joint, float Step)
{
    PhysicsBody& A = Bodies[Joint.BodyA];
    PhysicsBody& B = Bodies[Joint.BodyB];
    if (A.InvInertia <= 0.0f && B.InvInertia <= 0.0f && EffectiveInvMass(A) <= 0.0f && EffectiveInvMass(B) <= 0.0f) {
        return;
    }

    // ---- 位置约束：锚点在关节坐标系中被限制在 [LinearMin, LinearMax] 内 ----
    {
        QQuaternion FrameRotationA = A.Rotation * Joint.FrameA;
        QVector3D ArmA = A.Rotation.rotatedVector(Joint.AnchorA);
        QVector3D ArmB = B.Rotation.rotatedVector(Joint.AnchorB);
        QVector3D PointA = A.Position + ArmA;
        QVector3D PointB = B.Position + ArmB;
        QVector3D Local = FrameRotationA.conjugated().rotatedVector(PointB - PointA);
        QVector3D Clamped(ClampAxis(Local.x(), Joint.LinearMin.x(), Joint.LinearMax.x()),
                          ClampAxis(Local.y(), Joint.LinearMin.y(), Joint.LinearMax.y()),
                          ClampAxis(Local.z(), Joint.LinearMin.z(), Joint.LinearMax.z()));
        QVector3D Error = FrameRotationA.rotatedVector(Local - Clamped);
        ApplyPositionalCorrection(A, B, Error, ArmA, ArmB, 1.0f);

        // 位置弹簧：把剩余偏移拉回初始位置
        float Spring = (Joint.LinearSpring.x() + Joint.LinearSpring.y() + Joint.LinearSpring.z()) / 3.0f;
        if (Spring > 0.0f && Clamped.lengthSquared() > 1e-10f) {
            float Fraction = MathUtil::Clamp(Spring * Step * Step * (EffectiveInvMass(A) + EffectiveInvMass(B)), 0.0f, 1.0f);
            ApplyPositionalCorrection(A, B, FrameRotationA.rotatedVector(Clamped), ArmA, ArmB, Fraction);
        }
    }

    // ---- 角度约束：相对旋转分解为 XYZ 欧拉角后限制在 [AngularMin, AngularMax] 内 ----
    {
        QQuaternion FrameRotationA = A.Rotation * Joint.FrameA;
        QQuaternion FrameRotationB = B.Rotation * Joint.FrameB;
        QQuaternion Relative = FrameRotationA.conjugated() * FrameRotationB;
        QVector3D Angles = MathUtil::DecomposeXyz(Relative);
        QVector3D Clamped(ClampAxis(Angles.x(), Joint.AngularMin.x(), Joint.AngularMax.x()),
                          ClampAxis(Angles.y(), Joint.AngularMin.y(), Joint.AngularMax.y()),
                          ClampAxis(Angles.z(), Joint.AngularMin.z(), Joint.AngularMax.z()));
        if ((Clamped - Angles).lengthSquared() > 1e-10f) {
            QQuaternion Target = FrameRotationA * MathUtil::ComposeXyz(Clamped);
            QVector3D Theta = MathUtil::ToAxisAngleVector(Target * FrameRotationB.conjugated());
            ApplyAngularCorrection(A, B, Theta, 1.0f);
        }

        // 旋转弹簧：拉回初始相对朝向
        float Spring = (Joint.AngularSpring.x() + Joint.AngularSpring.y() + Joint.AngularSpring.z()) / 3.0f;
        if (Spring > 0.0f) {
            FrameRotationA = A.Rotation * Joint.FrameA;
            FrameRotationB = B.Rotation * Joint.FrameB;
            float Fraction = MathUtil::Clamp(Spring * Step * Step * (A.InvInertia + B.InvInertia), 0.0f, 1.0f);
            QVector3D Theta = MathUtil::ToAxisAngleVector(FrameRotationA * FrameRotationB.conjugated());
            ApplyAngularCorrection(A, B, Theta, Fraction);
        }
    }
}

void PhysicsWorld::Segment(const PhysicsBody& Body, QVector3D* Start, QVector3D* End) const
{
    QVector3D Axis = Body.Rotation.rotatedVector(Body.LocalAxis) * Body.HalfLength;
    *Start = Body.Position - Axis;
    *End = Body.Position + Axis;
}

void PhysicsWorld::SolveCollisions()
{
    const int Count = Bodies.size();
    for (int I = 0; I < Count; ++I) {
        PhysicsBody& A = Bodies[I];
        bool DynamicA = A.Mode != 0 && A.InvMass > 0.0f;
        for (int J = I + 1; J < Count; ++J) {
            PhysicsBody& B = Bodies[J];
            bool DynamicB = B.Mode != 0 && B.InvMass > 0.0f;
            if (!DynamicA && !DynamicB) {
                continue;
            }
            // 碰撞组过滤：双方的掩码都必须包含对方所在的组
            if (!(A.CollisionMask & B.GroupBit) || !(B.CollisionMask & A.GroupBit)) {
                continue;
            }
            float ReachA = A.Radius + A.HalfLength;
            float ReachB = B.Radius + B.HalfLength;
            float Reach = ReachA + ReachB;
            if ((A.Position - B.Position).lengthSquared() > Reach * Reach) {
                continue;
            }
            if (JointPairs.contains(PairKey(I, J))) {
                continue;
            }
            QVector3D StartA, EndA, StartB, EndB, ClosestA, ClosestB;
            Segment(A, &StartA, &EndA);
            Segment(B, &StartB, &EndB);
            MathUtil::ClosestPointsOnSegments(StartA, EndA, StartB, EndB, &ClosestA, &ClosestB);
            QVector3D Delta = ClosestB - ClosestA;
            float Distance = Delta.length();
            float RadiusSum = A.Radius + B.Radius;
            if (Distance >= RadiusSum) {
                continue;
            }
            QVector3D Normal = Distance > 1e-6f ? Delta / Distance : QVector3D(0, 1, 0);
            float Penetration = RadiusSum - Distance;
            QVector3D ContactA = ClosestA + Normal * A.Radius;
            QVector3D ContactB = ClosestB - Normal * B.Radius;
            // 把 A 沿 -Normal、B 沿 +Normal 推开
            ApplyPositionalCorrection(A, B, -Normal * Penetration, ContactA - A.Position, ContactB - B.Position, 1.0f);
        }
    }
}

void PhysicsWorld::ApplyToBones(QVector<QMatrix4x4>* BoneWorlds, QVector<bool>* Driven) const
{
    for (const PhysicsBody& Body : Bodies) {
        if (Body.Mode == 0 || Body.BoneIndex < 0 || Body.BoneIndex >= BoneWorlds->size()) {
            continue;
        }
        (*BoneWorlds)[Body.BoneIndex] = MathUtil::Compose(Body.Position, Body.Rotation) * Body.BoneFromBody;
        (*Driven)[Body.BoneIndex] = true;
    }
}

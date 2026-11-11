#ifndef PHYSICSWORLD_H
#define PHYSICSWORLD_H

#include <QMatrix4x4>
#include <QQuaternion>
#include <QSet>
#include <QVector>
#include <QVector3D>

class PmxModel;

// 物理刚体运行时状态
struct PhysicsBody
{
    int BoneIndex;
    int Mode; // 0 跟随骨骼，1 物理，2 物理 + 骨骼位置
    int Shape; // 0 球体，1 长方体，2 胶囊
    QVector3D Size;
    float InvMass;
    float InvInertia;
    float LinearDamping;
    float AngularDamping;
    int GroupBit;
    int CollisionMask;
    // 碰撞形状统一近似为胶囊（球体 = 长度为 0 的胶囊）
    float Radius;
    float HalfLength;
    QVector3D LocalAxis;
    QMatrix4x4 OffsetFromBone; // 刚体相对骨骼的变换
    QMatrix4x4 BoneFromBody; // 上者的逆
    QVector3D Position;
    QQuaternion Rotation;
    QVector3D PreviousPosition;
    QQuaternion PreviousRotation;
    QVector3D Velocity;
    QVector3D AngularVelocity;
    QVector3D KinematicStartPosition;
    QVector3D KinematicEndPosition;
    QQuaternion KinematicStartRotation;
    QQuaternion KinematicEndRotation;
};

// 关节运行时数据（锚点与坐标系都保存在各自刚体的局部空间中）
struct PhysicsJoint
{
    int BodyA;
    int BodyB;
    QVector3D AnchorA;
    QVector3D AnchorB;
    QQuaternion FrameA;
    QQuaternion FrameB;
    QVector3D LinearMin;
    QVector3D LinearMax;
    QVector3D AngularMin;
    QVector3D AngularMax;
    QVector3D LinearSpring;
    QVector3D AngularSpring;
};

// 自研的简化物理世界：基于位置的动力学（PBD）+ 子步进。
// 支持刚体跟随骨骼/物理模拟、六自由度弹簧关节（位置与角度限制 + 弹簧）、
// 按碰撞组与掩码过滤的胶囊近似碰撞。不追求精确，只求头发与裙摆稳定自然。
class PhysicsWorld
{
public:
    PhysicsWorld();

    void Build(const PmxModel& Model);
    void Clear();
    bool IsEmpty() const { return Bodies.isEmpty(); }
    int BodyCount() const { return Bodies.size(); }
    int JointCount() const { return Joints.size(); }

    // 将所有刚体放回骨骼对应的位置并清零速度
    void Reset(const QVector<QMatrix4x4>& BoneWorlds);
    // 推进模拟（内部以固定 1/60 秒步长运行）
    void Step(float DeltaSeconds, const QVector<QMatrix4x4>& BoneWorlds);
    // 把物理刚体的结果写回骨骼世界矩阵，Driven 标记被物理驱动的骨骼
    void ApplyToBones(QVector<QMatrix4x4>* BoneWorlds, QVector<bool>* Driven) const;

    const QVector<PhysicsBody>& BodyList() const { return Bodies; }
    QMatrix4x4 BodyMatrix(int Index) const;

    QVector3D Gravity;
    int SubSteps;
    int SolverIterations;

private:
    void Simulate(float Step, float Blend);
    void SolveJoint(PhysicsJoint& Joint, float Step);
    void SolveCollisions();
    void ApplyPositionalCorrection(PhysicsBody& A, PhysicsBody& B, const QVector3D& Correction,
                                   const QVector3D& ArmA, const QVector3D& ArmB, float Stiffness);
    void ApplyAngularCorrection(PhysicsBody& A, PhysicsBody& B, const QVector3D& Theta, float Stiffness);
    static void RotateBody(PhysicsBody& Body, const QVector3D& Theta);
    static float EffectiveInvMass(const PhysicsBody& Body);
    void Segment(const PhysicsBody& Body, QVector3D* Start, QVector3D* End) const;

    QVector<PhysicsBody> Bodies;
    QVector<PhysicsJoint> Joints;
    QSet<quint64> JointPairs;
    float Accumulator;
    bool HasKinematicHistory;
};

#endif

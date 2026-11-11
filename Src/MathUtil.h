#ifndef MATHUTIL_H
#define MATHUTIL_H

#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>

// 数学工具：只依赖 Qt 的向量、四元数与矩阵类型
class MathUtil
{
public:
    // 以弧度构造绕轴旋转（Qt 自带接口使用角度）
    static QQuaternion FromAxisRadians(const QVector3D& Axis, float Radians);
    // 欧拉角（弧度）按 R = Rx * Ry * Rz 的顺序分解 / 合成
    static QVector3D DecomposeXyz(const QQuaternion& Rotation);
    static QQuaternion ComposeXyz(const QVector3D& Angles);
    // 欧拉角（弧度）按 R = Ry * Rx * Rz 的顺序合成（PMX 刚体与关节使用）
    static QQuaternion ComposeYxz(const QVector3D& Angles);
    // 从单位四元数到 Rotation 按权重插值，支持负权重（取逆旋转）
    static QQuaternion WeightedRotation(const QQuaternion& Rotation, float Weight);
    // 四元数转换为“旋转轴 * 旋转角”向量
    static QVector3D ToAxisAngleVector(const QQuaternion& Rotation);
    static QVector3D Translation(const QMatrix4x4& Matrix);
    static QQuaternion Rotation(const QMatrix4x4& Matrix);
    static QMatrix4x4 Compose(const QVector3D& Translation, const QQuaternion& Rotation);
    static float Clamp(float Value, float Low, float High);
    // 求两条线段之间的最近点对
    static void ClosestPointsOnSegments(const QVector3D& P1, const QVector3D& Q1,
                                        const QVector3D& P2, const QVector3D& Q2,
                                        QVector3D* ClosestA, QVector3D* ClosestB);
};

#endif

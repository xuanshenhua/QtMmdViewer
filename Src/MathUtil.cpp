#include "MathUtil.h"

#include <QGenericMatrix>
#include <QtMath>

QQuaternion MathUtil::FromAxisRadians(const QVector3D& Axis, float Radians)
{
    float Half = Radians * 0.5f;
    float S = qSin(Half);
    QVector3D N = Axis.normalized();
    return QQuaternion(qCos(Half), N * S);
}

QVector3D MathUtil::DecomposeXyz(const QQuaternion& Rotation)
{
    QMatrix3x3 M = Rotation.normalized().toRotationMatrix();
    float Sy = Clamp(M(0, 2), -1.0f, 1.0f);
    float Y = qAsin(Sy);
    float X;
    float Z;
    if (qAbs(Sy) < 0.9999f) {
        X = qAtan2(-M(1, 2), M(2, 2));
        Z = qAtan2(-M(0, 1), M(0, 0));
    } else {
        // 万向节锁：把绕 Z 的旋转并入 X
        X = qAtan2(M(2, 1), M(1, 1));
        Z = 0.0f;
    }
    return QVector3D(X, Y, Z);
}

QQuaternion MathUtil::ComposeXyz(const QVector3D& Angles)
{
    return FromAxisRadians(QVector3D(1, 0, 0), Angles.x())
        * FromAxisRadians(QVector3D(0, 1, 0), Angles.y())
        * FromAxisRadians(QVector3D(0, 0, 1), Angles.z());
}

QQuaternion MathUtil::ComposeYxz(const QVector3D& Angles)
{
    return FromAxisRadians(QVector3D(0, 1, 0), Angles.y())
        * FromAxisRadians(QVector3D(1, 0, 0), Angles.x())
        * FromAxisRadians(QVector3D(0, 0, 1), Angles.z());
}

QQuaternion MathUtil::WeightedRotation(const QQuaternion& Rotation, float Weight)
{
    if (Weight == 0.0f) {
        return QQuaternion();
    }
    if (Weight < 0.0f) {
        return QQuaternion::slerp(QQuaternion(), Rotation.inverted(), qMin(-Weight, 1.0f));
    }
    if (Weight >= 1.0f) {
        return Rotation;
    }
    return QQuaternion::slerp(QQuaternion(), Rotation, Weight);
}

QVector3D MathUtil::ToAxisAngleVector(const QQuaternion& Rotation)
{
    QQuaternion Q = Rotation.normalized();
    if (Q.scalar() < 0.0f) {
        Q = -Q;
    }
    QVector3D V = Q.vector();
    float Length = V.length();
    if (Length < 1e-7f) {
        return V * 2.0f;
    }
    float Angle = 2.0f * qAtan2(Length, Q.scalar());
    return V / Length * Angle;
}

QVector3D MathUtil::Translation(const QMatrix4x4& Matrix)
{
    return Matrix.column(3).toVector3D();
}

QQuaternion MathUtil::Rotation(const QMatrix4x4& Matrix)
{
    QMatrix3x3 M;
    for (int Row = 0; Row < 3; ++Row) {
        QVector3D Column = Matrix.column(Row).toVector3D().normalized();
        // 去除可能存在的缩放，逐列归一化
        M(0, Row) = Column.x();
        M(1, Row) = Column.y();
        M(2, Row) = Column.z();
    }
    return QQuaternion::fromRotationMatrix(M).normalized();
}

QMatrix4x4 MathUtil::Compose(const QVector3D& Translation, const QQuaternion& Rotation)
{
    QMatrix4x4 Result;
    Result.translate(Translation);
    Result.rotate(Rotation);
    return Result;
}

float MathUtil::Clamp(float Value, float Low, float High)
{
    return Value < Low ? Low : (Value > High ? High : Value);
}

void MathUtil::ClosestPointsOnSegments(const QVector3D& P1, const QVector3D& Q1,
                                       const QVector3D& P2, const QVector3D& Q2,
                                       QVector3D* ClosestA, QVector3D* ClosestB)
{
    QVector3D D1 = Q1 - P1;
    QVector3D D2 = Q2 - P2;
    QVector3D R = P1 - P2;
    float A = QVector3D::dotProduct(D1, D1);
    float E = QVector3D::dotProduct(D2, D2);
    float F = QVector3D::dotProduct(D2, R);
    float S = 0.0f;
    float T = 0.0f;
    const float Epsilon = 1e-8f;
    if (A <= Epsilon && E <= Epsilon) {
        *ClosestA = P1;
        *ClosestB = P2;
        return;
    }
    if (A <= Epsilon) {
        T = Clamp(F / E, 0.0f, 1.0f);
    } else {
        float C = QVector3D::dotProduct(D1, R);
        if (E <= Epsilon) {
            S = Clamp(-C / A, 0.0f, 1.0f);
        } else {
            float B = QVector3D::dotProduct(D1, D2);
            float Denominator = A * E - B * B;
            if (Denominator > Epsilon) {
                S = Clamp((B * F - C * E) / Denominator, 0.0f, 1.0f);
            }
            T = (B * S + F) / E;
            if (T < 0.0f) {
                T = 0.0f;
                S = Clamp(-C / A, 0.0f, 1.0f);
            } else if (T > 1.0f) {
                T = 1.0f;
                S = Clamp((B - C) / A, 0.0f, 1.0f);
            }
        }
    }
    *ClosestA = P1 + D1 * S;
    *ClosestB = P2 + D2 * T;
}

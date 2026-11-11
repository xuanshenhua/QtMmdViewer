#include "OrbitCamera.h"

#include <QtMath>

OrbitCamera::OrbitCamera()
{
    Reset();
}

void OrbitCamera::Reset()
{
    Target = QVector3D(0.0f, 10.0f, 0.0f);
    Yaw = 0.0f;
    Pitch = qDegreesToRadians(5.0f);
    Distance = 45.0f;
    Fov = 30.0f;
}

void OrbitCamera::Rotate(float DeltaX, float DeltaY)
{
    Yaw -= DeltaX * 0.008f;
    Pitch += DeltaY * 0.008f;
    const float Limit = qDegreesToRadians(89.0f);
    Pitch = qBound(-Limit, Pitch, Limit);
}

void OrbitCamera::Pan(float DeltaX, float DeltaY)
{
    QMatrix4x4 View = ViewMatrix();
    QVector3D Right(View(0, 0), View(0, 1), View(0, 2));
    QVector3D Up(View(1, 0), View(1, 1), View(1, 2));
    float Scale = Distance * 0.0015f;
    Target += (-Right * DeltaX + Up * DeltaY) * Scale;
}

void OrbitCamera::Zoom(float Steps)
{
    Distance *= qPow(0.9f, Steps);
    Distance = qBound(1.0f, Distance, 1000.0f);
}

QVector3D OrbitCamera::EyePosition() const
{
    QVector3D Direction(qCos(Pitch) * qSin(Yaw), qSin(Pitch), qCos(Pitch) * qCos(Yaw));
    return Target + Direction * Distance;
}

QMatrix4x4 OrbitCamera::ViewMatrix() const
{
    QMatrix4x4 View;
    View.lookAt(EyePosition(), Target, QVector3D(0.0f, 1.0f, 0.0f));
    return View;
}

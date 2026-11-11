#ifndef ORBITCAMERA_H
#define ORBITCAMERA_H

#include <QMatrix4x4>
#include <QVector3D>

// 轨道相机：鼠标左键旋转、右键/中键平移、滚轮缩放
class OrbitCamera
{
public:
    OrbitCamera();

    void Reset();
    void Rotate(float DeltaX, float DeltaY);
    void Pan(float DeltaX, float DeltaY);
    void Zoom(float Steps);

    QVector3D EyePosition() const;
    QMatrix4x4 ViewMatrix() const;

    QVector3D Target;
    float Yaw;
    float Pitch;
    float Distance;
    float Fov;
};

#endif

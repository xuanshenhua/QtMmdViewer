#ifndef BINARYREADER_H
#define BINARYREADER_H

#include <QByteArray>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

// 二进制读取器：以小端字节序从内存缓冲区中顺序读取基础数据类型。
// 读取越界时不会崩溃，而是将状态标记为失败，并返回零值。
class BinaryReader
{
public:
    explicit BinaryReader(const QByteArray& Data);

    bool IsOk() const { return Ok; }
    bool AtEnd() const { return Offset >= Buffer.size(); }
    qint64 Remaining() const { return Buffer.size() - Offset; }

    qint8 ReadI8();
    quint8 ReadU8();
    qint16 ReadI16();
    quint16 ReadU16();
    qint32 ReadI32();
    quint32 ReadU32();
    float ReadF32();
    QVector2D ReadVec2();
    QVector3D ReadVec3();
    QVector4D ReadVec4();
    QByteArray ReadBytes(int Count);
    void Skip(int Count);

    // 读取宽度为 1/2/4 字节的索引；Unsigned 为真时按无符号解析（PMX 顶点索引）
    int ReadIndex(int Size, bool Unsigned);

private:
    bool Ensure(int Count);

    QByteArray Buffer;
    int Offset;
    bool Ok;
};

#endif

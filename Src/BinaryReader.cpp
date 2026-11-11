#include "BinaryReader.h"

BinaryReader::BinaryReader(const QByteArray& Data)
    : Buffer(Data), Offset(0), Ok(true)
{
}

bool BinaryReader::Ensure(int Count)
{
    if (!Ok || Count < 0 || Offset + Count > Buffer.size()) {
        Ok = false;
        return false;
    }
    return true;
}

quint8 BinaryReader::ReadU8()
{
    if (!Ensure(1)) {
        return 0;
    }
    quint8 Value = quint8(Buffer.at(Offset));
    Offset += 1;
    return Value;
}

qint8 BinaryReader::ReadI8()
{
    return qint8(ReadU8());
}

quint16 BinaryReader::ReadU16()
{
    if (!Ensure(2)) {
        return 0;
    }
    const uchar* Data = reinterpret_cast<const uchar*>(Buffer.constData()) + Offset;
    quint16 Value = quint16(Data[0]) | (quint16(Data[1]) << 8);
    Offset += 2;
    return Value;
}

qint16 BinaryReader::ReadI16()
{
    return qint16(ReadU16());
}

quint32 BinaryReader::ReadU32()
{
    if (!Ensure(4)) {
        return 0;
    }
    const uchar* Data = reinterpret_cast<const uchar*>(Buffer.constData()) + Offset;
    quint32 Value = quint32(Data[0]) | (quint32(Data[1]) << 8) | (quint32(Data[2]) << 16) | (quint32(Data[3]) << 24);
    Offset += 4;
    return Value;
}

qint32 BinaryReader::ReadI32()
{
    return qint32(ReadU32());
}

float BinaryReader::ReadF32()
{
    // 使用联合体把 32 位整数按位解释为 IEEE754 单精度浮点数
    union {
        quint32 Integer;
        float Real;
    } Converter;
    Converter.Integer = ReadU32();
    return Converter.Real;
}

QVector2D BinaryReader::ReadVec2()
{
    float X = ReadF32();
    float Y = ReadF32();
    return QVector2D(X, Y);
}

QVector3D BinaryReader::ReadVec3()
{
    float X = ReadF32();
    float Y = ReadF32();
    float Z = ReadF32();
    return QVector3D(X, Y, Z);
}

QVector4D BinaryReader::ReadVec4()
{
    float X = ReadF32();
    float Y = ReadF32();
    float Z = ReadF32();
    float W = ReadF32();
    return QVector4D(X, Y, Z, W);
}

QByteArray BinaryReader::ReadBytes(int Count)
{
    if (!Ensure(Count)) {
        return QByteArray();
    }
    QByteArray Result = Buffer.mid(Offset, Count);
    Offset += Count;
    return Result;
}

void BinaryReader::Skip(int Count)
{
    if (Ensure(Count)) {
        Offset += Count;
    }
}

int BinaryReader::ReadIndex(int Size, bool Unsigned)
{
    switch (Size) {
    case 1:
        return Unsigned ? int(ReadU8()) : int(ReadI8());
    case 2:
        return Unsigned ? int(ReadU16()) : int(ReadI16());
    case 4:
        return ReadI32();
    default:
        Ok = false;
        return -1;
    }
}

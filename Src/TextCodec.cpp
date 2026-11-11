#include "TextCodec.h"

#include <QStringDecoder>
#include <QStringEncoder>

QString TextCodec::DecodeShiftJis(const QByteArray& Bytes)
{
    QStringDecoder Decoder("Shift_JIS");
    if (!Decoder.isValid()) {
        // 当前 Qt 构建不支持 Shift-JIS 时退回 Latin-1，名称可能显示为乱码
        return QString::fromLatin1(Bytes);
    }
    QString Result = Decoder.decode(Bytes);
    return Result;
}

QByteArray TextCodec::EncodeShiftJis(const QString& Text)
{
    QStringEncoder Encoder("Shift_JIS");
    if (!Encoder.isValid()) {
        return Text.toLatin1();
    }
    QByteArray Result = Encoder.encode(Text);
    return Result;
}

QString TextCodec::DecodeUtf16Le(const QByteArray& Bytes)
{
    QStringDecoder Decoder(QStringDecoder::Utf16LE);
    QString Result = Decoder.decode(Bytes);
    return Result;
}

QString TextCodec::DecodeUtf8(const QByteArray& Bytes)
{
    return QString::fromUtf8(Bytes);
}

QByteArray TextCodec::CutAtNull(const QByteArray& Bytes)
{
    int Position = Bytes.indexOf('\0');
    if (Position < 0) {
        return Bytes;
    }
    return Bytes.left(Position);
}

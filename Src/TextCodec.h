#ifndef TEXTCODEC_H
#define TEXTCODEC_H

#include <QByteArray>
#include <QString>

// 文本编码工具：负责 Shift-JIS / UTF-8 / UTF-16LE 与 QString 之间的转换
class TextCodec
{
public:
    static QString DecodeShiftJis(const QByteArray& Bytes);
    static QByteArray EncodeShiftJis(const QString& Text);
    static QString DecodeUtf16Le(const QByteArray& Bytes);
    static QString DecodeUtf8(const QByteArray& Bytes);
    // 截断到第一个 0 字节（VMD 中的定长名称字段）
    static QByteArray CutAtNull(const QByteArray& Bytes);
};

#endif

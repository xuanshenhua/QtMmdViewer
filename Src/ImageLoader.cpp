#include "ImageLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

QImage ImageLoader::Load(const QString& FilePath)
{
    QFile File(FilePath);
    if (!File.open(QIODevice::ReadOnly)) {
        return QImage();
    }
    QByteArray Data = File.readAll();
    File.close();
    if (FilePath.endsWith(QStringLiteral(".tga"), Qt::CaseInsensitive)) {
        QImage Image = DecodeTga(Data);
        if (!Image.isNull()) {
            return Image;
        }
    }
    QImage Image;
    if (Image.loadFromData(Data)) {
        return Image;
    }
    // 扩展名与内容不符时（例如 .png 实际是 TGA）再尝试一次
    return DecodeTga(Data);
}

QImage ImageLoader::DecodeTga(const QByteArray& Data)
{
    if (Data.size() < 18) {
        return QImage();
    }
    const uchar* Bytes = reinterpret_cast<const uchar*>(Data.constData());
    const int IdLength = Bytes[0];
    const int ColorMapType = Bytes[1];
    const int ImageType = Bytes[2];
    const int ColorMapLength = Bytes[5] | (Bytes[6] << 8);
    const int ColorMapDepth = Bytes[7];
    const int Width = Bytes[12] | (Bytes[13] << 8);
    const int Height = Bytes[14] | (Bytes[15] << 8);
    const int Depth = Bytes[16];
    const int Descriptor = Bytes[17];
    const bool Rle = ImageType == 10 || ImageType == 11;
    const bool Gray = ImageType == 3 || ImageType == 11;
    if (!(ImageType == 2 || ImageType == 3 || ImageType == 10 || ImageType == 11) || Width <= 0 || Height <= 0) {
        return QImage();
    }
    if (!(Depth == 8 || Depth == 24 || Depth == 32) || (Gray && Depth != 8) || (!Gray && Depth == 8)) {
        return QImage();
    }
    int Offset = 18 + IdLength;
    if (ColorMapType == 1) {
        Offset += ColorMapLength * ((ColorMapDepth + 7) / 8);
    }
    const int PixelSize = Depth / 8;
    const int PixelCount = Width * Height;
    QByteArray Pixels;
    Pixels.resize(PixelCount * PixelSize);
    uchar* Out = reinterpret_cast<uchar*>(Pixels.data());
    if (!Rle) {
        if (Offset + PixelCount * PixelSize > Data.size()) {
            return QImage();
        }
        for (int I = 0; I < PixelCount * PixelSize; ++I) {
            Out[I] = Bytes[Offset + I];
        }
    } else {
        int Written = 0;
        while (Written < PixelCount) {
            if (Offset >= Data.size()) {
                return QImage();
            }
            int Header = Bytes[Offset++];
            int Run = (Header & 0x7f) + 1;
            if (Header & 0x80) {
                if (Offset + PixelSize > Data.size()) {
                    return QImage();
                }
                for (int K = 0; K < Run && Written < PixelCount; ++K, ++Written) {
                    for (int C = 0; C < PixelSize; ++C) {
                        Out[Written * PixelSize + C] = Bytes[Offset + C];
                    }
                }
                Offset += PixelSize;
            } else {
                if (Offset + Run * PixelSize > Data.size()) {
                    return QImage();
                }
                for (int K = 0; K < Run && Written < PixelCount; ++K, ++Written) {
                    for (int C = 0; C < PixelSize; ++C) {
                        Out[Written * PixelSize + C] = Bytes[Offset + K * PixelSize + C];
                    }
                }
                Offset += Run * PixelSize;
            }
        }
    }
    QImage Image(Width, Height, QImage::Format_ARGB32);
    const bool TopToBottom = (Descriptor & 0x20) != 0;
    for (int Y = 0; Y < Height; ++Y) {
        int SourceRow = TopToBottom ? Y : (Height - 1 - Y);
        QRgb* Line = reinterpret_cast<QRgb*>(Image.scanLine(Y));
        for (int X = 0; X < Width; ++X) {
            const uchar* P = Out + (SourceRow * Width + X) * PixelSize;
            if (Gray) {
                Line[X] = qRgba(P[0], P[0], P[0], 255);
            } else if (PixelSize == 3) {
                Line[X] = qRgba(P[2], P[1], P[0], 255);
            } else {
                Line[X] = qRgba(P[2], P[1], P[0], P[3]);
            }
        }
    }
    return Image;
}

QString ImageLoader::ResolvePath(const QString& BaseDirectory, const QString& RelativePath)
{
    QString Normalized = RelativePath;
    Normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (Normalized.isEmpty()) {
        return QString();
    }
    QString Direct = QDir(BaseDirectory).filePath(Normalized);
    if (QFileInfo::exists(Direct)) {
        return Direct;
    }
    // 逐级进行大小写不敏感的匹配（Linux 文件系统区分大小写）
    QStringList Parts = Normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString Current = BaseDirectory;
    for (const QString& Part : Parts) {
        if (Part == QLatin1String(".")) {
            continue;
        }
        if (Part == QLatin1String("..")) {
            Current = QDir(Current).filePath(QStringLiteral(".."));
            continue;
        }
        QDir Directory(Current);
        QStringList Entries = Directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        QString Match;
        for (const QString& Entry : Entries) {
            if (Entry.compare(Part, Qt::CaseInsensitive) == 0) {
                Match = Entry;
                break;
            }
        }
        if (Match.isEmpty()) {
            return QString();
        }
        Current = Directory.filePath(Match);
    }
    return QFileInfo::exists(Current) ? Current : QString();
}

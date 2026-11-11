#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <QImage>
#include <QString>

// 纹理图像加载：支持 Qt 自带格式（PNG/JPG/BMP 等，.sph/.spa 按内容识别），
// 并自带一个不依赖插件的 TGA 解码器。另外处理 Windows 风格路径与大小写不敏感的查找。
class ImageLoader
{
public:
    static QImage Load(const QString& FilePath);
    static QImage DecodeTga(const QByteArray& Data);
    // 在 BaseDirectory 下解析 PMX 中记录的相对路径（反斜杠、大小写差异）
    static QString ResolvePath(const QString& BaseDirectory, const QString& RelativePath);
};

#endif

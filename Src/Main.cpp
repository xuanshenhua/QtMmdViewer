#include "MainWindow.h"
#include "SmokeTest.h"
#include "ViewerWidget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QImage>
#include <QSurfaceFormat>
#include <QTimer>

namespace
{
bool HasArgument(int Argc, char* Argv[], const char* Name)
{
    for (int I = 1; I < Argc; ++I) {
        if (qstrcmp(Argv[I], Name) == 0) {
            return true;
        }
    }
    return false;
}

void SetupParser(QCommandLineParser* Parser)
{
    Parser->setApplicationDescription(QStringLiteral("Qt MMD 模型查看器"));
    Parser->addHelpOption();
    Parser->addPositionalArgument(QStringLiteral("文件"), QStringLiteral("要打开的 .pmx / .vmd / 音频文件"));
    Parser->addOption(QCommandLineOption(QStringLiteral("smoke-test"), QStringLiteral("无界面冒烟测试，推进指定帧数后退出"), QStringLiteral("帧数")));
    Parser->addOption(QCommandLineOption(QStringLiteral("screenshot"), QStringLiteral("渲染指定帧后保存截图并退出"), QStringLiteral("路径")));
    Parser->addOption(QCommandLineOption(QStringLiteral("frame"), QStringLiteral("截图时的帧号（默认 0）"), QStringLiteral("帧号")));
}
}

// 程序入口（main 为 C++ 规定的入口函数名，不能改为大写）
int main(int argc, char* argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("QtMmdViewer"));
    QCoreApplication::setApplicationName(QStringLiteral("QtMmdViewer"));

    if (HasArgument(argc, argv, "--smoke-test")) {
        QCoreApplication Application(argc, argv);
        QCommandLineParser Parser;
        SetupParser(&Parser);
        Parser.process(Application);
        return RunSmokeTest(Parser.positionalArguments(), Parser.value(QStringLiteral("smoke-test")).toInt());
    }

    // 请求 OpenGL 3.3 核心模式，开启 4 倍多重采样抗锯齿
    QSurfaceFormat Format;
    Format.setRenderableType(QSurfaceFormat::OpenGL);
    Format.setVersion(3, 3);
    Format.setProfile(QSurfaceFormat::CoreProfile);
    Format.setDepthBufferSize(24);
    Format.setStencilBufferSize(8);
    Format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(Format);

    QApplication Application(argc, argv);
    QCommandLineParser Parser;
    SetupParser(&Parser);
    Parser.process(Application);

    MainWindow Window;
    Window.show();
    Window.OpenFiles(Parser.positionalArguments());

    if (Parser.isSet(QStringLiteral("screenshot"))) {
        const QString OutputPath = Parser.value(QStringLiteral("screenshot"));
        const int TargetFrame = Parser.value(QStringLiteral("frame")).toInt();
        QTimer::singleShot(800, &Window, [&Window, OutputPath, TargetFrame]() {
            // 从第 0 帧逐帧推进到目标帧，让物理有机会稳定
            ViewerWidget* Viewer = Window.Viewer();
            Viewer->SetFrame(0.0f, 0.0f, true);
            for (int Frame = 0; Frame <= TargetFrame; ++Frame) {
                Viewer->SetFrame(float(Frame), 1.0f / 30.0f, false);
            }
            Window.SeekToFrame(float(TargetFrame));
            for (int I = 0; I < 30; ++I) {
                Viewer->SetFrame(float(TargetFrame), 1.0f / 30.0f, false);
            }
            QImage Image = Viewer->grabFramebuffer();
            bool Saved = Image.save(OutputPath);
            qInfo("截图%s：%s", Saved ? "已保存" : "保存失败", qPrintable(OutputPath));
            QCoreApplication::exit(Saved ? 0 : 1);
        });
    }
    return Application.exec();
}

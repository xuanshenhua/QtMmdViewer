# Qt MMD 模型查看器 —— qmake 项目文件
# 构建：qmake6 && make（Windows 下可直接用 Qt Creator 打开本文件）

QT += core gui widgets opengl openglwidgets

# 若安装了 Qt Multimedia 模块则启用音频播放，否则自动禁用（程序仍可运行）
qtHaveModule(multimedia) {
    QT += multimedia
    DEFINES += QtMmdHasMultimedia
} else {
    message("未找到 Qt Multimedia 模块，音频播放功能将被禁用。")
}

CONFIG += c++17 warn_on
TEMPLATE = app
TARGET = ModelViewer

# 禁止 Qt 的隐式字符串转换，保证中文字符串都显式使用 UTF-8
DEFINES += QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII

win32 {
    LIBS += -lopengl32
}
unix:!macx {
    LIBS += -lGL
}
msvc {
    QMAKE_CXXFLAGS += /utf-8
}

SOURCES += \
    Src/AudioTrack.cpp \
    Src/BinaryReader.cpp \
    Src/ImageLoader.cpp \
    Src/Main.cpp \
    Src/MainWindow.cpp \
    Src/MathUtil.cpp \
    Src/ModelRuntime.cpp \
    Src/OrbitCamera.cpp \
    Src/PhysicsWorld.cpp \
    Src/PmxModel.cpp \
    Src/SmokeTest.cpp \
    Src/TextCodec.cpp \
    Src/ViewerWidget.cpp \
    Src/VmdMotion.cpp

HEADERS += \
    Src/AudioTrack.h \
    Src/BinaryReader.h \
    Src/ImageLoader.h \
    Src/MainWindow.h \
    Src/MathUtil.h \
    Src/ModelRuntime.h \
    Src/OrbitCamera.h \
    Src/PhysicsWorld.h \
    Src/PmxModel.h \
    Src/SmokeTest.h \
    Src/TextCodec.h \
    Src/ViewerWidget.h \
    Src/VmdMotion.h

RESOURCES += Resources.qrc

DISTFILES += \
    Shaders/Model.vert Shaders/Model.frag \
    Shaders/Edge.vert Shaders/Edge.frag \
    Shaders/Shadow.vert Shaders/Shadow.frag \
    Shaders/Ground.vert Shaders/Ground.frag \
    README.md

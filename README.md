# Qt MMD 模型查看器（QtMmdViewer）

一个使用 **Qt6 C++ + OpenGL 3.3** 编写的桌面版 MMD（MikuMikuDance）模型与动作查看器。

本项目是网页项目 [Phoshco/model-viewer](https://github.com/Phoshco/model-viewer)（TypeScript + Babylon.js + babylon-mmd）的桌面重写版本：
原项目在浏览器中运行，本项目改为原生 Qt 程序，所有功能（模型解析、骨骼、IK、表情、动作插值、物理、渲染）都用 Qt 自带的类重新实现。

## 特点

- **只依赖 Qt6**：Core、Gui、Widgets、OpenGL、OpenGLWidgets，以及可选的 Multimedia（音频）。
- **不使用第三方库**：没有 glm、imgui、Bullet、Havok 等；数学运算使用 `QVector3D`、`QQuaternion`、`QMatrix4x4`。
- **不使用 C++ 标准库**：代码中没有 `std::`，容器与字符串全部使用 `QVector`、`QHash`、`QMap`、`QString`、`QByteArray` 等 Qt 类型。
- **只用 OpenGL**：没有 Vulkan、DirectX、WebGPU。
- **命名规范**：文件名与代码标识符（类、函数、成员变量、局部变量）都采用“每个单词首字母大写、其余小写”的写法，例如 `PmxModel.cpp`、`LoadModel`、`BoneList`、`UpdatePhysics`。
- **qmake 项目**：使用 `ModelViewer.pro`。
- **全中文**：界面、注释、提示信息、文档均为中文。

## 功能

| 模块 | 说明 |
|------|------|
| PMX 模型 | 支持 PMX 2.0 / 2.1：顶点（BDEF1/2/4，SDEF 按 BDEF2 处理，QDEF 按 BDEF4 处理）、材质、纹理（含球面贴图、Toon 贴图）、骨骼、表情、刚体、关节；文本编码 UTF-16LE / UTF-8 |
| VMD 动作 | 骨骼关键帧（贝塞尔插值）、表情关键帧、镜头关键帧、IK 开关关键帧；名称使用 Shift-JIS 精确匹配 |
| 追加动作 | 可把多个 VMD 合并（例如：舞蹈动作 + 口型动作 + 表情/视线动作） |
| 骨骼系统 | 层级变换、变形阶层排序、付与（继承）旋转/平移、CCD IK（含角度限制与膝盖单轴处理）、物理后变形 |
| 表情 | 顶点、UV、骨骼、材质（乘算/加算）、组合表情；翻转表情按组合表情处理；表情面板可手动调节权重 |
| 物理（自研简化版） | 刚体三种模式（跟随骨骼 / 物理 / 物理+骨骼位置）；六自由度弹簧关节（位置与角度限制 + 弹簧）；按碰撞组与掩码过滤的碰撞；基于位置的动力学（PBD）+ 子步进，适合头发与裙摆 |
| 渲染 | GPU 蒙皮（骨骼矩阵存放在浮点纹理中，不受骨骼数量限制）、MMD 风格卡通着色、球面贴图（乘算/加算）、Toon 贴图、高光、描边、半透明、阴影贴图（3×3 PCF）、程序化地面网格 |
| 镜头 | 鼠标轨道相机（左键旋转、右键/中键平移、滚轮缩放），或跟随 VMD 镜头动作 |
| 舞台 | 可额外加载一个 PMX 舞台模型 |
| 音频 | 通过 Qt Multimedia（`QMediaPlayer` + `QAudioOutput`）播放并与动画同步；若编译环境没有 Multimedia 模块则自动禁用 |
| 界面 | 菜单、工具栏、时间轴、帧号显示、播放速度、音量、循环播放、物理/描边/网格/阴影/镜头开关、最近打开的文件、拖放打开、表情面板、模型信息面板 |

## 构建

### Linux（Debian / Ubuntu）

```bash
sudo apt install qt6-base-dev qt6-base-dev-tools libgl-dev
sudo apt install qt6-multimedia-dev   # 可选：音频播放
qmake6
make -j$(nproc)
./ModelViewer
```

也可以在单独的目录中构建：

```bash
mkdir Build && cd Build
qmake6 ../ModelViewer.pro
make -j$(nproc)
```

### Windows

1. 安装 Qt 6（建议 6.5 或更高版本），在安装器中勾选 MSVC 或 MinGW 套件，以及 **Qt Multimedia**（可选）。
2. 用 Qt Creator 打开 `ModelViewer.pro`，选择套件后点击“构建”即可。
3. 或在 “Qt 6 命令提示符” 中执行：`qmake` 然后 `nmake`（MSVC）或 `mingw32-make`（MinGW）。

### macOS

安装 Qt 6 后执行 `qmake6 && make`（macOS 的 OpenGL 已被标记为过时，但 3.3 核心模式仍可使用）。

## 使用方法

1. “文件 → 打开模型” 选择 `.pmx` 文件（贴图需与模型位于相同的相对路径，路径大小写不敏感）。
2. “文件 → 打开动作” 选择舞蹈 `.vmd`；如有口型、表情动作，用 “追加动作” 合并进来。
3. “文件 → 打开镜头动作” 选择镜头 `.vmd`（可在“视图”菜单关闭“使用镜头动作”改用鼠标自由视角）。
4. “文件 → 打开音频” 选择音乐文件，按 **空格** 播放 / 暂停。
5. 也可以把文件直接拖进窗口：第一个 PMX 作为角色、第二个作为舞台，第一个动作 VMD 为主动作、其余动作 VMD 自动追加，只含镜头的 VMD 自动作为镜头动作。

快捷键：空格 播放/暂停，Esc 停止，R 重置视角，Ctrl+O 打开模型，Ctrl+D 打开动作，Ctrl+Shift+D 追加动作，Ctrl+K 打开镜头，Ctrl+U 打开音频，Ctrl+T 打开舞台。

### 命令行

```bash
# 直接打开文件
./ModelViewer 模型.pmx 舞台.pmx 舞蹈.vmd 口型.vmd 镜头.vmd 音乐.wav

# 无界面冒烟测试：加载并逐帧推进 600 帧（含物理），检查蒙皮矩阵是否有效
./ModelViewer --smoke-test 600 模型.pmx 舞蹈.vmd

# 渲染第 300 帧并保存截图后退出（需要可用的 OpenGL 显示环境，例如 xvfb-run）
./ModelViewer --screenshot 截图.png --frame 300 模型.pmx 舞蹈.vmd
```

## 项目结构

```
ModelViewer.pro          qmake 项目文件
Resources.qrc            Qt 资源文件（着色器）
Shaders/
  Model.vert/.frag       模型着色器（GPU 蒙皮 + 卡通着色 + 阴影）
  Edge.vert/.frag        描边着色器
  Shadow.vert/.frag      阴影贴图着色器
  Ground.vert/.frag      地面网格与阴影接收
Src/
  Main.cpp               程序入口与命令行处理
  MainWindow.*           主窗口（菜单、工具栏、时间轴、面板）
  ViewerWidget.*         OpenGL 视口与渲染
  OrbitCamera.*          轨道相机
  PmxModel.*             PMX 模型解析
  VmdMotion.*            VMD 动作解析、贝塞尔插值与采样
  ModelRuntime.*         骨骼、付与、IK、表情、蒙皮矩阵
  PhysicsWorld.*         自研简化物理引擎
  AudioTrack.*           音频播放（Qt Multimedia）
  ImageLoader.*          纹理加载（含自带的 TGA 解码器）与路径解析
  BinaryReader.*         二进制读取器
  TextCodec.*            Shift-JIS / UTF-8 / UTF-16 文本转换
  MathUtil.*             数学工具
  SmokeTest.*            无界面冒烟测试
```

## 坐标系说明

MMD 使用左手坐标系，OpenGL 使用右手坐标系。本项目在读取 PMX / VMD 时统一把 Z 轴取反：
位置与平移的 Z 分量取反，四元数的 X、Y 分量取反，欧拉角的 X、Y 分量取反，并交换三角形绕序。

## 与原项目相比的简化与限制

- **物理为自研简化版**：用 PBD 近似刚体与六自由度弹簧关节，碰撞形状统一近似为胶囊（长方体按最长轴近似），没有摩擦与反弹，效果与 Bullet/Havok 有差异。
- SDEF 按 BDEF2 处理，QDEF 按 BDEF4 处理；PMX 2.1 软体、冲量表情、外部父骨骼、固定轴/局部轴不参与计算。
- 只支持 **PMX**，不支持 PMD；不支持 babylon-mmd 专用的 **.bvmd / .bpmx** 格式，请使用原始的 `.vmd` / `.pmx`。
- 内置 Toon（toon01~toon10）为程序生成的近似色，不是 MMD 自带的原图。
- 纹理支持 Qt 能读取的格式（PNG、JPG、BMP 等）和自带解码的 TGA；DDS 需要额外安装 Qt 图像格式插件。
- 只能同时加载一个角色模型和一个舞台模型。
- 原网页版的角色列表、网址路由、移动端界面等与网页相关的功能已替换为文件打开、拖放与“最近打开的文件”。
- 由于 C++ 与 Qt 的规定，`main`、`initializeGL`、`paintGL`、`resizeGL`、`mousePressEvent` 等函数名必须保持原样，无法改为首字母大写。

## 许可证

MIT。致谢 [Phoshco/model-viewer](https://github.com/Phoshco/model-viewer) 与 [noname0310/babylon-mmd](https://github.com/noname0310/babylon-mmd) 提供的设计参考。本仓库不包含任何模型、动作或音频素材，请自行准备并遵守素材作者的使用条款。

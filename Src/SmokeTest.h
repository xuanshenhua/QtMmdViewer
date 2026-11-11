#ifndef SMOKETEST_H
#define SMOKETEST_H

#include <QStringList>

// 无界面冒烟测试：加载模型与动作，逐帧推进动画与物理，检查结果是否有效（无 NaN 等）。
// 返回进程退出码，0 表示通过。
int RunSmokeTest(const QStringList& Files, int FrameCount);

#endif

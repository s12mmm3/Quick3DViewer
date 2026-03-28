#ifndef DUMPCATCHER_H
#define DUMPCATCHER_H

#include <QString>

namespace UINamespace {
namespace DumpCatcher {
/*!
 * @brief 异常崩溃时自动生成 .dmp 文件, 方便调试，仅支持Windows系统
 * @param appName 进程名字
 */
void initDumpCatcher(QString appName);
}
}

#endif // DUMPCATCHER_H

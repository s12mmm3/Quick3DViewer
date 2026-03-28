#ifndef LOGMGR_H
#define LOGMGR_H

#include "definevaluehelper.h"

#include <QFile>
#include <QMutex>
#include <QObject>

namespace UINamespace {
/**
 * @class LogMgr
 * @brief LogMgr日志管理类
 */
class LogMgr: public QObject
{
    Q_OBJECT
    DEFINE_VALUE(QString, logDirPath, ""); // 日志文件夹绝对路径
    DEFINE_VALUE(QString, logFilePath, ""); // 日志文件绝对路径
    DEFINE_VALUE(QString, allDirPath, ""); // 所有日志文件夹的根路径

public:
    static LogMgr* instance();
    // 日志输出函数
    static void handler(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    // 打开日志文件夹
    Q_INVOKABLE void openLogFolder();
    // 打开日志文件
    Q_INVOKABLE void openLogFile();

    Q_INVOKABLE void openDebug(bool beOpen = false);

    LogMgr();

private:
    void initInternal();
    void handlerInternal(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    // 日志文件后缀
    QString logFileSuffix();

private:
    QMutex m_mtxLog;                // 日志锁
    QFile *m_logFile = Q_NULLPTR;   // 日志文件句柄
};
}

#endif // LOGMGR_H

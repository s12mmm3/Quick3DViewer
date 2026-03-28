#ifndef LOGGER_H
#define LOGGER_H

#include <QElapsedTimer>
#include <QLoggingCategory>

#define LOGGER_NAME CloudMusicHelperLogger

Q_DECLARE_LOGGING_CATEGORY(LOGGER_NAME)

#define DEBUG qCDebug(LOGGER_NAME)
#define INFO qCInfo(LOGGER_NAME)
#define WARNING qCWarning(LOGGER_NAME)
#define CRITICAL qCritical(LOGGER_NAME)

class RunCost
{
public:
    RunCost(QString info) :_info(info) { _time.start(); DEBUG << (_info) << " begin...."; }
    ~RunCost() { DEBUG << (_info) << " cost(milliseconds):" << qint64(_time.elapsed()); }
private:
    QElapsedTimer _time;
    QString _info;
};

#define MethodCost RunCost cost(QString::fromStdString(std::string(__FUNCTION__)+std::string(",L:")+std::to_string(__LINE__)) );

#endif // LOGGER_H

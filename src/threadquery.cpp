#include "threadquery.h"

#include <QtCore/QObject>
#include <QtCore/QCoreApplication>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlRecord>
#include <QMetaObject>

#include "threadquery_p.h"
#include "clogging.h"
#include <utils/spinlocker.h>

namespace RTPTechGroup {
namespace SqlExtension {

ThreadQuery::ThreadQuery(const QString &query, const QSqlDatabase &db): QThread()
{
    m_driverName = db.driverName();
    m_databaseName = db.databaseName();
    m_hostName = db.hostName();
    m_port = db.port();
    m_userName = db.userName();
    m_password = db.password();
    m_precisionPolicy = db.numericalPrecisionPolicy();
    m_forwardOnly = false;
    m_queryText = query;
    m_blockThread = nullptr;
    m_spinlock.lock();

    this->start();
}

ThreadQuery::ThreadQuery(const QSqlDatabase &db): QThread()
{
    m_driverName = db.driverName();
    m_databaseName = db.databaseName();
    m_hostName = db.hostName();
    m_port = db.port();
    m_userName = db.userName();
    m_password = db.password();
    m_precisionPolicy = db.numericalPrecisionPolicy();
    m_forwardOnly = false;
    m_queryText = "";
    m_blockThread = nullptr;
    m_spinlock.lock();

    this->start();
}

ThreadQuery::~ThreadQuery()
{
    QCoreApplication::removePostedEvents(m_queryPrivate);
    QMetaObject::invokeMethod(m_queryPrivate, "deleteLater",
                              Qt::BlockingQueuedConnection);
    this->quit();
    this->wait();
}

void ThreadQuery::setNumericalPrecisionPolicy(
        QSql::NumericalPrecisionPolicy precisionPolicy)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    m_precisionPolicy = precisionPolicy;
    QMetaObject::invokeMethod(
                m_queryPrivate, "setNumericalPrecisionPolicy", Qt::QueuedConnection,
                Q_ARG( QSql::NumericalPrecisionPolicy, precisionPolicy));
}

QSql::NumericalPrecisionPolicy ThreadQuery::numericalPrecisionPolicy()
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    return m_precisionPolicy;
}

void ThreadQuery::setForwardOnly(bool forward)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    m_forwardOnly = forward;
    QMetaObject::invokeMethod(
                m_queryPrivate, "setForwardOnly", Qt::QueuedConnection,
                Q_ARG(bool, m_forwardOnly));
}

bool ThreadQuery::isForwardOnly()
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    return m_forwardOnly;
}

void ThreadQuery::bindValue(const QUuid &queryUuid, const QString &placeholder,
                            const QVariant &val, QSql::ParamType paramType)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
        return;

    m_boundValues.insert(placeholder, val);
    m_boundTypes.insert(placeholder, paramType);

    QMetaObject::invokeMethod(
                m_queryPrivate, "bindValue", Qt::QueuedConnection,
                Q_ARG(QUuid, m_queryUuid),
                Q_ARG(QString, placeholder),
                Q_ARG(QVariant, val),
                Q_ARG(QSql::ParamType, paramType));
}

void ThreadQuery::bindValue(const QString &placeholder, const QVariant &val,
                            QSql::ParamType paramType)
{
    bindValue(QUuid(), placeholder, val, paramType);
}

QVariant ThreadQuery::boundValue(const QString &placeholder)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    return m_boundValues.value(placeholder);
}

QMap<QString, QVariant> ThreadQuery::boundValues()
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    return m_boundValues;
}

void ThreadQuery::prepare(const QString &query, const QUuid &queryUuid)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    m_boundTypes.clear();
    m_boundValues.clear();
    m_queryText = query;

    m_queryUuid = queryUuid;
    m_queryPrivate->setQueryUuid(QUuid::createUuid());
    QMetaObject::invokeMethod(m_queryPrivate, "prepare", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid),
                              Q_ARG(QString, m_queryText));
}

void ThreadQuery::execute(const QString &query, const QUuid &queryUuid)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    m_queryUuid = queryUuid;
    m_queryText = query;
    QMetaObject::invokeMethod(m_queryPrivate, "execute", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid),
                              Q_ARG(QString, m_queryText));
}

void ThreadQuery::execute(const char *query, const QUuid &queryUuid)
{
    execute(QString(query), queryUuid);
}

void ThreadQuery::execute(const QUuid &queryUuid)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    m_queryUuid = queryUuid;
    QMetaObject::invokeMethod(m_queryPrivate, "execute", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid));
}

void ThreadQuery::executeBatch(QSqlQuery::BatchExecutionMode mode, const QUuid &queryUuid)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    m_queryUuid = queryUuid;
    QMetaObject::invokeMethod(m_queryPrivate, "executeBatch", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid),
                              Q_ARG(QSqlQuery::BatchExecutionMode, mode));
}

QString ThreadQuery::lastQuery()
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    return m_queryText;
}

QSqlError ThreadQuery::lastError() const
{
    return m_lastError;
}

void ThreadQuery::begin()
{
    m_spinlock.lock();
    m_blockThread = QThread::currentThread();
}

void ThreadQuery::end()
{
    m_spinlock.unlock();
    m_blockThread = nullptr;
}

void ThreadQuery::first(const QUuid &queryUuid)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
        return;

    QMetaObject::invokeMethod(m_queryPrivate, "first", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid));
}

void ThreadQuery::next(const QUuid &queryUuid)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
        return;

    QMetaObject::invokeMethod(m_queryPrivate, "next", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid));
}

void ThreadQuery::seek(const QUuid &queryUuid, int index, bool relative)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
        return;

    QMetaObject::invokeMethod(m_queryPrivate, "seek", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid),
                              Q_ARG(int, index), Q_ARG(bool, relative));
}

void ThreadQuery::seek(int index, bool relative)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    QMetaObject::invokeMethod(m_queryPrivate, "seek", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid),
                              Q_ARG(int, index), Q_ARG(bool, relative));
}

void ThreadQuery::previous(const QUuid &queryUuid)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
        return;

    QMetaObject::invokeMethod(m_queryPrivate, "previous", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid));
}

void ThreadQuery::last(const QUuid &queryUuid)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
        return;

    QMetaObject::invokeMethod(m_queryPrivate, "last", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid));
}

void ThreadQuery::fetchAll(const QUuid &queryUuid)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
        return;

    QMetaObject::invokeMethod(m_queryPrivate, "fetchAll", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid));
}

void ThreadQuery::fetchSome(int count, const QUuid &queryUuid)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
        return;

    QMetaObject::invokeMethod(m_queryPrivate, "fetchSome", Qt::QueuedConnection,
                              Q_ARG(int, count), Q_ARG(QUuid, m_queryUuid));
}

void ThreadQuery::finish()
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    if (m_queryUuid == ThreadQueryPrivate::FINISH_UUID)
        return;

    QUuid oldUuid = m_queryUuid;
    m_queryUuid = ThreadQueryPrivate::FINISH_UUID;
    m_queryPrivate->setQueryUuid(QUuid::createUuid());    
    QMetaObject::invokeMethod(m_queryPrivate, "finish", Qt::QueuedConnection,
                              Q_ARG(QUuid, oldUuid));
}

void ThreadQuery::fetchOne(const QUuid &queryUuid)
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
        return;

    QMetaObject::invokeMethod(m_queryPrivate, "fetchOne", Qt::QueuedConnection,
                              Q_ARG(QUuid, m_queryUuid));
}

void ThreadQuery::clear()
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);

    QUuid oldUuid = m_queryUuid;
    m_queryUuid = ThreadQueryPrivate::FINISH_UUID;
    m_queryPrivate->setQueryUuid(QUuid::createUuid());

    m_boundTypes.clear();
    m_boundValues.clear();

    QMetaObject::invokeMethod(m_queryPrivate, "clear", Qt::QueuedConnection,
                              Q_ARG(QUuid, oldUuid));
}

void ThreadQuery::transaction()
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    QMetaObject::invokeMethod(m_queryPrivate, "transaction", Qt::QueuedConnection);
}

void ThreadQuery::commit()
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    QMetaObject::invokeMethod(m_queryPrivate, "commit", Qt::QueuedConnection);
}

void ThreadQuery::rollback()
{
    SpinLocker locker((m_blockThread != QThread::currentThread()) ? &m_spinlock : nullptr);
    QMetaObject::invokeMethod(m_queryPrivate, "rollback", Qt::QueuedConnection);
}

void ThreadQuery::run()
{
    m_queryPrivate =  new ThreadQueryPrivate();

    qRegisterMetaType<QSql::ParamType>( "QSql::ParamType" );

    connect(m_queryPrivate, &ThreadQueryPrivate::prepareDone,
            this, &ThreadQuery::prepareDone, Qt::DirectConnection);

    connect(m_queryPrivate, &ThreadQueryPrivate::executeDone,
            this, &ThreadQuery::executeDone, Qt::DirectConnection);

    connect(m_queryPrivate, &ThreadQueryPrivate::changePosition,
            this, &ThreadQuery::pChangePosition, Qt::DirectConnection);

    qRegisterMetaType< QSqlError >( "QSqlError" );
    connect(m_queryPrivate, &ThreadQueryPrivate::error,
            this, &ThreadQuery::pError, Qt::DirectConnection);

    qRegisterMetaType< QList<QSqlRecord> >( "QList<QSqlRecord>" );
    connect(m_queryPrivate, &ThreadQueryPrivate::values,
            this, &ThreadQuery::pValues, Qt::DirectConnection);

    qRegisterMetaType< QSqlRecord >( "QSqlRecord" );
    connect(m_queryPrivate, &ThreadQueryPrivate::value,
            this, &ThreadQuery::pValue, Qt::DirectConnection);

    QMetaObject::invokeMethod(
                m_queryPrivate, "databaseConnect", Qt::QueuedConnection,
                Q_ARG(QString, m_driverName), Q_ARG(QString, m_databaseName),
                Q_ARG(QString, m_hostName),   Q_ARG(int, m_port),
                Q_ARG(QString, m_userName),   Q_ARG(QString, m_password),
                Q_ARG(QString, m_queryText));

    m_spinlock.unlock();

    exec();
}

void ThreadQuery::pChangePosition(const QUuid &queryUuid, int pos)
{
//    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
//        return;

    emit changePosition(queryUuid, pos);
}

void ThreadQuery::pError(const QUuid &queryUuid, QSqlError err)
{
    m_lastError = err;
    emit error(queryUuid, err);

    qCWarning(lcSqlExtension) << err.text();
}

void ThreadQuery::pValues(const QUuid &queryUuid, const QList<QSqlRecord> &records)
{
//    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
//        return;

    emit values(queryUuid, records);
}

void ThreadQuery::pValue(const QUuid &queryUuid, const QSqlRecord &record)
{
//    if (!queryUuid.isNull() && queryUuid != m_queryUuid)
//        return;

    emit value(queryUuid, record);
}

}}

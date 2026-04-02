// Copyright (C) 2025 Signal Slot Inc.
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QMCPSERVERSTREAMABLEHTTP_H
#define QMCPSERVERSTREAMABLEHTTP_H

#include <QtCore/QJsonObject>
#include <QtCore/QScopedPointer>
#include <QtMcpServer/qmcpserverbackendinterface.h>
#include <QtMcpServer/qmcpserverbackendplugin.h>

QT_BEGIN_NAMESPACE

class QMcpServerStreamableHttp : public QMcpServerBackendInterface
{
    Q_OBJECT
    Q_PROPERTY(QString bearerToken READ bearerToken WRITE setBearerToken)
    Q_PROPERTY(int sessionCloseGracePeriodMs READ sessionCloseGracePeriodMs WRITE setSessionCloseGracePeriodMs)
public:
    explicit QMcpServerStreamableHttp(QObject *parent = nullptr);
    ~QMcpServerStreamableHttp() override;

    void setBearerToken(const QString &bearerToken);
    QString bearerToken() const;
    void setSessionCloseGracePeriodMs(int gracePeriodMs);
    int sessionCloseGracePeriodMs() const;
    QString remoteAddress(const QUuid &session) const override;

public slots:
    void start(const QString &server) override;
    void shutdown() override;
    void send(const QUuid &session, const QJsonObject &object) override;
    void notify(const QUuid &session, const QJsonObject &object) override;

private:
    class Private;
    QScopedPointer<Private> d;
};

class QMcpServerStreamableHttpPlugin : public QMcpServerBackendPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QMcpServerBackendPluginFactoryInterface_iid FILE "qmcpserverstreamablehttp.json")
public:
    QMcpServerBackendInterface *create(const QString &key, QObject *parent = nullptr) override
    {
        Q_ASSERT(key == "streamablehttp"_L1);
        return new QMcpServerStreamableHttp(parent);
    }
};

QT_END_NAMESPACE

#endif // QMCPSERVERSTREAMABLEHTTP_H

// Copyright (C) 2025 Signal Slot Inc.
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qmcpserverstreamablehttp.h"
#include "httpserver.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/QUrl>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(lcQMcpServerStreamableHttpPlugin, "qt.mcpserver.plugins.backend.streamablehttp")

namespace {

struct ServerConfig
{
    QHostAddress address = QHostAddress::Any;
    quint16 port = 8000;
    QString endpointPath = "/mcp"_L1;
};

QHostAddress parseAddress(const QString &value)
{
    if (value == "localhost"_L1)
        return QHostAddress(QHostAddress::LocalHost);
    return QHostAddress(value);
}

ServerConfig parseServerConfig(const QString &server)
{
    ServerConfig config;
    if (server.isEmpty())
        return config;

    if (server.contains("://"_L1)) {
        QUrl url(server);
        if (!url.host().isEmpty())
            config.address = parseAddress(url.host());
        if (url.port() > 0)
            config.port = static_cast<quint16>(url.port());
        if (!url.path().isEmpty())
            config.endpointPath = url.path();
        return config;
    }

    const int slash = server.indexOf('/'_L1);
    const QString hostPort = slash >= 0 ? server.left(slash) : server;
    const QString path = slash >= 0 ? server.mid(slash) : QString();
    const int colon = hostPort.indexOf(':'_L1);
    if (colon >= 0) {
        const QString host = hostPort.left(colon);
        const QString port = hostPort.mid(colon + 1);
        if (!host.isEmpty())
            config.address = parseAddress(host);
        if (!port.isEmpty())
            config.port = static_cast<quint16>(port.toUShort());
    } else if (!hostPort.isEmpty()) {
        config.address = parseAddress(hostPort);
    }

    if (!path.isEmpty())
        config.endpointPath = path;

    return config;
}

} // namespace

class QMcpServerStreamableHttp::Private
{
public:
    QTcpServer tcpServer;
    StreamableHttpServer httpServer;
};

QMcpServerStreamableHttp::QMcpServerStreamableHttp(QObject *parent)
    : QMcpServerBackendInterface(parent)
    , d(new Private)
{
    connect(&d->httpServer, &StreamableHttpServer::newSession, this, &QMcpServerStreamableHttp::newSessionStarted);
    connect(&d->httpServer, &StreamableHttpServer::received, this, &QMcpServerStreamableHttp::received);
}

QMcpServerStreamableHttp::~QMcpServerStreamableHttp() = default;

void QMcpServerStreamableHttp::setBearerToken(const QString &bearerToken)
{
    d->httpServer.setBearerToken(bearerToken);
}

QString QMcpServerStreamableHttp::bearerToken() const
{
    return d->httpServer.bearerToken();
}

void QMcpServerStreamableHttp::start(const QString &server)
{
    const auto config = parseServerConfig(server);
    d->httpServer.setEndpointPath(config.endpointPath);

    if (!d->tcpServer.listen(config.address, config.port) || !d->httpServer.bind(&d->tcpServer)) {
        qWarning() << "streamablehttp server start failed:" << server;
        return;
    }

    qCDebug(lcQMcpServerStreamableHttpPlugin)
            << "Listening on" << d->tcpServer.serverAddress()
            << d->tcpServer.serverPort()
            << "path" << d->httpServer.endpointPath();
    emit started();
}

void QMcpServerStreamableHttp::shutdown()
{
    d->tcpServer.close();
    d->httpServer.shutdown();
    emit finished();
}

void QMcpServerStreamableHttp::send(const QUuid &session, const QJsonObject &object)
{
    d->httpServer.send(session, object);
}

void QMcpServerStreamableHttp::notify(const QUuid &session, const QJsonObject &object)
{
    d->httpServer.send(session, object);
}

QT_END_NAMESPACE

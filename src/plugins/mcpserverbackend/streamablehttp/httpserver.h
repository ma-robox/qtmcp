#ifndef STREAMABLEHTTPHTTPSERVER_H
#define STREAMABLEHTTPHTTPSERVER_H

#include <QtCore/QString>
#include <QtCore/QScopedPointer>
#include <QtMcpServer/qmcpabstracthttpserver.h>

class StreamableHttpServer : public QMcpAbstractHttpServer
{
    Q_OBJECT
public:
    explicit StreamableHttpServer(QObject *parent = nullptr);
    ~StreamableHttpServer() override;

    void setEndpointPath(QString endpointPath);
    QString endpointPath() const;
    void setBearerToken(QString bearerToken);
    QString bearerToken() const;
    QString remoteAddress(const QUuid &session) const;

    Q_INVOKABLE QByteArray get(const QNetworkRequest &request);
    Q_INVOKABLE QByteArray post(const QNetworkRequest &request, const QByteArray &body);
    Q_INVOKABLE QByteArray deleteResource(const QNetworkRequest &request);

public slots:
    void send(const QUuid &session, const QJsonObject &object);
    void closeSession(const QUuid &session);
    void shutdown();

signals:
    void newSession(const QUuid &session);
    void sessionClosed(const QUuid &sessionId);
    void received(const QUuid &session, const QJsonObject &object);

private:
    class Private;
    QScopedPointer<Private> d;
};

#endif // STREAMABLEHTTPHTTPSERVER_H

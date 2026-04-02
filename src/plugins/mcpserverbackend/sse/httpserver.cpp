#include "httpserver.h"
#include <QtCore/QUrlQuery>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

class HttpServer::Private{
public:
    QSet<QUuid> sessions;
    QHash<QUuid, QString> remoteAddresses;
};


HttpServer::HttpServer(QObject *parent)
    : QMcpAbstractHttpServer(parent)
    , d(new Private)
{
    connect(this, &QMcpAbstractHttpServer::sseConnectionClosed, this, [this](const QUuid &session) {
        if (!d->sessions.remove(session))
            return;
        d->remoteAddresses.remove(session);
        emit sessionClosed(session);
    });
}

HttpServer::~HttpServer() = default;

QString HttpServer::remoteAddress(const QUuid &session) const
{
    return d->remoteAddresses.value(session);
}

QByteArray HttpServer::getSse(const QNetworkRequest &request)
{
    QByteArray response;
    if (request.hasRawHeader("Accept") && request.rawHeader("Accept") == "text/event-stream") {
        auto uuid = registerSseRequest(request);
        if (!uuid.isNull()) {
            d->sessions.insert(uuid);
            d->remoteAddresses.insert(uuid, peerAddress(request));
            response += "event: endpoint\r\ndata: /messages/?session_id=";
            response += uuid.toByteArray(QUuid::WithoutBraces);
            response += "\r\n\r\n";
            emit newSession(uuid);
        } else {
            qWarning() << uuid << "is empty";
        }
    } else {
        qWarning() << request.headers();
    }
    return response;
}

QByteArray HttpServer::postMessages(const QNetworkRequest &request, const QByteArray &body)
{
    QUrlQuery query(request.url().query());
    const auto session = QUuid::fromString("{"_L1 + query.queryItemValue("session_id") + "}"_L1);
    if (session.isNull()) {
        qWarning() << "session id error" << query.queryItemValue("session_id");
        return QByteArray();
    }
    if (!d->sessions.contains(session)) {
        qWarning() << "missing session id" << session;
        return QByteArray();
    }

    // Parse message body for target client ID
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(body, &error);
    if (error.error == QJsonParseError::NoError && doc.isObject()) {
        emit received(session, doc.object());
    } else {
        qWarning() << body;
        qWarning() << "error parsing message" << error.errorString();
    }
    return "Accept"_ba;
}

void HttpServer::send(const QUuid &session, const QJsonObject &object)
{
    sendSseEvent(session, QJsonDocument(object).toJson(QJsonDocument::Compact), "message"_L1);
}

void HttpServer::shutdown()
{
    const auto sessions = d->sessions.values();
    for (const auto &sessionId : sessions)
        closeSseConnection(sessionId);
    d->sessions.clear();
    d->remoteAddresses.clear();
}

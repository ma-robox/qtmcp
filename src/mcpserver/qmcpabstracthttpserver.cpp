#include "qmcpabstracthttpserver.h"
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHttpHeaders>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QMap>

class QMcpAbstractHttpServer::Private
{
public:
    Private(QMcpAbstractHttpServer *parent);
    void handleNewConnection();
    void handleDisconnected(QTcpSocket *socket);
    void parseHttpRequest(QTcpSocket *socket);
    void resetParseData(QTcpSocket *socket, const QByteArray &remainingData = {});
    void sendHttpResponse(QTcpSocket *socket, const QByteArray &data,
                          const QString &contentType = QStringLiteral("text/plain"),
                          int statusCode = 200,
                          const QHttpHeaders &headers = {});
    static QByteArray statusTextForCode(int statusCode);

private:
    QMcpAbstractHttpServer *q;
public:
    QTcpServer *server = nullptr;
    struct ParseData {
        QByteArray data;
        QNetworkRequest request;
        int indexOfMethod = -1;
        qint64 contentLength = -1;  // Store expected content length
        bool responseDeferred = false;
        bool responseCompleted = false;
    };

    QMap<QTcpSocket*, ParseData> dataMap;
    QMap<QUuid, QTcpSocket*> sessions;
};

QMcpAbstractHttpServer::Private::Private(QMcpAbstractHttpServer *parent)
    : q(parent)
{}

void QMcpAbstractHttpServer::Private::handleNewConnection()
{
    while (QTcpSocket *socket = server->nextPendingConnection()) {
        dataMap.insert(socket, ParseData());
        connect(socket, &QTcpSocket::readyRead, q, [this, socket]() {
            parseHttpRequest(socket);
        });
        connect(socket, &QTcpSocket::disconnected, q, [this, socket]() {
            handleDisconnected(socket);
        });

        if (socket->bytesAvailable() > 0)
            parseHttpRequest(socket);
    }
}

void QMcpAbstractHttpServer::Private::handleDisconnected(QTcpSocket *socket)
{
    if (!socket)
        return;

    for (auto it = sessions.begin(); it != sessions.end(); ) {
        if (it.value() == socket)
            it = sessions.erase(it);
        else
            ++it;
    }

    if (dataMap.contains(socket)) {
        dataMap.remove(socket);
    }

    socket->deleteLater();
}

void QMcpAbstractHttpServer::Private::resetParseData(QTcpSocket *socket, const QByteArray &remainingData)
{
    ParseData next;
    next.data = remainingData;
    dataMap.insert(socket, next);
    if (!remainingData.isEmpty())
        parseHttpRequest(socket);
}

void QMcpAbstractHttpServer::Private::parseHttpRequest(QTcpSocket *socket)
{
    const auto mo = q->metaObject();

    ParseData &data = dataMap[socket];
    QByteArray newData = socket->readAll();
    data.data.append(newData);

    if (!data.request.url().isValid()) {
        const int headersEnd = data.data.indexOf("\r\n\r\n");
        if (headersEnd < 0)
            return;

        int cr = data.data.indexOf('\r');
        int lf = data.data.indexOf('\n');
        if (lf < 0) {
            return;
        }
        if (cr + 1 != lf) {
            qWarning() << cr << lf << data.data;
        }
        // Parse request line
        const QList<QByteArray> requestLine = data.data.left(cr).split(' ');
        if (requestLine.size() < 3) {
            qWarning() << requestLine;
            return;
        }

        QByteArray method = requestLine.at(0).trimmed();
        QByteArray path = requestLine.at(1).trimmed();

        // parse headers
        QHttpHeaders headers;
        int prevLf = 0;
        while (prevLf < headersEnd) {
            prevLf = lf + 1;
            cr = data.data.indexOf('\r', prevLf);
            lf = data.data.indexOf('\n', prevLf);
            if (cr + 1 != lf) {
                qWarning() << cr << lf << prevLf << data.data;
            }
            if (cr == prevLf)
                break;

            QByteArray header = data.data.mid(prevLf, cr - prevLf);
            int colon = header.indexOf(':');
            if (colon < 0) {
                continue;
            }
            headers.append(QString::fromLatin1(header.left(colon)), QString::fromUtf8(header.mid(colon + 1).trimmed()));
        }

        // Extract Content-Length if present
        if (headers.contains("Content-Length"_L1)) {
            bool ok;
            const auto contentLengthStr = headers.value("Content-Length"_L1);
            data.contentLength = contentLengthStr.toLongLong(&ok);
            if (!ok) {
                data.contentLength = -1;  // No valid Content-Length header
            }
        }

        QUrl url;
        int question = path.indexOf('?');
        if (question < 0) {
            url.setPath(QString::fromUtf8(path));
        } else {
            url.setPath(QString::fromUtf8(path.left(question)));
            url.setQuery(QString::fromUtf8(path.mid(question + 1)));
        }

        data.request = QNetworkRequest(url);
        data.request.setAttribute(QNetworkRequest::User, QUuid::createUuid());
        data.request.setHeaders(headers);
        data.data.remove(0, headersEnd + 4);

        QByteArray slotName = method.toLower();
        const auto pathElements = url.path().split("/"_L1, Qt::SkipEmptyParts);
        for (const auto &pe : pathElements) {
            slotName += pe.toUpper().toUtf8().at(0);
            slotName += pe.toUtf8().mid(1).toLower();
        }

        for (int i = mo->methodOffset(); i < mo->methodCount(); i++) {
            if (mo->method(i).name() == slotName) {
                data.indexOfMethod = i;
                break;
            }
        }

        if (data.indexOfMethod < 0) {
            const QByteArray fallbackSlotName =
                    method.toLower() == "delete"_ba ? "deleteResource"_ba : method.toLower();
            for (int i = mo->methodOffset(); i < mo->methodCount(); i++) {
                if (mo->method(i).name() == fallbackSlotName) {
                    data.indexOfMethod = i;
                    break;
                }
            }
        }
    }

    // Check if we have received all expected data
    if (data.contentLength >= 0 && data.data.size() < data.contentLength) {
        return;  // Wait for more data
    }

    if (data.indexOfMethod < 0) {
        sendHttpResponse(socket, "Not Found"_ba, QStringLiteral("text/plain"), 404);
        return;
    }

    const QByteArray requestBody = data.contentLength >= 0 ? data.data.left(data.contentLength) : data.data;
    const QByteArray remainingData = data.contentLength >= 0 ? data.data.mid(data.contentLength) : QByteArray();

    auto mm = mo->method(data.indexOfMethod);
    QByteArray ret;
    switch (mm.parameterCount()) {
    case 0:
        mm.invoke(q
                  , Qt::DirectConnection
                  , Q_RETURN_ARG(QByteArray, ret)
                  );
        break;
    case 1:
        mm.invoke(q
                  , Qt::DirectConnection
                  , Q_RETURN_ARG(QByteArray, ret)
                  , Q_ARG(QNetworkRequest, data.request)
                  );
        break;
    case 2:
        mm.invoke(q
                  , Qt::DirectConnection
                  , Q_RETURN_ARG(QByteArray, ret)
                  , Q_ARG(QNetworkRequest, data.request)
                  , Q_ARG(QByteArray, requestBody)
                  );
        break;
    default:
        qFatal();
    }
    if (sessions.key(socket).isNull()) {
        if (!data.responseDeferred)
            sendHttpResponse(socket, ret, "text/plain"_L1, 200);
    } else {
        socket->write(ret);
    }

    if (!data.responseDeferred || data.responseCompleted)
        resetParseData(socket, remainingData);
}

void QMcpAbstractHttpServer::Private::sendHttpResponse(QTcpSocket *socket, const QByteArray &data,
                                                       const QString &contentType,
                                                       int statusCode,
                                                       const QHttpHeaders &headers)
{
    const QByteArray statusText = statusTextForCode(statusCode);
    QByteArray response = QString(u"HTTP/1.1 %1 %2\r\n"
                          "Content-Type: %3\r\n"
                          "Content-Length: %4\r\n")
                          .arg(statusCode)
                          .arg(QString::fromLatin1(statusText))
                          .arg(contentType)
                          .arg(data.size())
                          .toLatin1();
    for (qsizetype i = 0; i < headers.size(); ++i) {
        //response += headers.nameAt(i).toLatin1();
        response += headers.nameAt(i);
        response += ": ";
        //response += headers.valueAt(i).toLatin1();
        response += headers.valueAt(i);
        response += "\r\n";
    }
    response += "\r\n"_ba;
    response += data;
    socket->write(response);
    socket->flush();
}

QByteArray QMcpAbstractHttpServer::Private::statusTextForCode(int statusCode)
{
    switch (statusCode) {
    case 200:
        return "OK"_ba;
    case 202:
        return "Accepted"_ba;
    case 204:
        return "No Content"_ba;
    case 400:
        return "Bad Request"_ba;
    case 404:
        return "Not Found"_ba;
    case 405:
        return "Method Not Allowed"_ba;
    case 406:
        return "Not Acceptable"_ba;
    case 415:
        return "Unsupported Media Type"_ba;
    case 500:
        return "Internal Server Error"_ba;
    default:
        return "OK"_ba;
    }
}

QMcpAbstractHttpServer::QMcpAbstractHttpServer(QObject *parent)
    : QObject{parent}
    , d(new Private(this))
{}

QMcpAbstractHttpServer::~QMcpAbstractHttpServer()
{}

bool QMcpAbstractHttpServer::bind(QTcpServer *server)
{
    static QMetaObject::Connection connection;

    if (d->server) {
        disconnect(connection);
        connection = QMetaObject::Connection();
        // Clean up any existing connections
        const auto sockets = d->dataMap.keys();
        for (QTcpSocket *socket : sockets) {
            socket->disconnect();
            socket->deleteLater();
        }
        d->dataMap.clear();
    }

    d->server = server;
    if (d->server) {
        while (server->hasPendingConnections())
            d->handleNewConnection();
        connection = connect(server, &QTcpServer::newConnection, this, [this]() {
            d->handleNewConnection();
        });
    }
    return true;
}

QUuid QMcpAbstractHttpServer::registerSseRequest(const QNetworkRequest &request)
{
    QUuid ret;
    static QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
                                            "Content-Type: text/event-stream\r\n"
                                            "Cache-Control: no-cache\r\n"
                                            "Connection: keep-alive\r\n"
                                            "\r\n");
    const auto sockets = d->dataMap.keys();
    for (QTcpSocket *socket : sockets) {
        if (d->dataMap.value(socket).request == request) {
            ret = QUuid::createUuid();
            d->sessions.insert(ret, socket);
            socket->write(response);
            socket->flush();
            break;
        }
    }
    if (ret.isNull())
        qWarning() << "sse socket for" << request.url() << "not found";
    return ret;
}

QUuid QMcpAbstractHttpServer::deferHttpResponse(const QNetworkRequest &request)
{
    const auto requestId = request.attribute(QNetworkRequest::User).toUuid();
    if (requestId.isNull())
        return {};

    const auto sockets = d->dataMap.keys();
    for (QTcpSocket *socket : sockets) {
        auto &parseData = d->dataMap[socket];
        if (parseData.request.attribute(QNetworkRequest::User).toUuid() == requestId) {
            parseData.responseDeferred = true;
            return requestId;
        }
    }

    return {};
}

void QMcpAbstractHttpServer::sendHttpResponse(const QUuid &id,
                                              const QByteArray &data,
                                              const QString &contentType,
                                              int statusCode,
                                              const QHttpHeaders &headers)
{
    const auto sockets = d->dataMap.keys();
    for (QTcpSocket *socket : sockets) {
        const auto requestId = d->dataMap.value(socket).request.attribute(QNetworkRequest::User).toUuid();
        if (requestId != id)
            continue;

        d->sendHttpResponse(socket, data, contentType, statusCode, headers);
        d->dataMap[socket].responseCompleted = true;
        return;
    }

    qWarning() << "http request" << id << "not found";
}

void QMcpAbstractHttpServer::sendSseEvent(const QUuid &id, const QByteArray &data,
                                         const QString &event)
{
    if (!d->sessions.contains(id)) {
        qWarning() << "sse" << id << "not found";
        return;
    }
    auto *socket = d->sessions.value(id);
    QByteArray message;
    if (!event.isEmpty())
        message += "event: " + event.toUtf8() + "\r\n";
    message += "data: " + data + "\r\n\r\n";
    socket->write(message);
    socket->flush();
}

void QMcpAbstractHttpServer::closeSseConnection(const QUuid &id)
{
    if (!d->sessions.contains(id)) {
        qWarning() << "sse" << id << "not found";
        return;
    }
    auto *socket = d->sessions.take(id);
    d->dataMap.remove(socket);
    socket->close();
    socket->deleteLater();
    return;
}

bool QMcpAbstractHttpServer::hasSseConnection(const QUuid &id) const
{
    return d->sessions.contains(id);
}


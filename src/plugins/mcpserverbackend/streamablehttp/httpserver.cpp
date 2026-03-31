#include "httpserver.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonParseError>
#include <QtCore/QLoggingCategory>
#include <QtCore/QDebug>
#include <optional>

Q_LOGGING_CATEGORY(lcQMcpServerStreamableHttpTransport, "qt.mcpserver.plugins.backend.streamablehttp.transport")

#ifdef QT_MCP_STREAMABLEHTTP_VERBOSE_DIAGNOSTICS
#define QT_MCP_STREAMABLEHTTP_VERBOSE 1
#endif

namespace {

constexpr auto kJsonContentType = "application/json";
constexpr auto kSseContentType = "text/event-stream";

QHttpHeaders jsonHeaders(const QUuid &session, std::optional<QtMcp::ProtocolVersion> protocolVersion = std::nullopt)
{
    QHttpHeaders headers;
    headers.append("Cache-Control"_L1, "no-store"_L1);
    if (protocolVersion.has_value())
        headers.append("MCP-Protocol-Version"_L1, QtMcp::protocolVersionToString(*protocolVersion));
    if (!session.isNull())
        headers.append("Mcp-Session-Id"_L1, session.toString(QUuid::WithoutBraces));
    return headers;
}

QByteArray jsonBody(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray jsonBody(const QJsonArray &array)
{
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QJsonObject jsonRpcError(int code, const QString &message, const QJsonValue &id = QJsonValue::Null)
{
    return {
        { "jsonrpc"_L1, "2.0"_L1 },
        { "id"_L1, id },
        { "error"_L1, QJsonObject{
              { "code"_L1, code },
              { "message"_L1, message }
          } }
    };
}

bool acceptsEventStream(const QNetworkRequest &request)
{
    return request.headers().value("Accept"_L1).contains("text/event-stream"_L1);
}

bool isInitializeRequest(const QJsonObject &object)
{
    return object.value("method"_L1).toString() == "initialize"_L1;
}

bool isRequestObject(const QJsonObject &object)
{
    return object.contains("method"_L1) && object.contains("id"_L1);
}

bool isNotificationObject(const QJsonObject &object)
{
    return object.contains("method"_L1) && !object.contains("id"_L1);
}

bool isResponseObject(const QJsonObject &object)
{
    return !object.contains("method"_L1)
            && object.contains("id"_L1)
            && (object.contains("result"_L1) || object.contains("error"_L1));
}

QString requestSummary(const QNetworkRequest &request)
{
    return QStringLiteral("path=")
            + request.url().path()
            + QStringLiteral(" accept=")
            + request.headers().value("Accept"_L1)
            + QStringLiteral(" contentType=")
            + request.headers().value("Content-Type"_L1)
            + QStringLiteral(" protocol=")
            + request.headers().value("MCP-Protocol-Version"_L1)
            + QStringLiteral(" session=")
            + request.headers().value("Mcp-Session-Id"_L1);
}

} // namespace

class StreamableHttpServer::Private
{
public:
    struct SessionState {
        QUuid sessionId;
        bool sseOpen = false;
        QUuid sseConnectionId;
        QList<QJsonObject> queuedMessages;
        std::optional<QtMcp::ProtocolVersion> protocolVersion;
    };

    struct PendingResponse {
        QUuid batchId;
        QUuid sessionId;
    };

    struct PendingBatch {
        QUuid requestId;
        QUuid sessionId;
        QList<QString> responseOrder;
        QHash<QString, QJsonObject> responses;
    };

    QString endpointPath = "/mcp"_L1;
    QHash<QUuid, SessionState> sessions;
    QHash<QUuid, QHash<QString, PendingResponse>> pendingResponses;
    QHash<QUuid, PendingBatch> pendingBatches;

    static QString responseKey(const QJsonValue &id)
    {
        if (id.isString())
            return QStringLiteral("s:") + id.toString();
        if (id.isDouble())
            return QStringLiteral("n:") + QString::number(id.toInteger());
        const QByteArray encoded = QJsonDocument(QJsonArray{ id }).toJson(QJsonDocument::Compact);
        return QStringLiteral("j:") + QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
    }

    QUuid sessionIdFromRequest(const QNetworkRequest &request) const
    {
        const auto value = request.headers().value("Mcp-Session-Id"_L1);
        if (value.isEmpty())
            return {};
        return QUuid::fromString("{"_L1 + value + "}"_L1);
    }

    void clearPendingBatch(const QUuid &batchId)
    {
        if (!pendingBatches.contains(batchId))
            return;

        const auto batch = pendingBatches.take(batchId);
        auto pendingBySession = pendingResponses.value(batch.sessionId);
        for (const auto &key : batch.responseOrder)
            pendingBySession.remove(key);

        if (pendingBySession.isEmpty())
            pendingResponses.remove(batch.sessionId);
        else
            pendingResponses[batch.sessionId] = pendingBySession;
    }
};

StreamableHttpServer::StreamableHttpServer(QObject *parent)
    : QMcpAbstractHttpServer(parent)
    , d(new Private)
{
}

StreamableHttpServer::~StreamableHttpServer() = default;

void StreamableHttpServer::setEndpointPath(QString endpointPath)
{
    if (!endpointPath.startsWith('/'_L1))
        endpointPath.prepend('/'_L1);

    if (endpointPath.size() > 1 && endpointPath.endsWith('/'_L1))
        endpointPath.chop(1);

    d->endpointPath = endpointPath;
}

QString StreamableHttpServer::endpointPath() const
{
    return d->endpointPath;
}

QByteArray StreamableHttpServer::get(const QNetworkRequest &request)
{
#ifdef QT_MCP_STREAMABLEHTTP_VERBOSE
    qCInfo(lcQMcpServerStreamableHttpTransport).noquote()
            << "HTTP GET /mcp" << requestSummary(request);
#endif

    const auto deferredId = deferHttpResponse(request);
    if (deferredId.isNull())
        return {};

    if (request.url().path() != d->endpointPath) {
        sendHttpResponse(deferredId,
                         QByteArrayLiteral("Not Found"),
                         QStringLiteral("text/plain"),
                         404);
        return {};
    }

    if (!acceptsEventStream(request)) {
        sendHttpResponse(deferredId,
                         jsonBody(jsonRpcError(-32000, "GET on the MCP endpoint requires Accept: text/event-stream"_L1)),
                         kJsonContentType,
                         406);
        return {};
    }

    const auto sessionId = d->sessionIdFromRequest(request);
    if (sessionId.isNull() || !d->sessions.contains(sessionId)) {
        // A client may probe GET /mcp before initialize or while negotiating transport.
        // Returning 405 keeps us compliant with the Streamable HTTP transport when no
        // session-bound SSE stream can be opened yet.
#ifdef QT_MCP_STREAMABLEHTTP_VERBOSE
        qCInfo(lcQMcpServerStreamableHttpTransport)
                << "Rejecting pre-session GET with 405" << request.url();
#endif
        sendHttpResponse(deferredId,
                         QByteArray(),
                         kJsonContentType,
                         405);
        return {};
    }

    const auto sseId = registerSseRequest(request);
    if (sseId.isNull()) {
        sendHttpResponse(deferredId,
                         jsonBody(jsonRpcError(-32000, "Unable to open SSE stream"_L1)),
                         kJsonContentType,
                         500);
        return {};
    }

    auto &sessionState = d->sessions[sessionId];
    if (sessionState.sseOpen && !sessionState.sseConnectionId.isNull()
            && sessionState.sseConnectionId != sseId
            && hasSseConnection(sessionState.sseConnectionId)) {
#ifdef QT_MCP_STREAMABLEHTTP_VERBOSE
        qCInfo(lcQMcpServerStreamableHttpTransport)
                << "Replacing existing SSE stream for session" << sessionId
                << "oldConnection" << sessionState.sseConnectionId
                << "newConnection" << sseId;
#endif
        closeSseConnection(sessionState.sseConnectionId);
    }

    sessionState.sseOpen = true;
    sessionState.sseConnectionId = sseId;
    qCInfo(lcQMcpServerStreamableHttpTransport)
            << "Opened SSE stream for session" << sessionId << "connection" << sseId;

    if (!sessionState.queuedMessages.isEmpty()) {
#ifdef QT_MCP_STREAMABLEHTTP_VERBOSE
        qCInfo(lcQMcpServerStreamableHttpTransport)
                << "Flushing queued server-initiated messages for session" << sessionId
                << "count" << sessionState.queuedMessages.size();
#endif
        const auto queuedMessages = sessionState.queuedMessages;
        sessionState.queuedMessages.clear();
        for (const auto &queuedObject : queuedMessages) {
#ifdef QT_MCP_STREAMABLEHTTP_VERBOSE
            qCInfo(lcQMcpServerStreamableHttpTransport).noquote()
                    << "Flushed queued SSE message"
                    << "session=" << sessionId
                    << "payload=" << QString::fromUtf8(jsonBody(queuedObject));
#endif
            sendSseEvent(sseId, jsonBody(queuedObject), "message"_L1);
        }
    }

    return {};
}

QByteArray StreamableHttpServer::post(const QNetworkRequest &request, const QByteArray &body)
{
#ifdef QT_MCP_STREAMABLEHTTP_VERBOSE
    qCInfo(lcQMcpServerStreamableHttpTransport).noquote()
            << "HTTP POST /mcp" << requestSummary(request)
            << "body=" << QString::fromUtf8(body);
#endif

    const auto deferredId = deferHttpResponse(request);
    if (deferredId.isNull())
        return {};

    if (request.url().path() != d->endpointPath) {
        sendHttpResponse(deferredId,
                         QByteArrayLiteral("Not Found"),
                         QStringLiteral("text/plain"),
                         404);
        return {};
    }

    const auto contentType = request.headers().value("Content-Type"_L1);
    if (!contentType.startsWith("application/json"_L1)) {
        sendHttpResponse(deferredId,
                         jsonBody(jsonRpcError(-32000, "POST /mcp requires Content-Type: application/json"_L1)),
                         kJsonContentType,
                         415);
        return {};
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || (!document.isObject() && !document.isArray())) {
        sendHttpResponse(deferredId,
                         jsonBody(jsonRpcError(-32700, "Invalid JSON-RPC payload"_L1)),
                         kJsonContentType,
                         400);
        return {};
    }

    QList<QJsonObject> messages;
    if (document.isObject()) {
        messages.append(document.object());
    } else {
        const auto array = document.array();
        if (array.isEmpty()) {
            sendHttpResponse(deferredId,
                             jsonBody(jsonRpcError(-32600, "JSON-RPC batch must not be empty"_L1)),
                             kJsonContentType,
                             400);
            return {};
        }

        messages.reserve(array.size());
        for (const auto &value : array) {
            if (!value.isObject()) {
                sendHttpResponse(deferredId,
                                 jsonBody(jsonRpcError(-32600, "JSON-RPC batch elements must be objects"_L1)),
                                 kJsonContentType,
                                 400);
                return {};
            }
            messages.append(value.toObject());
        }
    }

    auto sessionId = d->sessionIdFromRequest(request);

    if (sessionId.isNull()) {
        if (messages.size() != 1 || !isInitializeRequest(messages.first())) {
            sendHttpResponse(deferredId,
                             jsonBody(jsonRpcError(-32000, "Mcp-Session-Id header is required after initialize"_L1,
                                                   messages.size() == 1 ? messages.first().value("id"_L1)
                                                                        : QJsonValue::Null)),
                             kJsonContentType,
                             400);
            return {};
        }

        sessionId = QUuid::createUuid();
        d->sessions.insert(sessionId, { sessionId, false, {}, {}, std::nullopt });
        qCInfo(lcQMcpServerStreamableHttpTransport)
                << "Created MCP session" << sessionId;
        emit newSession(sessionId);
    } else if (!d->sessions.contains(sessionId)) {
        sendHttpResponse(deferredId,
                         jsonBody(jsonRpcError(-32000, "Unknown MCP session"_L1,
                                               messages.size() == 1 ? messages.first().value("id"_L1)
                                                                    : QJsonValue::Null)),
                         kJsonContentType,
                         404);
        return {};
    }

    bool hasRequests = false;
    bool hasNotificationsOrResponsesOnly = true;
    QList<QString> requestKeys;
    requestKeys.reserve(messages.size());

    for (const auto &object : std::as_const(messages)) {
        if (isRequestObject(object)) {
            hasRequests = true;
            hasNotificationsOrResponsesOnly = false;

            const auto key = Private::responseKey(object.value("id"_L1));
            if (requestKeys.contains(key)) {
                sendHttpResponse(deferredId,
                                 jsonBody(jsonRpcError(-32600, "Duplicate request ids in JSON-RPC batch"_L1)),
                                 kJsonContentType,
                                 400);
                return {};
            }
            requestKeys.append(key);
            continue;
        }

        if (!isNotificationObject(object) && !isResponseObject(object)) {
            sendHttpResponse(deferredId,
                             jsonBody(jsonRpcError(-32600, "Invalid JSON-RPC message in batch"_L1,
                                                   object.value("id"_L1))),
                             kJsonContentType,
                             400);
            return {};
        }
    }

    if (!hasRequests && hasNotificationsOrResponsesOnly) {
#ifdef QT_MCP_STREAMABLEHTTP_VERBOSE
        qCInfo(lcQMcpServerStreamableHttpTransport)
                << "Dispatching notification/response-only POST for session" << sessionId
                << "messages" << messages.size();
#endif
        sendHttpResponse(deferredId,
                         QByteArray(),
                         kJsonContentType,
                         202,
                         jsonHeaders(sessionId, d->sessions.value(sessionId).protocolVersion));
        for (const auto &object : std::as_const(messages))
            emit received(sessionId, object);
        return {};
    }

    Private::PendingBatch pendingBatch { deferredId, sessionId, requestKeys, {} };
    d->pendingBatches.insert(deferredId, pendingBatch);
    for (const auto &key : requestKeys)
        d->pendingResponses[sessionId].insert(key, { deferredId, sessionId });
#ifdef QT_MCP_STREAMABLEHTTP_VERBOSE
    qCInfo(lcQMcpServerStreamableHttpTransport)
            << "Dispatching request POST for session" << sessionId
            << "messages" << messages.size()
            << "requestIds" << requestKeys;
#endif

    for (const auto &object : std::as_const(messages))
        emit received(sessionId, object);

    return {};
}

QByteArray StreamableHttpServer::deleteResource(const QNetworkRequest &request)
{
    const auto deferredId = deferHttpResponse(request);
    if (deferredId.isNull())
        return {};

    if (request.url().path() != d->endpointPath) {
        sendHttpResponse(deferredId,
                         QByteArrayLiteral("Not Found"),
                         QStringLiteral("text/plain"),
                         404);
        return {};
    }

    const auto sessionId = d->sessionIdFromRequest(request);
    if (sessionId.isNull() || !d->sessions.contains(sessionId)) {
        sendHttpResponse(deferredId,
                         jsonBody(jsonRpcError(-32000, "Missing or unknown Mcp-Session-Id header"_L1)),
                         kJsonContentType,
                         400);
        return {};
    }

    closeSession(sessionId);
    sendHttpResponse(deferredId, QByteArray(), kJsonContentType, 204);
    return {};
}

void StreamableHttpServer::send(const QUuid &session, const QJsonObject &object)
{
    if (d->pendingResponses.contains(session) && object.contains("id"_L1)) {
        auto &pendingById = d->pendingResponses[session];
        const auto key = Private::responseKey(object.value("id"_L1));
        if (pendingById.contains(key)) {
            if (object.contains("result"_L1)) {
                const auto result = object.value("result"_L1).toObject();
                const auto versionString = result.value("protocolVersion"_L1).toString();
                if (!versionString.isEmpty())
                    d->sessions[session].protocolVersion = QtMcp::stringToProtocolVersion(versionString);
            }
            const auto pending = pendingById.take(key);
            auto &batch = d->pendingBatches[pending.batchId];
            batch.responses.insert(key, object);
#ifdef QT_MCP_STREAMABLEHTTP_VERBOSE
            qCInfo(lcQMcpServerStreamableHttpTransport).noquote()
                    << "Matched JSON-RPC response for pending HTTP batch"
                    << "session=" << session
                    << "id=" << object.value("id"_L1)
                    << "payload=" << QString::fromUtf8(jsonBody(object));
#endif

            if (batch.responses.size() == batch.responseOrder.size()) {
                QJsonArray responseArray;
                for (const auto &responseKey : batch.responseOrder)
                    responseArray.append(batch.responses.value(responseKey));

                qCInfo(lcQMcpServerStreamableHttpTransport)
                        << "Completing deferred HTTP response for session" << session
                        << "batch" << pending.batchId
                        << "responses" << batch.responseOrder.size();
                sendHttpResponse(batch.requestId,
                                 batch.responseOrder.size() == 1
                                         ? jsonBody(responseArray.first().toObject())
                                         : jsonBody(responseArray),
                                 kJsonContentType,
                                 200,
                                 jsonHeaders(session, d->sessions.value(session).protocolVersion));
                d->clearPendingBatch(pending.batchId);
            } else if (pendingById.isEmpty()) {
                d->pendingResponses.remove(session);
            }
            return;
        }
    }

    if (!d->sessions.contains(session) || !d->sessions.value(session).sseOpen) {
        if (d->sessions.contains(session)) {
            d->sessions[session].queuedMessages.append(object);
#ifdef QT_MCP_STREAMABLEHTTP_VERBOSE
            qCInfo(lcQMcpServerStreamableHttpTransport).noquote()
                    << "Queueing server-initiated message until SSE opens"
                    << "session=" << session
                    << "payload=" << QString::fromUtf8(jsonBody(object));
#endif
            return;
        }

        qCDebug(lcQMcpServerStreamableHttpTransport)
                << "Dropping server-initiated message because session is unknown"
                << session;
        return;
    }

    const QUuid sseConnectionId = d->sessions.value(session).sseConnectionId;
    if (sseConnectionId.isNull() || !hasSseConnection(sseConnectionId)) {
        d->sessions[session].sseOpen = false;
        d->sessions[session].sseConnectionId = {};
        qCWarning(lcQMcpServerStreamableHttpTransport)
                << "SSE connection is no longer valid for session" << session;
        return;
    }

    qCInfo(lcQMcpServerStreamableHttpTransport).noquote()
            << "Sending server-initiated SSE message"
            << "session=" << session
            << "payload=" << QString::fromUtf8(jsonBody(object));
    sendSseEvent(sseConnectionId, jsonBody(object), "message"_L1);
}

void StreamableHttpServer::closeSession(const QUuid &session)
{
    qCInfo(lcQMcpServerStreamableHttpTransport)
            << "Closing MCP session" << session;
    if (d->pendingResponses.contains(session)) {
        const auto pendingBatches = d->pendingResponses.value(session);
        for (auto it = pendingBatches.cbegin(), end = pendingBatches.cend(); it != end; ++it)
            d->pendingBatches.remove(it.value().batchId);
        d->pendingResponses.remove(session);
    }
    if (!d->sessions.contains(session))
        return;

    if (d->sessions.value(session).sseOpen) {
        const QUuid sseConnectionId = d->sessions.value(session).sseConnectionId;
        if (!sseConnectionId.isNull() && hasSseConnection(sseConnectionId))
            closeSseConnection(sseConnectionId);
    }

    d->sessions.remove(session);
}

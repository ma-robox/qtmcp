#ifndef QMCPABSTRACTHTTPSERVER_H
#define QMCPABSTRACTHTTPSERVER_H

#include <QtCore/QObject>
#include <QtCore/QUuid>
#include <QtMcpServer/qmcpserverglobal.h>
#include <QtNetwork/QHttpHeaders>
#include <QtNetwork/QNetworkRequest>

QT_BEGIN_NAMESPACE

class QTcpServer;

/*!
    \class QMcpAbstractHttpServer
    \inmodule QtMcpServer
    \brief The QMcpAbstractHttpServer class provides a base class for HTTP-based MCP servers.

    This class implements basic HTTP server functionality with support for Server-Sent Events (SSE),
    allowing real-time communication from server to client. It handles the low-level details of
    HTTP connections and SSE event streaming.

    To implement a custom HTTP server:
    \list
    \li Inherit from QMcpAbstractHttpServer
    \li Call bind() with a QTcpServer instance to start accepting connections
    \li Use the protected SSE methods to manage event streaming
    \endlist
*/
class Q_MCPSERVER_EXPORT QMcpAbstractHttpServer : public QObject
{
    Q_OBJECT

public:
    /*!
        Constructs an HTTP server with the given parent.
        \param parent The parent object
    */
    explicit QMcpAbstractHttpServer(QObject *parent = nullptr);

    /*!
        Destroys the HTTP server.
    */
    ~QMcpAbstractHttpServer() override;

    /*!
        Binds this server to the given TCP server.
        The TCP server must be listening before calling this method.

        \param server The TCP server to bind to
        \return true if binding was successful, false otherwise
    */
    bool bind(QTcpServer *server);

protected:
    /*!
        Registers a new SSE request and returns a unique identifier for it.
        
        \param request The HTTP request to register
        \return UUID for the registered SSE connection
    */
    QUuid registerSseRequest(const QNetworkRequest &request);

    /*!
        Marks an HTTP request for deferred completion and returns its identifier.

        Subclasses can use this for transports where the HTTP response becomes available only
        after the request has been dispatched through higher-level protocol handlers.

        \param request The HTTP request being processed
        \return UUID of the pending HTTP request, or a null UUID if the request is unknown
    */
    QUuid deferHttpResponse(const QNetworkRequest &request);

    /*!
        Sends an HTTP response for a deferred request.

        \param id UUID of the deferred HTTP request
        \param data Response body
        \param contentType Value for the Content-Type header
        \param statusCode HTTP status code
        \param headers Additional HTTP headers
    */
    void sendHttpResponse(const QUuid &id,
                          const QByteArray &data,
                          const QString &contentType = QStringLiteral("text/plain"),
                          int statusCode = 200,
                          const QHttpHeaders &headers = {});

    /*!
        Sends an SSE event to a specific client.
        
        \param id UUID of the SSE connection
        \param data The event data to send
        \param event Optional event type name
    */
    void sendSseEvent(const QUuid &id, const QByteArray &data, const QString &event = QString());

    /*!
        Closes an SSE connection.
        
        \param id UUID of the SSE connection to close
    */
    void closeSseConnection(const QUuid &id);

    /*!
        Returns whether an SSE connection is currently registered.

        \param id UUID of the SSE connection
        \return true if the connection is registered, false otherwise
    */
    bool hasSseConnection(const QUuid &id) const;

private:
    class Private;
    QScopedPointer<Private> d;
};

QT_END_NAMESPACE

#endif // QMCPABSTRACTHTTPSERVER_H

# Qt MCP Library API

Questa guida descrive le API pubbliche principali di `qtmcp` dal punto di vista di chi deve usare la libreria in un'applicazione Qt.

## Moduli

La libreria è divisa in tre moduli pubblici:

- `Qt::McpCommon`
  Tipi MCP condivisi: richieste, risultati, notifiche, capabilities, tool, resource, prompt e versioning protocollo.
- `Qt::McpClient`
  Client MCP type-safe con backend pluggable.
- `Qt::McpServer`
  Server MCP con gestione sessioni, tool set, resource e prompt.

## Requisiti

- Qt `6.8.1` o superiore
- C++20
- `Qt::Network` se usi backend HTTP lato server
- `Qt::Gui` opzionale per tool che restituiscono `QImage` o per integrazione con `QAction`

## Integrazione CMake

```cmake
find_package(Qt6 REQUIRED COMPONENTS McpCommon McpClient McpServer)

target_link_libraries(my_app PRIVATE
    Qt::McpCommon
    Qt::McpClient
    Qt::McpServer
)
```

Per usare solo client o solo server puoi linkare il solo modulo necessario.

## Namespace e versioni

Il namespace pubblico è `QtMcp`, definito in [qtmcpnamespace.h](source/src/mcpcommon/qtmcpnamespace.h).

Versioni protocollo supportate:

- `QtMcp::ProtocolVersion::v2024_11_05`
- `QtMcp::ProtocolVersion::v2025_03_26`
- `QtMcp::ProtocolVersion::v2025_06_18`
- `QtMcp::ProtocolVersion::Latest`

Helper utili:

- `QtMcp::protocolVersionToString(...)`
- `QtMcp::stringToProtocolVersion(...)`

## Tipi comuni

Il modulo `Qt::McpCommon` espone i tipi serializzabili MCP. I più usati in applicazioni sono:

- inizializzazione:
  - `QMcpInitializeRequest`
  - `QMcpInitializeResult`
  - `QMcpInitializedNotification`
- tool:
  - `QMcpTool`
  - `QMcpToolInputSchema`
  - `QMcpListToolsRequest`
  - `QMcpListToolsResult`
  - `QMcpCallToolRequest`
  - `QMcpCallToolResult`
  - `QMcpCallToolResultContent`
- resources:
  - `QMcpResource`
  - `QMcpResourceTemplate`
  - `QMcpListResourcesRequest`
  - `QMcpReadResourceRequest`
  - `QMcpReadResourceResult`
- prompts:
  - `QMcpPrompt`
  - `QMcpListPromptsRequest`
  - `QMcpGetPromptRequest`
  - `QMcpGetPromptResult`
- protocol/core:
  - `QMcpRequest`
  - `QMcpResult`
  - `QMcpNotification`
  - `QMcpJSONRPCErrorError`
  - `QMcpServerCapabilities`
  - `QMcpClientCapabilities`

Questi tipi espongono tipicamente:

- `toJsonObject(protocolVersion)`
- `fromJsonObject(json, protocolVersion)`
- `method()`

Quindi puoi usarli senza scrivere JSON-RPC a mano.

## Client API

La classe principale è [qmcpclient.h](source/src/mcpclient/qmcpclient.h).

### Creazione del client

```cpp
#include <QtMcpClient/QMcpClient>

auto *client = new QMcpClient("stdio", this);
client->setProtocolVersion(QtMcp::ProtocolVersion::Latest);
client->start();
```

Backend disponibili:

- `QMcpClient::backends()`

Il nome backend dipende dai plugin trovati a runtime, ad esempio `stdio` o `sse`.

### Invio richieste

Il client usa un'API type-safe basata su template:

```cpp
client->request(QMcpInitializeRequest(), [](const QMcpInitializeResult &result,
                                            const QMcpJSONRPCErrorError *error) {
    if (error) {
        qWarning() << error->message();
        return;
    }
    qDebug() << result.protocolVersion();
});
```

Pattern disponibile:

- `request(request, callback)`
- `request(request)` per fire-and-forget
- `notify(notification)`

### Handler lato client

Puoi anche gestire richieste e notifiche inviate dal server:

```cpp
client->addNotificationHandler([](const QMcpLoggingMessageNotification &notification) {
    qDebug() << notification.params().message();
});
```

Per richieste dal server:

```cpp
client->addRequestHandler([](const QMcpCreateMessageRequest &request,
                             QMcpJSONRPCErrorError *error) {
    Q_UNUSED(error);
    QMcpCreateMessageResult result;
    return result;
});
```

### Segnali principali del client

- `started()`
- `errorOccurred(QString)`
- `received(QJsonObject)`
- `protocolVersionChanged(QtMcp::ProtocolVersion)`

## Server API

Le classi principali sono:

- [qmcpserver.h](source/src/mcpserver/qmcpserver.h)
- [qmcpserversession.h](source/src/mcpserver/qmcpserversession.h)

### Creazione del server

```cpp
#include <QtMcpServer/QMcpServer>

auto *server = new QMcpServer("stdio", this);
server->setProtocolVersion(QtMcp::ProtocolVersion::Latest);
server->start();
```

Backend disponibili:

- `QMcpServer::backends()`

Esempi presenti nel repository:

- [echo/main.cpp](source/examples/mcpserver/echo/main.cpp)
- [texteditor/mainwindow.cpp](source/examples/mcpserver/texteditor/mainwindow.cpp)

### Configurazione server

Proprietà pubbliche:

- `capabilities`
- `instructions`
- `protocolVersion`
- `supportedProtocolVersions`
- `bearerToken`
- `sessionCloseGracePeriodMs`

Il server negozia la versione protocollo durante `initialize`. Le versioni supportate sono esposte da `supportedProtocolVersions()`.

Note utili:

- `bearerToken` Ã¨ usata dai backend HTTP che supportano autorizzazione bearer, come `streamablehttp`
- `sessionCloseGracePeriodMs` Ã¨ usata dai backend che supportano una finestra di riconnessione della sessione; in `streamablehttp` controlla per quanto tempo una sessione resta aperta dopo la chiusura della SSE prima di emettere `sessionClosed(...)`

### Sessioni

Ogni connessione client crea una `QMcpServerSession`, emessa tramite:

- `newSession(QMcpServerSession *session)`
- `sessionClosed(const QUuid &sessionId)` quando la sessione viene chiusa

Nel backend `streamablehttp`, la chiusura della sessione puÃ² essere ritardata da `sessionCloseGracePeriodMs` per consentire la riapertura della connessione SSE sulla stessa sessione.

La sessione contiene stato per:

- resources
- prompt
- tool
- subscriptions
- roots
- protocol version negoziata

Metodi utili su `QMcpServerSession`:

- `tools()`
- `callTool(...)`
- `callToolAsync(...)`
- `resources()`
- `contents(uri)`
- `prompts()`
- `messages(name)`
- `setRoots(...)`
- `createMessage(...)`

### Handler type-safe lato server

Puoi registrare handler per richieste e notifiche MCP:

```cpp
server->addRequestHandler([](const QUuid &sessionId,
                             const QMcpPingRequest &request,
                             QMcpJSONRPCErrorError *error) {
    Q_UNUSED(sessionId);
    Q_UNUSED(request);
    Q_UNUSED(error);
    return QMcpEmptyResult();
});
```

Notifiche:

```cpp
server->addNotificationHandler([](const QUuid &sessionId,
                                  const QMcpInitializedNotification &notification) {
    Q_UNUSED(sessionId);
    Q_UNUSED(notification);
});
```

### Registrazione tool da QObject

Il pattern più semplice è registrare un `QObject` che espone metodi `Q_INVOKABLE`.

Esempio minimale:

```cpp
class MyTools : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    Q_INVOKABLE QString hello(const QString &name) const
    {
        return u"Hello %1"_s.arg(name);
    }
};

auto *tools = new MyTools(server);
tools->setObjectName("demo");
server->registerToolSet(tools, {
    { "hello", "Simple greeting tool" },
    { "demo/hello/name", "Name to greet" }
});
```

Risultato lato MCP:

- nome tool: `demo/hello`
- schema input derivato dai parametri Qt

Regole pratiche dedotte dall’implementazione in [qmcpserversession.cpp](source/src/mcpserver/qmcpserversession.cpp):

- vengono esposti solo metodi `public`
- `signals` e costruttori sono ignorati
- il prefisso tool deriva da `objectName()`
- i parametri supportati vengono mappati automaticamente a schema JSON
- i tipi mappati esplicitamente in schema sono:
  - `QString -> string`
  - `bool -> boolean`
  - `int -> integer`
- `QUuid` è trattato come parametro interno e può ricevere il `sessionId`
- i metodi con overload/default parameters vengono consolidati nel tool schema

### Tipi di ritorno tool supportati

Per tool sincroni:

- `void`
- `bool`
- `QString`
- `QStringList`
- `QImage` se `QtGui` è disponibile

Per tool asincroni:

- `QFuture<QList<QMcpCallToolResultContent>>`

Se un tool asincrono riceve un `progressToken`, la sessione emette notifiche di progresso MCP.

Un esempio reale di tool asincroni è in:

- [mainwindow.h](source/examples/mcpserver/texteditor/mainwindow.h)

### Resource e prompt

`QMcpServerSession` espone API ad alto livello per pubblicare contenuti:

- `appendResourceTemplate(...)`
- `appendResource(...)`
- `replaceResource(...)`
- `removeResource(...)`
- `appendPrompt(...)`
- `replacePrompt(...)`
- `removePromptAt(...)`

Quando la sessione è inizializzata, le modifiche generano notifiche MCP `list_changed` o `resourceUpdated` dove applicabile.

## Backend e trasporti

Libreria e trasporto sono separati.

Interfacce pubbliche:

- [qmcpclientbackendinterface.h](source/src/mcpclient/qmcpclientbackendinterface.h)
- [qmcpserverbackendinterface.h](source/src/mcpserver/qmcpserverbackendinterface.h)

Se devi implementare un nuovo trasporto:

- lato client deriva da `QMcpClientBackendInterface`
- lato server deriva da `QMcpServerBackendInterface`
- registra il plugin con `QMcpClientBackendPlugin` o `QMcpServerBackendPlugin`

Backend già presenti nel repository:

- client:
  - `stdio`
  - `sse`
- server:
  - `stdio`
  - `sse`
  - `streamablehttp`

## Esempio end-to-end minimo

### Server

```cpp
#include <QtCore/QCoreApplication>
#include <QtMcpServer/QMcpServer>

class DemoServer : public QMcpServer
{
    Q_OBJECT
public:
    DemoServer() : QMcpServer("stdio") {}

    Q_INVOKABLE QString hello(const QString &name) const
    {
        return "Hello " + name;
    }

    QHash<QString, QString> toolDescriptions() const override
    {
        return {
            { "hello", "Return a greeting" },
            { "hello/name", "Name to greet" }
        };
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    DemoServer server;
    server.start();
    return app.exec();
}
```

### Client

```cpp
#include <QtCore/QCoreApplication>
#include <QtMcpClient/QMcpClient>
#include <QtMcpCommon/QMcpInitializeRequest>
#include <QtMcpCommon/QMcpInitializedNotification>
#include <QtMcpCommon/QMcpListToolsRequest>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QMcpClient client("stdio");
    client.setProtocolVersion(QtMcp::ProtocolVersion::Latest);
    client.start();

    QMcpInitializeRequest init;
    client.request(init, [&client](const QMcpInitializeResult &, const QMcpJSONRPCErrorError *error) {
        if (error)
            return;

        client.notify(QMcpInitializedNotification());

        QMcpListToolsRequest request;
        client.request(request, [](const QMcpListToolsResult &result, const QMcpJSONRPCErrorError *error) {
            if (!error)
                qDebug() << result.tools().size();
        });
    });

    return app.exec();
}
```

## Limiti pratici attuali

Questa guida riflette il codice corrente del repository, non una API reference generata automaticamente. Alcune limitazioni utili da sapere:

- la mappatura automatica schema-parametri dei tool copre un set ristretto di tipi Qt di base
- l’invocazione riflessiva dei tool gestisce fino a 5 parametri
- il comportamento dei backend disponibili dipende dai plugin effettivamente installati a runtime

## Riferimenti nel repository

- overview generale: [README.md](source/README.md)
- client: [qmcpclient.h](source/src/mcpclient/qmcpclient.h)
- server: [qmcpserver.h](source/src/mcpserver/qmcpserver.h)
- sessione server: [qmcpserversession.h](source/src/mcpserver/qmcpserversession.h)
- namespace/versioni: [qtmcpnamespace.h](source/src/mcpcommon/qtmcpnamespace.h)

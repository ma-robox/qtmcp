Sei un agente di sviluppo software senior incaricato di aggiungere il supporto a un backend MCP “Streamable HTTP” in un repository basato su QtMcp.

Obiettivo
Implementare un backend server Streamable HTTP compatibile con la specifica MCP moderna, integrabile con Codex, mantenendo coerenza con l’architettura esistente del progetto e senza rompere i backend già presenti, in particolare stdio e SSE.

Contesto
- Il repository usa Qt / C++ / CMake.
- Esiste già supporto backend almeno per stdio e probabilmente SSE.
- Il server attuale con SSE non è sufficiente per un’integrazione stabile con Codex.
- Codex supporta STDIO e Streamable HTTP.
- Il nuovo backend deve permettere di configurare un endpoint HTTP MCP moderno, idealmente su path tipo /mcp.
- L’implementazione deve rispettare la struttura e lo stile del repository, non introdurre scorciatoie o hack isolati.

Cosa devi fare
1. Analizza il repository e individua:
   - il punto in cui viene creato/configurato il backend server;
   - le interfacce o classi base dei backend/transport;
   - il backend SSE esistente, da usare come riferimento architetturale;
   - il flusso di parsing/serializzazione JSON-RPC;
   - il punto in cui vengono gestite sessioni, messaggi, capabilities e tools.

2. Progetta e implementa un backend “Streamable HTTP” che:
   - esponga endpoint HTTP compatibili con MCP moderno;
   - supporti richieste JSON-RPC via HTTP;
   - supporti streaming tramite SSE dove richiesto dal transport Streamable HTTP;
   - gestisca correttamente sessioni, lifecycle e framing dei messaggi;
   - non invii output spurio;
   - non passi linee vuote o framing SSE al parser JSON;
   - distingua chiaramente livello transport e payload JSON-RPC.

3. Assicurati che:
   - il content type sia corretto per ogni endpoint;
   - le richieste non-MCP o malformate ricevano errori HTTP/JSON-RPC sensati;
   - le capabilities del server siano esposte correttamente;
   - tools/list e tool invocation continuino a funzionare;
   - non si rompa il comportamento di stdio/SSE esistente.

4. Aggiorna anche:
   - CMakeLists.txt e build system;
   - eventuale registrazione plugin/backend;
   - CLI/opzioni di avvio del server, ad esempio con un backend selezionabile;
   - documentazione minima d’uso;
   - esempio di configurazione Codex.

Output richiesto
Fornisci:
1. una breve analisi iniziale dell’architettura rilevante;
2. un piano di modifica conciso;
3. i file modificati e i nuovi file creati;
4. patch o contenuto completo dei file;
5. spiegazione tecnica delle scelte principali;
6. comandi di build e test;
7. esempio di esecuzione del server;
8. esempio di config Codex per usare il backend Streamable HTTP.

Vincoli tecnici
- Non fare refactor non necessari.
- Mantieni compatibilità con Qt e CMake già usati dal repository.
- Riusa quanto possibile le classi e i pattern già presenti.
- Non duplicare logica di protocollo se esiste già in componenti comuni.
- Evita dipendenze esterne non necessarie.
- Se una parte del protocollo non è ancora supportata nel repository, implementa la soluzione minima corretta e documenta chiaramente il limite.
- Se trovi ambiguità architetturali, scegli la soluzione più conservativa e coerente con il codice esistente.

Criteri di accettazione
Il lavoro è considerato riuscito se:
- il server può essere avviato in modalità Streamable HTTP;
- un client MCP compatibile riesce a completare initialize;
- tools/list restituisce i tool correttamente;
- l’invocazione di almeno un tool funziona;
- il backend non genera errori di parsing dovuti a framing o payload vuoti;
- è possibile configurare Codex tramite URL HTTP invece di usare mcp-proxy.

Indicazioni di lavoro
- Parti leggendo i file che definiscono backend server, sessioni e protocollo.
- Se esiste un backend SSE, confrontalo con i requisiti di Streamable HTTP e adatta solo ciò che serve.
- Prima di scrivere codice, riassumi in poche righe dove intendi intervenire.
- Quando modifichi il codice, produci patch leggibili e spiegazioni precise.
- Se qualcosa nel repository impedisce una soluzione completa, non fermarti: implementa la parte utile massima possibile e segnala esattamente cosa manca.

Esempio di risultato atteso
- Nuovo backend selezionabile tipo “streamablehttp”
- endpoint HTTP operativo, ad esempio /mcp
- gestione corretta di POST/GET e streaming dove necessario
- documentazione con esempio:
  [mcp_servers.nome]
  url = "http://127.0.0.1:8000/mcp"

Importante
Lavora solo sulla base del repository reale. Non inventare API inesistenti se puoi prima verificare le classi già presenti. Quando fai assunzioni, dichiarale esplicitamente.

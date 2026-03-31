# Windows Server Transport Notes

This document summarizes a common class of Windows server issues around client disconnects, streaming transports, and noisy socket errors. It is intentionally generic and applies beyond MCP or any single library.

## What Can Happen

When a server runs on Windows, especially with long-lived or streaming connections, you may see:

- `Connection reset by peer`
- `WinError 10054`
- broken pipe or write-after-close errors
- stack traces during client disconnect or server shutdown
- `404` or `401` on optional discovery endpoints such as `/.well-known/...`
- partial reads, partial writes, or abrupt disconnects on SSE, WebSocket, chunked HTTP, or long-poll requests
- behavior that differs from Linux under the same test

These events are not necessarily fatal. Some are expected when a client probes an unsupported endpoint or closes the connection before the server finishes writing.

## How To Check

Use this checklist before deciding whether the issue is a real server bug or only transport noise.

1. Reproduce with repeated connect/disconnect cycles.
2. Check whether the server still works after the error appears.
3. Distinguish expected probe traffic from real failures.
4. Test both streaming and non-streaming endpoints.
5. Compare the same scenario on Windows and Linux.
6. Enable framework and transport debug logging.
7. Test graceful client shutdown and abrupt client termination separately.
8. Try a minimal client such as `curl` or a small socket script to isolate the problem from the main application.
9. Check whether the issue only happens with a specific runtime mode, transport backend, or protocol version.

## Typical Root Causes

Common causes include:

- the client closes the socket while the server is still reading or writing
- the server logs normal disconnects as errors
- the Windows runtime or event loop handles teardown differently than Linux
- a framework has edge cases around streaming responses or shutdown
- optional endpoints are probed automatically by clients but are not implemented
- local antivirus, VPN, proxy, or endpoint security software interferes with sockets

## Possible Fixes

The right fix depends on the runtime and server library, but these are the usual options:

- treat client disconnects as normal and reduce their log severity
- catch and ignore connection-reset exceptions during response teardown
- switch to a different event loop or I/O backend if the runtime supports it
- upgrade the runtime, HTTP server, and transport libraries
- tune keep-alive, timeout, and graceful shutdown settings
- return controlled responses for optional discovery endpoints instead of falling through noisily
- avoid fragile streaming transports on Windows if a simpler request/response model is acceptable
- isolate the server behind a reverse proxy if the application server has weak Windows transport handling
- test with antivirus, VPN, or proxy software disabled if resets appear environment-specific

## Language And Runtime Scope

This problem class is not limited to MCP and not limited to one library.

- Python async servers on Windows commonly expose these issues because of runtime and event loop behavior.
- Other languages such as C++, Rust, Go, Java, and Node can also see connection resets, but the symptoms and stack traces differ.
- A compiled library is not automatically immune. The relevant factor is how its runtime and networking stack handle disconnects on Windows.

## Practical Guidance

Treat the issue as real if:

- requests fail consistently
- the server becomes unusable after the error
- responses are truncated or corrupted
- reconnects stop working

Treat it as mostly log noise if:

- the errors happen only when clients disconnect abruptly
- unsupported discovery endpoints return `404`
- normal requests continue to work afterward

## Recommended Baseline

For any Windows server project, document and test:

- repeated client connect/disconnect behavior
- streaming endpoint behavior
- graceful shutdown behavior
- expected optional endpoint responses
- any required Windows-specific runtime setting

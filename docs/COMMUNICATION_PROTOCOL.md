# Communication Protocol Contract V1

- Any logged-in client may send asynchronous requests.
- Each request carries client-owned `request_id`.
- Response: `request_id`, boolean `status`, optional `payload`, optional non-empty `diagnostics`.
- `operation_id` is internal and is not part of the wire response.
- `LOGIN(name)` returns current Server state in `payload`.
- LOAD/UNLOAD/BUILD/PUBLISH/REBUILD do not return Server state in `payload`.
- Server state changes are pushed asynchronously as `SERVER_STATE(old,new)`.
- `CLIENT(login,arg,parameters)` routes to every logged-in client with matching login.
- Empty login broadcasts to all logged-in clients.
- Recipient gets `CLIENT ACTION(from,arg,parameters)`.
- Subscriptions are session-owned and deliver asynchronously by `subscription_id` and sequence.
- TCP framing and concrete JSON encoding are separate implementation slices.

## Communication Connection Core V1

Cardinality is one transport connection to one logical session. Reconnect creates
a new session; V1 has no resume.

The connection owns a bounded MPSC outbound channel with one writer. The writer
alone assigns monotonically increasing `connection_sequence` when it dequeues
the next frame. Producers never allocate connection sequence values.

Overflow transitions the connection to closing. Queued outbound data is
discarded; there is no selective drop and no drain guarantee. Enqueue after
closing returns false. Reconnect + LOGIN is the resynchronization mechanism.

`server_request_origin` carries a move-only intrusive connection lifetime token.
`present()` is cheap and non-blocking: it only enqueues a semantic response.
Connection destruction performs no thread-affine transport I/O.

`request_id` is unique while outstanding. Duplicate IDs are rejected before
Server enqueue. The writer releases a request ID when its response becomes the
next outbound frame and receives its connection sequence.

LOGIN commit and subscription-registry mutation are Server-control-thread-only.

```text
after successful LOGIN:
    client sees either current state in LOGIN response
    or later SERVER_STATE transition
    and never misses the boundary
```

SERVER_STATE routing checks `logged_in` and is serialized on that same Server
control thread. Subscription teardown deregisters publisher tokens; a transport
close thread never mutates the subscription registry.

Connection close is one-way (`open -> closing`). RX stops, new requests are
rejected, outbound enqueue fails, queued outbound is discarded, late
responses/events are ignored, and the transport owner terminates the writer and
closes the socket. Requests already accepted by Server are not cancelled by
peer disconnect.

For Server shutdown, queued-but-not-started requests are discarded without
execution so their origin lifetime tokens are released. The currently executing
request completes. SHUTDOWN may use a bounded writer barrier: wait until its
response sequence is locally written, or until timeout / transport failure,
then continue global shutdown.

Open issues:
- heartbeat / half-open detection / idle timeout;
- slow-consumer reconnect loops and future pacing / flow-control / backoff.


## Communication Control V1

LOGIN, CLIENT routing, and connection-close are Communication control messages,
not `server_request_kind` values. They share the same Server control queue/thread
with ordinary Server requests.

LOGIN commits `logged_in`, registers the connection for async routing, captures
the current Server state, and enqueues the LOGIN response on one Server control
thread. SERVER_STATE publication also runs on that thread, so the LOGIN snapshot
boundary cannot miss a transition.

Successful Project state transitions enqueue SERVER_STATE only after the command
response has been presented. Failed operations produce no state event.

CLIENT routing scans the active logged-in connection registry. Empty target login
broadcasts to all logged-in sessions; a non-empty target routes to every matching
login, including the sender when it matches. Zero recipients is still a successful
routing request: success means Server accepted the routing operation, not delivery
acknowledgement.

A transport close posts a connection-close control item. The transport thread may
set `closing`, but only the Server control thread removes the connection from the
registry and tears down publisher subscription registrations.

Server shutdown stops request acceptance and destroys queued-but-not-started
control items without execution; their lifetime tokens are released by destruction.


## TCP JSON Transport V1

TCP uses a four-byte unsigned big-endian payload length followed by one UTF-8
JSON object. The V1 maximum JSON payload is 1 MiB. Zero-length or oversized
frames close the connection.

The TCP endpoint uses one non-blocking socket event loop for all connections of
that endpoint. It is the single writer for each connection; there is no thread
per client. Outbound enqueue wakes that event loop.

Wire requests use `request_id`, uppercase `command`, and optional `arguments`.
Supported V1 commands are LOGIN, LOAD, PUBLISH, BUILD, UNLOAD, REBUILD,
GET_STATE, GET_VALUE, SHUTDOWN, and CLIENT. CLIENT `parameters` is Base64.

Every outbound JSON object contains `sequence`. Responses additionally contain
`request_id` and boolean `status`; payload and diagnostics are emitted only when
present. Async actions use `action`.

The aggregate active TCP connection limit is shared across all configured TCP
endpoints. FULL uses the active lease `max_connections`; DEMO uses one active TCP
connection and closes it after five minutes. Console does not consume this limit.

SHUTDOWN waits up to two seconds for the initiating TCP response to complete its
local socket write. Timeout or transport failure does not prevent Server shutdown.


### TCP JSON V1 audit invariants

The wire schema is fail-closed. At the top level only `request_id`, `command`,
and the optional object `arguments` are accepted. A second `arguments` object,
unknown top-level objects, unknown scalar fields, and deeper nesting are rejected.

The SHUTDOWN write barrier is armed before its response is enqueued. Completion
is sticky for that one awaited response until the Server consumes the barrier,
so a later response write cannot overwrite the completion observation.

Stopping request acceptance wakes both queue wait APIs. An empty stopped queue
returns failure; it never synthesizes a Server request.

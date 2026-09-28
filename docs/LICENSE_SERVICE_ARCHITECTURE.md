# License Service Architecture V1

## Purpose

`LicenseServiceV1` is the centralized licensing authority for standalone
`ServerEngineV4` processes. It is separate from ServerEngine, Project, Studio,
and Runtime.

```text
ServerEngineV4
    -> Microsoft Entra server identity
    -> LicenseServiceV1
    -> entitlement + active-server admission
    -> signed short-lived lease
    -> local verification/enforcement in ServerEngine
```

A Microsoft Entra access token proves the daemon identity. It is not the product
license and its token lifetime is not the product-license lifetime.

## Deployment boundary

V1 is a standalone .NET service:

```text
LicenseService.exe
    ASP.NET Core / .NET 10
    Kestrel HTTPS
    Windows Service
    Blazor admin UI
    REST API
    SQL Server
    no IIS requirement
```

IIS or another reverse proxy may be added later without changing the service
boundary.

## Authentication and authorization

Production ServerEngine callers use Microsoft Entra app-only authentication.

```text
CW ServerEngine App Registration
    certificate credential
        |
        | OAuth2 client_credentials
        v
Microsoft Entra
        |
        | access token
        | role = ServerEngine.Access
        v
CW License Service API
```

The License Service maps the authenticated application to one `Customer` using:

```text
tid + azp/appid
    -> Customer.EntraTenantId
    -> Customer.EntraClientId
```

The protected lease API requires:

```text
ServerEngine.Access
```

Development-only authentication exists only to validate licensing semantics
without depending on Entra. It requires both:

```text
ASPNETCORE_ENVIRONMENT=Development
DevelopmentAuthentication.Enabled=true
```

and an explicit request header:

```text
X-CW-Development-Authentication: 1
```

The process fails closed if development authentication is enabled outside the
Development environment.

## Persistent model

```text
Customer
    Id
    Name
    EntraTenantId
    EntraClientId
    Enabled

Entitlement
    Id
    CustomerId
    Product
    ExpiresAtUtc
    MaxActiveServers
    MaxConnectionsPerServer
    LeaseMinutes
    Enabled

ServerInstance
    Id
    CustomerId
    FirstSeenUtc
    LastSeenUtc
    Disabled

LeaseRecord
    Id
    ServerInstanceId
    CustomerId
    EntitlementId
    Product
    IssuedAtUtc
    ExpiresAtUtc
    ReleasedAtUtc
    MaxConnections
```

`server_instance_id` is currently a persistent installation identifier supplied
by ServerEngine. Cryptographic binding of that identifier to a per-installation
private key/certificate is a future hardening slice.

## Lease API

```text
POST /api/v1/lease/acquire
POST /api/v1/lease/renew
POST /api/v1/lease/release
```

Acquire:

```json
{
  "serverInstanceId": "server-001",
  "product": "ServerEngineV4"
}
```

Renew:

```json
{
  "leaseId": "<guid>",
  "serverInstanceId": "server-001"
}
```

Release uses the same `leaseId + serverInstanceId` identity.

A successful acquire/renew returns a signed short-lived lease with:

```text
lease id
server instance id
customer id
product
issued at
not before
expires at
max connections
issuer
audience
```

## Lease signing

Lease tokens use a dedicated RSA certificate.

The HTTPS certificate and the lease-signing certificate are separate keys.

The service constructs and signs the new lease before committing the SQL
transaction. If signing fails, no new active lease is committed.

Internal signing exceptions are logged by LicenseService. Remote callers receive
a generic HTTP 500 response without certificate/store internals.

## Concurrency contract

`MaxActiveServers` is a hard admission limit.

The serialization root is the unique Entitlement row:

```text
(CustomerId, Product)
```

Every acquire/renew mutation locks that row with SQL Server:

```sql
WITH (UPDLOCK, HOLDLOCK)
```

The protected sequence is:

```text
resolve Customer
    -> determine Product
    -> lock Entitlement(CustomerId, Product)
    -> revalidate exact active lease for RENEW
    -> validate/create ServerInstance
    -> count active ServerInstances
    -> sign successor lease
    -> SaveChanges
    -> Commit
```

There is no global process mutex. Different customer/product pairs can proceed
independently.

Blind SQL retry is not the primary concurrency mechanism.

## Renew semantics

Renew is strict:

```text
RENEW(lease_id, server_instance_id)

requires:
    exact lease exists
    authenticated Customer owns it
    ServerInstanceId matches
    ReleasedAtUtc == null
    ExpiresAtUtc > now
```

Renew may read an old lease once to discover Product, but the exact active lease
is revalidated after the Entitlement serialization lock is held.

A released or expired lease cannot mint a successor lease.

## Release semantics

Release is idempotent for the owning caller.

Releasing an already released lease succeeds. A missing/foreign lease does not
disclose ownership information.

## HTTP failure semantics

```text
400 invalid_request
403 customer / entitlement / server-instance policy denial
404 active lease not found
409 active_server_limit_reached
500 lease_signing_failed
```

Authentication and product licensing remain separate concerns.

## Verified V1 behavior

The current V1 design has been exercised with:

```text
acquire -> signed lease
renew exact active lease
release
release again
renew released lease -> rejected
parallel acquire with MaxActiveServers=10
exact parallel acquire with MaxActiveServers=1
```

The exact concurrency test produced:

```text
5 simultaneous acquire requests
1 x HTTP 200
4 x HTTP 409
0 x HTTP 500
```

The earlier uncoordinated Serializable implementation produced SQL Server
deadlock victims. The Entitlement-row serialization root removed that failure in
the verified test.

## ServerEngine integration boundary

The next integration path is:

```text
persistent server_instance_id
    -> ServerEngine Microsoft Entra daemon identity
    -> obtain app-only access token
    -> POST /api/v1/lease/acquire
    -> verify signed lease locally
    -> enforce expiry + max_connections
    -> renew before expiry
    -> release during orderly shutdown
```

ServerEngine must verify lease authenticity locally. LicenseService is not on
the Runtime hot path and is not queried for every Runtime/client operation.

# License Service V1 Setup and Operations

## 1. Prerequisites

Windows development machine:

```text
.NET SDK 10.x
SQL Server 2025 Developer (or compatible SQL Server)
PowerShell
Microsoft Entra tenant for production-auth testing
```

Verify SQL Server:

```powershell
Get-Service MSSQLSERVER
```

Expected:

```text
Running  MSSQLSERVER
```

## 2. Create the project

From the `ServerEngineV4` repository:

```powershell
powershell -ExecutionPolicy Bypass `
    -File .\scripts\create_license_service_v1.ps1 `
    -Destination C:\Boris\LicenseServiceV1
```

The script creates the complete standalone `LicenseService` project, restores
NuGet packages, and performs a Release build.

It does not create production Entra registrations or production certificates.

## 3. SQL Server

Default development connection:

```json
"LicenseDatabase":
  "Server=localhost;Database=CWLicenseService;Trusted_Connection=True;TrustServerCertificate=True"
```

V1 uses `Database.EnsureCreated=true` for local bring-up. On first start the
service creates `CWLicenseService` and the required tables/indexes.

For a production deployment, replace `EnsureCreated` with an explicit migration
and deployment policy.

## 4. Development HTTPS

Trust the ASP.NET Core development certificate:

```powershell
dotnet dev-certs https --trust
```

The development configuration intentionally does not pin a named Kestrel store
certificate. Kestrel can therefore use the standard ASP.NET development
certificate.

Production Windows Service deployment must use an explicit machine certificate.

## 5. Lease-signing certificate

Create a separate RSA certificate:

```powershell
$leaseCert = New-SelfSignedCertificate `
    -Type Custom `
    -Subject "CN=CW License Service Lease Signing" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -KeyAlgorithm RSA `
    -KeyLength 3072 `
    -HashAlgorithm SHA256 `
    -KeyUsage DigitalSignature `
    -KeyExportPolicy NonExportable `
    -NotAfter (Get-Date).AddYears(5)

$leaseCert |
    Format-List Subject, Thumbprint, HasPrivateKey, NotAfter
```

Put its thumbprint into:

```json
"Lease": {
  "SigningCertificateThumbprint": "<thumbprint>",
  "SigningCertificateStore": "CurrentUser"
}
```

Do not reuse the HTTPS certificate as the lease-signing identity.

## 6. Start locally

```powershell
Set-Location C:\Boris\LicenseServiceV1

dotnet run --project .\LicenseService\LicenseService.csproj
```

Expected:

```text
Now listening on: https://0.0.0.0:8443
Application started.
```

Open:

```text
https://localhost:8443/
```

The Blazor admin UI is loopback-only in V1.

## 7. Development Customer and Entitlement

The generated development authentication identity is:

```text
Tenant ID:
11111111-1111-1111-1111-111111111111

Client ID:
22222222-2222-2222-2222-222222222222
```

Create one Customer with those values.

Create an Entitlement such as:

```text
Product: ServerEngineV4
Expires UTC: future
Max active servers: 10
Max connections/server: 32
Lease minutes: 60
Enabled: true
```

## 8. Development acquire test

```powershell
$headers = @{
    "X-CW-Development-Authentication" = "1"
}

$body = @{
    serverInstanceId = "dev-server-001"
    product = "ServerEngineV4"
} | ConvertTo-Json

Invoke-RestMethod `
    -Method Post `
    -Uri "https://localhost:8443/api/v1/lease/acquire" `
    -Headers $headers `
    -ContentType "application/json" `
    -Body $body
```

Expected response contains:

```text
leaseId
serverInstanceId
product
issuedAtUtc
expiresAtUtc
maxConnections
leaseToken
```

## 9. Microsoft Entra production-auth setup

Create two App Registrations:

```text
CW License Service API
CW ServerEngine
```

For `CW License Service API`, create App Role:

```text
Display name: ServerEngine Access
Allowed member types: Applications
Value: ServerEngine.Access
Enabled: Yes
```

For `CW ServerEngine`:

```text
API permissions
    -> CW License Service API
    -> Application permissions
    -> ServerEngine.Access
    -> Grant admin consent
```

Create a ServerEngine daemon certificate and upload only its public certificate
to the `CW ServerEngine` App Registration.

LicenseService configuration:

```json
"AzureAdApi": {
  "Instance": "https://login.microsoftonline.com/",
  "TenantId": "<TENANT_ID>",
  "ClientId": "<LICENSE_SERVICE_CLIENT_ID>"
}
```

ServerEngine token scope:

```text
api://<LICENSE_SERVICE_CLIENT_ID>/.default
```

ServerEngine identity:

```text
tenant_id   = TENANT_ID
client_id   = SERVERENGINE_CLIENT_ID
certificate = daemon certificate private key
```

Keep development authentication enabled until the first real Entra end-to-end
lease request passes. Then disable it for production configuration.

## 10. Windows Service deployment

Publish:

```powershell
dotnet publish `
    .\LicenseService\LicenseService.csproj `
    -c Release `
    -r win-x64 `
    --self-contained false `
    -o .\publish\LicenseService
```

Production deployment should run the executable as a dedicated Windows Service
identity with access to:

```text
SQL Server
HTTPS certificate private key
lease-signing certificate private key
network access to Microsoft Entra
```

If certificates are stored under `LocalMachine\My`, grant the service identity
access only to the required private keys.

## 11. Security boundaries

Do not treat any of these as equivalent:

```text
Entra access token
    proves daemon identity

Entitlement
    defines purchased/allowed product limits

Signed lease
    grants one ServerEngine instance bounded runtime authority

server_instance_id
    identifies an installation; V1 does not yet cryptographically bind it
```

Development authentication is not a production fallback.

## 12. Next ServerEngine slice

After Entra end-to-end validation:

```text
SERVERENGINE-LICENSE-CLIENT-V1

persistent server_instance_id
HTTPS LicenseService client
acquire
local RS256 lease verification
renew
release
fail-closed expiry handling
max_connections enforcement
```

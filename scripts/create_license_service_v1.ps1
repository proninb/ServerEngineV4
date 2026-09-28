param(
    [string]$Destination = "C:\Boris\LicenseServiceV1",
    [switch]$Force,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

function Write-Utf8File {
    param(
        [string]$Path,
        [string]$Content
    )

    $directory = Split-Path -Parent $Path

    if ($directory) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    [System.IO.File]::WriteAllText(
        $Path,
        $Content,
        [System.Text.UTF8Encoding]::new($false))
}

function Require-DotNet10 {
    $sdks = & dotnet --list-sdks

    if ($LASTEXITCODE -ne 0) {
        throw ".NET SDK is not available."
    }

    if (-not ($sdks | Where-Object { $_ -match '^10\.' })) {
        throw ".NET SDK 10.x is required."
    }
}

Require-DotNet10

$root = [System.IO.Path]::GetFullPath($Destination)

if (Test-Path $root) {
    if (-not $Force) {
        throw "Destination already exists: $root. Use -Force to overwrite generated files."
    }
}
else {
    New-Item -ItemType Directory -Path $root -Force | Out-Null
}

$project = Join-Path $root "LicenseService"

$files = @{}

$files["LicenseService\LicenseService.csproj"] = @'
<Project Sdk="Microsoft.NET.Sdk.Web">

  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
  </PropertyGroup>

  <ItemGroup>
    <PackageReference Include="Microsoft.EntityFrameworkCore.Design" Version="10.0.12">
      <PrivateAssets>all</PrivateAssets>
      <IncludeAssets>runtime; build; native; contentfiles; analyzers; buildtransitive</IncludeAssets>
    </PackageReference>
    <PackageReference Include="Microsoft.EntityFrameworkCore.SqlServer" Version="10.0.12" />
    <PackageReference Include="Microsoft.Extensions.Hosting.WindowsServices" Version="10.0.12" />
    <PackageReference Include="Microsoft.Identity.Web" Version="4.10.0" />
  </ItemGroup>

</Project>
'@

$files["LicenseService\appsettings.json"] = @'
{
  "ConnectionStrings": {
    "LicenseDatabase": "Server=localhost;Database=CWLicenseService;Trusted_Connection=True;TrustServerCertificate=True"
  },
  "Database": {
    "EnsureCreated": true
  },
  "AzureAdApi": {
    "Instance": "https://login.microsoftonline.com/",
    "TenantId": "<tenant-id>",
    "ClientId": "<license-service-api-client-id>"
  },
  "Authorization": {
    "RequiredApplicationRole": "ServerEngine.Access"
  },
  "DevelopmentAuthentication": {
    "Enabled": true,
    "TenantId": "11111111-1111-1111-1111-111111111111",
    "ClientId": "22222222-2222-2222-2222-222222222222"
  },
  "Lease": {
    "Issuer": "https://license.example.local",
    "Audience": "ServerEngineV4",
    "DefaultMinutes": 60,
    "SigningCertificateThumbprint": "<lease-signing-certificate-thumbprint>",
    "SigningCertificateStore": "CurrentUser"
  },
  "Kestrel": {
    "Endpoints": {
      "Https": {
        "Url": "https://0.0.0.0:8443"
      }
    }
  },
  "Logging": {
    "LogLevel": {
      "Default": "Information",
      "Microsoft.AspNetCore": "Warning"
    }
  }
}
'@

$files["LicenseService\Program.cs"] = @'
using LicenseService.Api;
using LicenseService.Components;
using LicenseService.Data;
using LicenseService.Licensing;
using LicenseService.Security;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.EntityFrameworkCore;
using Microsoft.Identity.Web;
using System.Net;

var options = new WebApplicationOptions
{
    Args = args,
};

var builder = WebApplication.CreateBuilder(options);

var developmentAuthenticationEnabled =
    builder.Configuration.GetValue<bool>(
        "DevelopmentAuthentication:Enabled");

if (developmentAuthenticationEnabled &&
    !builder.Environment.IsDevelopment())
{
    throw new InvalidOperationException(
        "DevelopmentAuthentication may be enabled only in the Development environment.");
}

builder.Host.UseWindowsService(service =>
{
    service.ServiceName = "CW License Service";
});

var authentication =
    builder.Services.AddAuthentication(
        JwtBearerDefaults.AuthenticationScheme);

authentication.AddMicrosoftIdentityWebApi(
    builder.Configuration.GetSection("AzureAdApi"),
    jwtBearerScheme:
        JwtBearerDefaults.AuthenticationScheme);

if (developmentAuthenticationEnabled)
{
    authentication.AddScheme<
        AuthenticationSchemeOptions,
        DevelopmentAuthenticationHandler>(
            DevelopmentAuthenticationDefaults.Scheme,
            _ =>
            {
            });
}

var requiredRole =
    builder.Configuration["Authorization:RequiredApplicationRole"]
    ?? "ServerEngine.Access";

builder.Services.AddAuthorization(authorization =>
{
    authorization.AddPolicy("ServerEngine", policy =>
    {
        if (developmentAuthenticationEnabled)
        {
            policy.AddAuthenticationSchemes(
                JwtBearerDefaults.AuthenticationScheme,
                DevelopmentAuthenticationDefaults.Scheme);
        }
        else
        {
            policy.AddAuthenticationSchemes(
                JwtBearerDefaults.AuthenticationScheme);
        }

        policy.RequireAuthenticatedUser();
        policy.RequireRole(requiredRole);
    });

    authorization.AddPolicy("LocalAdmin", policy =>
    {
        policy.RequireAssertion(context =>
        {
            if (context.Resource is not HttpContext httpContext)
            {
                return false;
            }

            var remote =
                httpContext.Connection.RemoteIpAddress;

            return remote is not null &&
                   IPAddress.IsLoopback(remote);
        });
    });
});

var connectionString =
    builder.Configuration.GetConnectionString("LicenseDatabase")
    ?? throw new InvalidOperationException(
        "ConnectionStrings:LicenseDatabase is required.");

builder.Services.AddDbContextFactory<LicenseDbContext>(database =>
{
    database.UseSqlServer(connectionString);
});

builder.Services.AddSingleton<LeaseSigner>();
builder.Services.AddScoped<LeaseService>();

builder.Services
    .AddRazorComponents()
    .AddInteractiveServerComponents();

var app = builder.Build();

if (builder.Configuration.GetValue<bool>("Database:EnsureCreated"))
{
    await using var scope =
        app.Services.CreateAsyncScope();

    var factory =
        scope.ServiceProvider
            .GetRequiredService<IDbContextFactory<LicenseDbContext>>();

    await using var database =
        await factory.CreateDbContextAsync();

    await database.Database.EnsureCreatedAsync();
}

app.UseHttpsRedirection();
app.UseStaticFiles();

app.UseAuthentication();
app.UseAuthorization();
app.UseAntiforgery();

app.MapLeaseEndpoints();

app.MapRazorComponents<App>()
    .AddInteractiveServerRenderMode()
    .RequireAuthorization("LocalAdmin");

app.Run();
'@

$files["LicenseService\Data\Entities.cs"] = @'
namespace LicenseService.Data;

public sealed class Customer
{
    public Guid Id { get; set; } = Guid.NewGuid();

    public string Name { get; set; } = string.Empty;

    public string EntraTenantId { get; set; } = string.Empty;

    public string EntraClientId { get; set; } = string.Empty;

    public bool Enabled { get; set; } = true;

    public List<Entitlement> Entitlements { get; set; } = [];

    public List<ServerInstance> ServerInstances { get; set; } = [];
}

public sealed class Entitlement
{
    public Guid Id { get; set; } = Guid.NewGuid();

    public Guid CustomerId { get; set; }

    public Customer Customer { get; set; } = null!;

    public string Product { get; set; } = string.Empty;

    public DateTimeOffset ExpiresAtUtc { get; set; }

    public int MaxActiveServers { get; set; }

    public int MaxConnectionsPerServer { get; set; }

    public int LeaseMinutes { get; set; }

    public bool Enabled { get; set; } = true;
}

public sealed class ServerInstance
{
    public string Id { get; set; } = string.Empty;

    public Guid CustomerId { get; set; }

    public Customer Customer { get; set; } = null!;

    public DateTimeOffset FirstSeenUtc { get; set; }

    public DateTimeOffset LastSeenUtc { get; set; }

    public bool Disabled { get; set; }
}

public sealed class LeaseRecord
{
    public Guid Id { get; set; } = Guid.NewGuid();

    public string ServerInstanceId { get; set; } = string.Empty;

    public Guid CustomerId { get; set; }

    public Guid EntitlementId { get; set; }

    public string Product { get; set; } = string.Empty;

    public DateTimeOffset IssuedAtUtc { get; set; }

    public DateTimeOffset ExpiresAtUtc { get; set; }

    public DateTimeOffset? ReleasedAtUtc { get; set; }

    public int MaxConnections { get; set; }
}
'@

$files["LicenseService\Data\LicenseDbContext.cs"] = @'
using Microsoft.EntityFrameworkCore;

namespace LicenseService.Data;

public sealed class LicenseDbContext(
    DbContextOptions<LicenseDbContext> options)
    : DbContext(options)
{
    public DbSet<Customer> Customers =>
        Set<Customer>();

    public DbSet<Entitlement> Entitlements =>
        Set<Entitlement>();

    public DbSet<ServerInstance> ServerInstances =>
        Set<ServerInstance>();

    public DbSet<LeaseRecord> Leases =>
        Set<LeaseRecord>();

    protected override void OnModelCreating(
        ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<Customer>(entity =>
        {
            entity.Property(value => value.Name)
                .HasMaxLength(200);

            entity.Property(value => value.EntraTenantId)
                .HasMaxLength(128);

            entity.Property(value => value.EntraClientId)
                .HasMaxLength(128);

            entity.HasIndex(
                    value =>
                        new
                        {
                            value.EntraTenantId,
                            value.EntraClientId,
                        })
                .IsUnique();
        });

        modelBuilder.Entity<Entitlement>(entity =>
        {
            entity.Property(value => value.Product)
                .HasMaxLength(64);

            entity.HasIndex(
                    value =>
                        new
                        {
                            value.CustomerId,
                            value.Product,
                        })
                .IsUnique();

            entity.HasOne(value => value.Customer)
                .WithMany(value => value.Entitlements)
                .HasForeignKey(value => value.CustomerId)
                .OnDelete(DeleteBehavior.Cascade);
        });

        modelBuilder.Entity<ServerInstance>(entity =>
        {
            entity.Property(value => value.Id)
                .HasMaxLength(128);

            entity.HasOne(value => value.Customer)
                .WithMany(value => value.ServerInstances)
                .HasForeignKey(value => value.CustomerId)
                .OnDelete(DeleteBehavior.Cascade);

            entity.HasIndex(value => value.CustomerId);
        });

        modelBuilder.Entity<LeaseRecord>(entity =>
        {
            entity.Property(value => value.ServerInstanceId)
                .HasMaxLength(128);

            entity.Property(value => value.Product)
                .HasMaxLength(64);

            entity.HasIndex(
                value =>
                    new
                    {
                        value.CustomerId,
                        value.ExpiresAtUtc,
                        value.ReleasedAtUtc,
                    });

            entity.HasIndex(
                value =>
                    new
                    {
                        value.ServerInstanceId,
                        value.ExpiresAtUtc,
                        value.ReleasedAtUtc,
                    });
        });
    }
}
'@

$files["LicenseService\Security\CallerIdentity.cs"] = @'
using System.Security.Claims;

namespace LicenseService.Security;

public sealed record CallerIdentity(
    string TenantId,
    string ClientId);

public static class CallerIdentityReader
{
    public static bool TryRead(
        ClaimsPrincipal principal,
        out CallerIdentity identity)
    {
        var tenantId =
            principal.FindFirstValue("tid");

        var clientId =
            principal.FindFirstValue("azp");

        if (string.IsNullOrWhiteSpace(clientId))
        {
            clientId =
                principal.FindFirstValue("appid");
        }

        if (string.IsNullOrWhiteSpace(tenantId) ||
            string.IsNullOrWhiteSpace(clientId))
        {
            identity =
                new CallerIdentity(
                    string.Empty,
                    string.Empty);

            return false;
        }

        identity =
            new CallerIdentity(
                tenantId,
                clientId);

        return true;
    }
}
'@

$files["LicenseService\Security\DevelopmentAuthenticationHandler.cs"] = @'
using Microsoft.AspNetCore.Authentication;
using Microsoft.Extensions.Options;
using System.Security.Claims;
using System.Text.Encodings.Web;

namespace LicenseService.Security;

public static class DevelopmentAuthenticationDefaults
{
    public const string Scheme =
        "CW.Development";
}

public sealed class DevelopmentAuthenticationHandler
    : AuthenticationHandler<AuthenticationSchemeOptions>
{
    private readonly IConfiguration configuration;

    public DevelopmentAuthenticationHandler(
        IOptionsMonitor<AuthenticationSchemeOptions> options,
        ILoggerFactory logger,
        UrlEncoder encoder,
        IConfiguration configuration)
        : base(
            options,
            logger,
            encoder)
    {
        this.configuration =
            configuration;
    }

    protected override Task<AuthenticateResult>
        HandleAuthenticateAsync()
    {
        if (!Context.Request.Headers.TryGetValue(
                "X-CW-Development-Authentication",
                out var values) ||
            values.Count != 1 ||
            !string.Equals(
                values[0],
                "1",
                StringComparison.Ordinal))
        {
            return Task.FromResult(
                AuthenticateResult.NoResult());
        }

        var tenantId =
            configuration[
                "DevelopmentAuthentication:TenantId"];

        var clientId =
            configuration[
                "DevelopmentAuthentication:ClientId"];

        var role =
            configuration[
                "Authorization:RequiredApplicationRole"]
            ?? "ServerEngine.Access";

        if (string.IsNullOrWhiteSpace(tenantId) ||
            string.IsNullOrWhiteSpace(clientId))
        {
            return Task.FromResult(
                AuthenticateResult.Fail(
                    "Development authentication requires TenantId and ClientId."));
        }

        var claims =
            new[]
            {
                new Claim("tid", tenantId),
                new Claim("azp", clientId),
                new Claim(ClaimTypes.Role, role),
            };

        var identity =
            new ClaimsIdentity(
                claims,
                Scheme.Name);

        var principal =
            new ClaimsPrincipal(identity);

        var ticket =
            new AuthenticationTicket(
                principal,
                Scheme.Name);

        return Task.FromResult(
            AuthenticateResult.Success(ticket));
    }
}
'@

$files["LicenseService\Licensing\LeaseContracts.cs"] = @'
namespace LicenseService.Licensing;

public sealed record AcquireLeaseRequest(
    string ServerInstanceId,
    string Product);

public sealed record RenewLeaseRequest(
    Guid LeaseId,
    string ServerInstanceId);

public sealed record ReleaseLeaseRequest(
    Guid LeaseId,
    string ServerInstanceId);

public sealed record LeaseResponse(
    Guid LeaseId,
    string ServerInstanceId,
    string Product,
    DateTimeOffset IssuedAtUtc,
    DateTimeOffset ExpiresAtUtc,
    int MaxConnections,
    string LeaseToken);

public enum LeaseFailure
{
    None = 0,
    InvalidRequest,
    InvalidCaller,
    CustomerNotFound,
    CustomerDisabled,
    EntitlementNotFound,
    EntitlementExpired,
    InstanceDisabled,
    ActiveServerLimitReached,
    LeaseNotFound,
    SigningFailed,
}

public sealed record LeaseResult(
    LeaseResponse? Lease,
    LeaseFailure Failure,
    string Detail)
{
    public bool Succeeded =>
        Lease is not null &&
        Failure == LeaseFailure.None;

    public static LeaseResult Success(
        LeaseResponse lease) =>
        new(
            lease,
            LeaseFailure.None,
            string.Empty);

    public static LeaseResult Failed(
        LeaseFailure failure,
        string detail) =>
        new(
            null,
            failure,
            detail);
}
'@

$files["LicenseService\Licensing\LeaseSigner.cs"] = @'
using Microsoft.IdentityModel.JsonWebTokens;
using Microsoft.IdentityModel.Tokens;
using System.Security.Claims;
using System.Security.Cryptography.X509Certificates;

namespace LicenseService.Licensing;

public sealed class LeaseSigner
{
    private readonly IConfiguration configuration;

    public LeaseSigner(
        IConfiguration configuration)
    {
        this.configuration =
            configuration;
    }

    public string CreateToken(
        Guid leaseId,
        Guid customerId,
        string serverInstanceId,
        string product,
        DateTimeOffset issuedAtUtc,
        DateTimeOffset expiresAtUtc,
        int maxConnections)
    {
        using var certificate =
            LoadCertificate();

        var credentials =
            new X509SigningCredentials(
                certificate,
                SecurityAlgorithms.RsaSha256);

        var descriptor =
            new SecurityTokenDescriptor
            {
                Issuer =
                    Required(
                        "Lease:Issuer"),
                Audience =
                    Required(
                        "Lease:Audience"),
                IssuedAt =
                    issuedAtUtc.UtcDateTime,
                NotBefore =
                    issuedAtUtc.UtcDateTime,
                Expires =
                    expiresAtUtc.UtcDateTime,
                SigningCredentials =
                    credentials,
                Subject =
                    new ClaimsIdentity(
                        new[]
                        {
                            new Claim(
                                JwtRegisteredClaimNames.Jti,
                                leaseId.ToString()),
                            new Claim(
                                JwtRegisteredClaimNames.Sub,
                                serverInstanceId),
                            new Claim(
                                "customer_id",
                                customerId.ToString()),
                            new Claim(
                                "product",
                                product),
                            new Claim(
                                "max_connections",
                                maxConnections.ToString(),
                                ClaimValueTypes.Integer32),
                        }),
            };

        return new JsonWebTokenHandler()
            .CreateToken(
                descriptor);
    }

    private X509Certificate2 LoadCertificate()
    {
        var thumbprint =
            Required(
                "Lease:SigningCertificateThumbprint")
                .Replace(
                    " ",
                    string.Empty,
                    StringComparison.Ordinal)
                .ToUpperInvariant();

        var location =
            configuration[
                "Lease:SigningCertificateStore"]
            ?? "CurrentUser";

        var storeLocation =
            string.Equals(
                location,
                "LocalMachine",
                StringComparison.OrdinalIgnoreCase)
                ? StoreLocation.LocalMachine
                : StoreLocation.CurrentUser;

        using var store =
            new X509Store(
                StoreName.My,
                storeLocation);

        store.Open(
            OpenFlags.ReadOnly);

        var matches =
            store.Certificates.Find(
                X509FindType.FindByThumbprint,
                thumbprint,
                validOnly:
                    false);

        var certificate =
            matches
                .OfType<X509Certificate2>()
                .FirstOrDefault(
                    value =>
                        value.HasPrivateKey);

        return certificate is null
            ? throw new InvalidOperationException(
                $"Lease signing certificate '{thumbprint}' was not found in {storeLocation}/My.")
            : new X509Certificate2(
                certificate);
    }

    private string Required(
        string key)
    {
        var value =
            configuration[key];

        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException(
                $"{key} is required.")
            : value;
    }
}
'@

$files["LicenseService\Licensing\LeaseService.cs"] = @'
using LicenseService.Data;
using LicenseService.Security;
using Microsoft.EntityFrameworkCore;
using System.Data;

namespace LicenseService.Licensing;

public sealed class LeaseService
{
    private readonly IDbContextFactory<LicenseDbContext> databaseFactory;
    private readonly LeaseSigner signer;
    private readonly IConfiguration configuration;
    private readonly ILogger<LeaseService> logger;

    public LeaseService(
        IDbContextFactory<LicenseDbContext> databaseFactory,
        LeaseSigner signer,
        IConfiguration configuration,
        ILogger<LeaseService> logger)
    {
        this.databaseFactory =
            databaseFactory;

        this.signer =
            signer;

        this.configuration =
            configuration;

        this.logger =
            logger;
    }

    public Task<LeaseResult> AcquireAsync(
        CallerIdentity caller,
        AcquireLeaseRequest request,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(
                request.ServerInstanceId) ||
            request.ServerInstanceId.Length > 128 ||
            string.IsNullOrWhiteSpace(
                request.Product) ||
            request.Product.Length > 64)
        {
            return Task.FromResult(
                LeaseResult.Failed(
                    LeaseFailure.InvalidRequest,
                    "Invalid Server instance or product."));
        }

        return IssueAsync(
            caller,
            request.ServerInstanceId,
            request.Product,
            null,
            cancellationToken);
    }

    public Task<LeaseResult> RenewAsync(
        CallerIdentity caller,
        RenewLeaseRequest request,
        CancellationToken cancellationToken)
    {
        if (request.LeaseId == Guid.Empty ||
            string.IsNullOrWhiteSpace(
                request.ServerInstanceId) ||
            request.ServerInstanceId.Length > 128)
        {
            return Task.FromResult(
                LeaseResult.Failed(
                    LeaseFailure.InvalidRequest,
                    "Invalid lease or Server instance."));
        }

        return IssueAsync(
            caller,
            request.ServerInstanceId,
            null,
            request.LeaseId,
            cancellationToken);
    }

    private async Task<LeaseResult> IssueAsync(
        CallerIdentity caller,
        string serverInstanceId,
        string? acquireProduct,
        Guid? renewLeaseId,
        CancellationToken cancellationToken)
    {
        await using var database =
            await databaseFactory.CreateDbContextAsync(
                cancellationToken);

        await using var transaction =
            await database.Database.BeginTransactionAsync(
                IsolationLevel.Serializable,
                cancellationToken);

        var now =
            DateTimeOffset.UtcNow;

        var customer =
            await database.Customers
                .SingleOrDefaultAsync(
                    value =>
                        value.EntraTenantId == caller.TenantId &&
                        value.EntraClientId == caller.ClientId,
                    cancellationToken);

        if (customer is null)
        {
            return LeaseResult.Failed(
                LeaseFailure.CustomerNotFound,
                "No customer is mapped to the authenticated ServerEngine application.");
        }

        if (!customer.Enabled)
        {
            return LeaseResult.Failed(
                LeaseFailure.CustomerDisabled,
                "Customer is disabled.");
        }

        string product;

        if (renewLeaseId.HasValue)
        {
            var renewalIdentity =
                await database.Leases
                    .AsNoTracking()
                    .Where(
                        value =>
                            value.Id == renewLeaseId.Value &&
                            value.CustomerId == customer.Id &&
                            value.ServerInstanceId == serverInstanceId)
                    .Select(
                        value =>
                            new
                            {
                                value.Product,
                            })
                    .SingleOrDefaultAsync(
                        cancellationToken);

            if (renewalIdentity is null)
            {
                return LeaseResult.Failed(
                    LeaseFailure.LeaseNotFound,
                    "Lease was not found.");
            }

            product =
                renewalIdentity.Product;
        }
        else
        {
            product =
                acquireProduct!;
        }

        var entitlement =
            await database.Entitlements
                .FromSqlInterpolated(
                    $@"SELECT *
                       FROM [Entitlements] WITH (UPDLOCK, HOLDLOCK)
                       WHERE [CustomerId] = {customer.Id}
                         AND [Product] = {product}")
                .SingleOrDefaultAsync(
                    cancellationToken);

        if (entitlement is null ||
            !entitlement.Enabled)
        {
            return LeaseResult.Failed(
                LeaseFailure.EntitlementNotFound,
                "No active entitlement exists for this product.");
        }

        if (entitlement.ExpiresAtUtc <= now)
        {
            return LeaseResult.Failed(
                LeaseFailure.EntitlementExpired,
                "The entitlement has expired.");
        }

        LeaseRecord? currentLease;

        if (renewLeaseId.HasValue)
        {
            currentLease =
                await database.Leases
                    .SingleOrDefaultAsync(
                        value =>
                            value.Id == renewLeaseId.Value &&
                            value.CustomerId == customer.Id &&
                            value.ServerInstanceId == serverInstanceId &&
                            value.Product == product &&
                            value.ReleasedAtUtc == null &&
                            value.ExpiresAtUtc > now,
                        cancellationToken);

            if (currentLease is null)
            {
                return LeaseResult.Failed(
                    LeaseFailure.LeaseNotFound,
                    "Active lease was not found.");
            }
        }
        else
        {
            currentLease =
                await database.Leases
                    .Where(
                        value =>
                            value.CustomerId == customer.Id &&
                            value.ServerInstanceId == serverInstanceId &&
                            value.Product == product &&
                            value.ReleasedAtUtc == null &&
                            value.ExpiresAtUtc > now)
                    .OrderByDescending(
                        value =>
                            value.ExpiresAtUtc)
                    .FirstOrDefaultAsync(
                        cancellationToken);
        }

        var instance =
            await database.ServerInstances
                .SingleOrDefaultAsync(
                    value =>
                        value.Id == serverInstanceId,
                    cancellationToken);

        if (instance is null)
        {
            if (renewLeaseId.HasValue)
            {
                return LeaseResult.Failed(
                    LeaseFailure.LeaseNotFound,
                    "Server instance for the active lease was not found.");
            }

            instance =
                new ServerInstance
                {
                    Id =
                        serverInstanceId,
                    CustomerId =
                        customer.Id,
                    FirstSeenUtc =
                        now,
                    LastSeenUtc =
                        now,
                };

            database.ServerInstances.Add(
                instance);
        }
        else
        {
            if (instance.CustomerId !=
                customer.Id)
            {
                return LeaseResult.Failed(
                    LeaseFailure.InvalidCaller,
                    "Server instance belongs to another customer.");
            }

            if (instance.Disabled)
            {
                return LeaseResult.Failed(
                    LeaseFailure.InstanceDisabled,
                    "Server instance is disabled.");
            }

            instance.LastSeenUtc =
                now;
        }

        if (currentLease is null)
        {
            var activeServers =
                await database.Leases
                    .Where(
                        value =>
                            value.CustomerId == customer.Id &&
                            value.Product == product &&
                            value.ReleasedAtUtc == null &&
                            value.ExpiresAtUtc > now)
                    .Select(
                        value =>
                            value.ServerInstanceId)
                    .Distinct()
                    .CountAsync(
                        cancellationToken);

            if (activeServers >=
                entitlement.MaxActiveServers)
            {
                return LeaseResult.Failed(
                    LeaseFailure.ActiveServerLimitReached,
                    "Maximum active ServerEngine instance count has been reached.");
            }
        }

        var leaseMinutes =
            entitlement.LeaseMinutes > 0
                ? entitlement.LeaseMinutes
                : configuration.GetValue(
                    "Lease:DefaultMinutes",
                    60);

        var expiresAt =
            now.AddMinutes(
                leaseMinutes);

        if (expiresAt >
            entitlement.ExpiresAtUtc)
        {
            expiresAt =
                entitlement.ExpiresAtUtc;
        }

        var lease =
            new LeaseRecord
            {
                Id =
                    Guid.NewGuid(),
                ServerInstanceId =
                    serverInstanceId,
                CustomerId =
                    customer.Id,
                EntitlementId =
                    entitlement.Id,
                Product =
                    product,
                IssuedAtUtc =
                    now,
                ExpiresAtUtc =
                    expiresAt,
                MaxConnections =
                    entitlement.MaxConnectionsPerServer,
            };

        string token;

        try
        {
            token =
                signer.CreateToken(
                    lease.Id,
                    customer.Id,
                    lease.ServerInstanceId,
                    lease.Product,
                    lease.IssuedAtUtc,
                    lease.ExpiresAtUtc,
                    lease.MaxConnections);
        }
        catch (Exception exception)
        {
            logger.LogError(
                exception,
                "Lease signing failed for Server instance {ServerInstanceId}.",
                serverInstanceId);

            return LeaseResult.Failed(
                LeaseFailure.SigningFailed,
                "Lease signing failed.");
        }

        if (currentLease is not null)
        {
            currentLease.ReleasedAtUtc =
                now;
        }

        database.Leases.Add(
            lease);

        await database.SaveChangesAsync(
            cancellationToken);

        await transaction.CommitAsync(
            cancellationToken);

        return LeaseResult.Success(
            new LeaseResponse(
                lease.Id,
                lease.ServerInstanceId,
                lease.Product,
                lease.IssuedAtUtc,
                lease.ExpiresAtUtc,
                lease.MaxConnections,
                token));
    }

    public async Task<bool> ReleaseAsync(
        CallerIdentity caller,
        ReleaseLeaseRequest request,
        CancellationToken cancellationToken)
    {
        await using var database =
            await databaseFactory.CreateDbContextAsync(
                cancellationToken);

        var lease =
            await database.Leases
                .SingleOrDefaultAsync(
                    value =>
                        value.Id == request.LeaseId &&
                        value.ServerInstanceId == request.ServerInstanceId,
                    cancellationToken);

        if (lease is null)
        {
            return false;
        }

        var customerMatches =
            await database.Customers
                .AnyAsync(
                    value =>
                        value.Id == lease.CustomerId &&
                        value.EntraTenantId == caller.TenantId &&
                        value.EntraClientId == caller.ClientId,
                    cancellationToken);

        if (!customerMatches)
        {
            return false;
        }

        if (lease.ReleasedAtUtc is null)
        {
            lease.ReleasedAtUtc =
                DateTimeOffset.UtcNow;

            await database.SaveChangesAsync(
                cancellationToken);
        }

        return true;
    }
}
'@

$files["LicenseService\Api\LeaseEndpoints.cs"] = @'
using LicenseService.Licensing;
using LicenseService.Security;

namespace LicenseService.Api;

public static class LeaseEndpoints
{
    public static void MapLeaseEndpoints(
        this WebApplication app)
    {
        var group =
            app.MapGroup("/api/v1/lease")
                .RequireAuthorization(
                    "ServerEngine");

        group.MapPost(
            "/acquire",
            AcquireAsync);

        group.MapPost(
            "/renew",
            RenewAsync);

        group.MapPost(
            "/release",
            ReleaseAsync);
    }

    private static async Task<IResult> AcquireAsync(
        HttpContext context,
        AcquireLeaseRequest request,
        LeaseService leases,
        CancellationToken cancellationToken)
    {
        if (!CallerIdentityReader.TryRead(
                context.User,
                out var caller))
        {
            return Results.Forbid();
        }

        var result =
            await leases.AcquireAsync(
                caller,
                request,
                cancellationToken);

        return ToHttpResult(
            result);
    }

    private static async Task<IResult> RenewAsync(
        HttpContext context,
        RenewLeaseRequest request,
        LeaseService leases,
        CancellationToken cancellationToken)
    {
        if (!CallerIdentityReader.TryRead(
                context.User,
                out var caller))
        {
            return Results.Forbid();
        }

        var result =
            await leases.RenewAsync(
                caller,
                request,
                cancellationToken);

        return ToHttpResult(
            result);
    }

    private static async Task<IResult> ReleaseAsync(
        HttpContext context,
        ReleaseLeaseRequest request,
        LeaseService leases,
        CancellationToken cancellationToken)
    {
        if (!CallerIdentityReader.TryRead(
                context.User,
                out var caller))
        {
            return Results.Forbid();
        }

        var released =
            await leases.ReleaseAsync(
                caller,
                request,
                cancellationToken);

        return released
            ? Results.NoContent()
            : Results.NotFound();
    }

    private static IResult ToHttpResult(
        LeaseResult result)
    {
        if (result.Succeeded)
        {
            return Results.Ok(
                result.Lease);
        }

        return result.Failure switch
        {
            LeaseFailure.InvalidRequest =>
                Results.BadRequest(
                    new
                    {
                        error =
                            "invalid_request",
                        detail =
                            result.Detail,
                    }),

            LeaseFailure.ActiveServerLimitReached =>
                Results.Conflict(
                    new
                    {
                        error =
                            "active_server_limit_reached",
                        detail =
                            result.Detail,
                    }),

            LeaseFailure.CustomerNotFound or
            LeaseFailure.CustomerDisabled or
            LeaseFailure.EntitlementNotFound or
            LeaseFailure.EntitlementExpired or
            LeaseFailure.InstanceDisabled =>
                Results.Json(
                    new
                    {
                        error =
                            result.Failure.ToString(),
                        detail =
                            result.Detail,
                    },
                    statusCode:
                        StatusCodes.Status403Forbidden),

            LeaseFailure.LeaseNotFound =>
                Results.NotFound(
                    new
                    {
                        error =
                            "lease_not_found",
                        detail =
                            result.Detail,
                    }),

            LeaseFailure.SigningFailed =>
                Results.Json(
                    new
                    {
                        error =
                            "lease_signing_failed",
                        detail =
                            "Lease signing failed.",
                    },
                    statusCode:
                        StatusCodes.Status500InternalServerError),

            _ =>
                Results.BadRequest(
                    new
                    {
                        error =
                            result.Failure.ToString(),
                        detail =
                            result.Detail,
                    }),
        };
    }
}
'@

$files["LicenseService\Components\_Imports.razor"] = @'
@using System.ComponentModel.DataAnnotations
@using Microsoft.AspNetCore.Components
@using Microsoft.AspNetCore.Components.Forms
@using Microsoft.AspNetCore.Components.Routing
@using Microsoft.AspNetCore.Components.Web
@using static Microsoft.AspNetCore.Components.Web.RenderMode
@using Microsoft.EntityFrameworkCore
@using LicenseService.Data
@using LicenseService.Licensing
@using LicenseService.Components
@using LicenseService.Components.Layout
'@

$files["LicenseService\Components\App.razor"] = @'
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <base href="/" />
    <link rel="stylesheet" href="app.css" />
    <HeadOutlet @rendermode="InteractiveServer" />
</head>
<body>
    <Routes @rendermode="InteractiveServer" />
    <script src="_framework/blazor.web.js"></script>
</body>
</html>
'@

$files["LicenseService\Components\Routes.razor"] = @'
<Router AppAssembly="typeof(Program).Assembly">
    <Found Context="routeData">
        <RouteView RouteData="routeData" DefaultLayout="typeof(MainLayout)" />
        <FocusOnNavigate RouteData="routeData" Selector="h1" />
    </Found>
</Router>
'@

$files["LicenseService\Components\Layout\MainLayout.razor"] = @'
@inherits LayoutComponentBase

<div class="shell">
    <aside>
        <h2>CW License Service</h2>
        <nav>
            <NavLink href="/" Match="NavLinkMatch.All">Dashboard</NavLink>
            <NavLink href="/admin/customers">Customers</NavLink>
            <NavLink href="/admin/entitlements">Entitlements</NavLink>
            <NavLink href="/admin/servers">Servers</NavLink>
        </nav>
    </aside>

    <main>
        @Body
    </main>
</div>
'@

$files["LicenseService\Components\Pages\Home.razor"] = @'
@page "/"
@inject IDbContextFactory<LicenseDbContext> DatabaseFactory

<PageTitle>License Service</PageTitle>

<h1>License Service</h1>

@if (loading)
{
    <p>Loading...</p>
}
else
{
    <div class="cards">
        <section>
            <strong>@customers</strong>
            <span>Customers</span>
        </section>
        <section>
            <strong>@entitlements</strong>
            <span>Entitlements</span>
        </section>
        <section>
            <strong>@servers</strong>
            <span>Server instances</span>
        </section>
        <section>
            <strong>@activeLeases</strong>
            <span>Active leases</span>
        </section>
    </div>
}

@code {
    private bool loading = true;
    private int customers;
    private int entitlements;
    private int servers;
    private int activeLeases;

    protected override async Task OnInitializedAsync()
    {
        await using var database =
            await DatabaseFactory.CreateDbContextAsync();

        var now =
            DateTimeOffset.UtcNow;

        customers =
            await database.Customers.CountAsync();

        entitlements =
            await database.Entitlements.CountAsync();

        servers =
            await database.ServerInstances.CountAsync();

        activeLeases =
            await database.Leases.CountAsync(
                value =>
                    value.ReleasedAtUtc == null &&
                    value.ExpiresAtUtc > now);

        loading = false;
    }
}
'@

$files["LicenseService\Components\Pages\Customers.razor"] = @'
@page "/admin/customers"
@inject IDbContextFactory<LicenseDbContext> DatabaseFactory

<PageTitle>Customers</PageTitle>

<h1>Customers</h1>

<div class="form-grid">
    <label>Name</label>
    <input @bind="name" />

    <label>Entra tenant ID</label>
    <input @bind="tenantId" />

    <label>Entra client ID</label>
    <input @bind="clientId" />

    <button @onclick="CreateAsync">Create</button>
</div>

@if (!string.IsNullOrWhiteSpace(message))
{
    <p>@message</p>
}

<table>
    <thead>
        <tr>
            <th>Name</th>
            <th>Tenant</th>
            <th>Client</th>
            <th>Enabled</th>
        </tr>
    </thead>
    <tbody>
        @foreach (var customer in customers)
        {
            <tr>
                <td>@customer.Name</td>
                <td>@customer.EntraTenantId</td>
                <td>@customer.EntraClientId</td>
                <td>@customer.Enabled</td>
            </tr>
        }
    </tbody>
</table>

@code {
    private List<Customer> customers = [];
    private string name = string.Empty;
    private string tenantId = string.Empty;
    private string clientId = string.Empty;
    private string message = string.Empty;

    protected override async Task OnInitializedAsync()
    {
        await ReloadAsync();
    }

    private async Task CreateAsync()
    {
        if (string.IsNullOrWhiteSpace(name) ||
            string.IsNullOrWhiteSpace(tenantId) ||
            string.IsNullOrWhiteSpace(clientId))
        {
            message = "All fields are required.";
            return;
        }

        await using var database =
            await DatabaseFactory.CreateDbContextAsync();

        database.Customers.Add(
            new Customer
            {
                Name = name.Trim(),
                EntraTenantId = tenantId.Trim(),
                EntraClientId = clientId.Trim(),
                Enabled = true,
            });

        await database.SaveChangesAsync();

        name = string.Empty;
        tenantId = string.Empty;
        clientId = string.Empty;
        message = "Customer created.";

        await ReloadAsync();
    }

    private async Task ReloadAsync()
    {
        await using var database =
            await DatabaseFactory.CreateDbContextAsync();

        customers =
            await database.Customers
                .OrderBy(value => value.Name)
                .AsNoTracking()
                .ToListAsync();
    }
}
'@

$files["LicenseService\Components\Pages\Entitlements.razor"] = @'
@page "/admin/entitlements"
@inject IDbContextFactory<LicenseDbContext> DatabaseFactory

<PageTitle>Entitlements</PageTitle>

<h1>Entitlements</h1>

<div class="form-grid">
    <label>Customer</label>
    <select @bind="customerId">
        <option value="">Select customer</option>
        @foreach (var customer in customers)
        {
            <option value="@customer.Id">@customer.Name</option>
        }
    </select>

    <label>Product</label>
    <input @bind="product" />

    <label>Expires UTC</label>
    <input type="datetime-local" @bind="expiresLocal" />

    <label>Max active servers</label>
    <input type="number" @bind="maxActiveServers" />

    <label>Max connections/server</label>
    <input type="number" @bind="maxConnectionsPerServer" />

    <label>Lease minutes</label>
    <input type="number" @bind="leaseMinutes" />

    <button @onclick="CreateAsync">Create</button>
</div>

@if (!string.IsNullOrWhiteSpace(message))
{
    <p>@message</p>
}

<table>
    <thead>
        <tr>
            <th>Customer</th>
            <th>Product</th>
            <th>Expires UTC</th>
            <th>Max servers</th>
            <th>Max connections</th>
            <th>Lease min</th>
            <th>Enabled</th>
        </tr>
    </thead>
    <tbody>
        @foreach (var entitlement in entitlements)
        {
            <tr>
                <td>@entitlement.Customer.Name</td>
                <td>@entitlement.Product</td>
                <td>@entitlement.ExpiresAtUtc</td>
                <td>@entitlement.MaxActiveServers</td>
                <td>@entitlement.MaxConnectionsPerServer</td>
                <td>@entitlement.LeaseMinutes</td>
                <td>@entitlement.Enabled</td>
            </tr>
        }
    </tbody>
</table>

@code {
    private List<Customer> customers = [];
    private List<Entitlement> entitlements = [];

    private string customerId = string.Empty;
    private string product = "ServerEngineV4";
    private DateTime expiresLocal = DateTime.Now.AddYears(1);
    private int maxActiveServers = 10;
    private int maxConnectionsPerServer = 32;
    private int leaseMinutes = 60;
    private string message = string.Empty;

    protected override async Task OnInitializedAsync()
    {
        await ReloadAsync();
    }

    private async Task CreateAsync()
    {
        if (!Guid.TryParse(
                customerId,
                out var parsedCustomerId) ||
            string.IsNullOrWhiteSpace(product) ||
            maxActiveServers <= 0 ||
            maxConnectionsPerServer <= 0 ||
            leaseMinutes <= 0)
        {
            message = "Invalid entitlement values.";
            return;
        }

        await using var database =
            await DatabaseFactory.CreateDbContextAsync();

        database.Entitlements.Add(
            new Entitlement
            {
                CustomerId = parsedCustomerId,
                Product = product.Trim(),
                ExpiresAtUtc =
                    new DateTimeOffset(
                        DateTime.SpecifyKind(
                            expiresLocal,
                            DateTimeKind.Local))
                    .ToUniversalTime(),
                MaxActiveServers = maxActiveServers,
                MaxConnectionsPerServer = maxConnectionsPerServer,
                LeaseMinutes = leaseMinutes,
                Enabled = true,
            });

        await database.SaveChangesAsync();

        message = "Entitlement created.";

        await ReloadAsync();
    }

    private async Task ReloadAsync()
    {
        await using var database =
            await DatabaseFactory.CreateDbContextAsync();

        customers =
            await database.Customers
                .OrderBy(value => value.Name)
                .AsNoTracking()
                .ToListAsync();

        entitlements =
            await database.Entitlements
                .Include(value => value.Customer)
                .OrderBy(value => value.Customer.Name)
                .ThenBy(value => value.Product)
                .AsNoTracking()
                .ToListAsync();
    }
}
'@

$files["LicenseService\Components\Pages\Servers.razor"] = @'
@page "/admin/servers"
@inject IDbContextFactory<LicenseDbContext> DatabaseFactory

<PageTitle>Servers</PageTitle>

<h1>Server instances</h1>

<table>
    <thead>
        <tr>
            <th>ID</th>
            <th>Customer</th>
            <th>First seen UTC</th>
            <th>Last seen UTC</th>
            <th>Disabled</th>
        </tr>
    </thead>
    <tbody>
        @foreach (var server in servers)
        {
            <tr>
                <td>@server.Id</td>
                <td>@server.Customer.Name</td>
                <td>@server.FirstSeenUtc</td>
                <td>@server.LastSeenUtc</td>
                <td>@server.Disabled</td>
            </tr>
        }
    </tbody>
</table>

@code {
    private List<ServerInstance> servers = [];

    protected override async Task OnInitializedAsync()
    {
        await using var database =
            await DatabaseFactory.CreateDbContextAsync();

        servers =
            await database.ServerInstances
                .Include(value => value.Customer)
                .OrderByDescending(value => value.LastSeenUtc)
                .AsNoTracking()
                .ToListAsync();
    }
}
'@

$files["LicenseService\wwwroot\app.css"] = @'
html, body {
    font-family: "Segoe UI", Arial, sans-serif;
    margin: 0;
    min-height: 100%;
}

.shell {
    display: grid;
    grid-template-columns: 240px 1fr;
    min-height: 100vh;
}

aside {
    padding: 24px;
    border-right: 1px solid #d8d8d8;
}

nav {
    display: grid;
    gap: 10px;
}

nav a {
    text-decoration: none;
}

main {
    padding: 28px;
}

.cards {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
    gap: 16px;
}

.cards section {
    border: 1px solid #d8d8d8;
    border-radius: 6px;
    padding: 18px;
    display: grid;
    gap: 6px;
}

.cards strong {
    font-size: 28px;
}

.form-grid {
    display: grid;
    grid-template-columns: 220px minmax(280px, 520px);
    gap: 10px;
    margin-bottom: 24px;
    align-items: center;
}

.form-grid button {
    grid-column: 2;
    width: 120px;
}

input, select, button {
    padding: 7px;
}

table {
    border-collapse: collapse;
    width: 100%;
}

th, td {
    border-bottom: 1px solid #d8d8d8;
    padding: 8px;
    text-align: left;
}
'@

$files["README.md"] = @'
# LicenseServiceV1

Standalone licensing service for ServerEngineV4.

See the ServerEngineV4 repository documentation:

- `docs/LICENSE_SERVICE_ARCHITECTURE.md`
- `docs/LICENSE_SERVICE_SETUP.md`

Local start:

```powershell
dotnet dev-certs https --trust
dotnet run --project .\LicenseService\LicenseService.csproj
```

Default development URL:

```text
https://localhost:8443/
```
'@

foreach ($relativePath in $files.Keys) {
    $target = Join-Path $root $relativePath

    if ((Test-Path $target) -and
        -not $Force) {
        throw "Generated file already exists: $target"
    }

    Write-Utf8File `
        -Path $target `
        -Content $files[$relativePath]
}

Write-Host "LicenseServiceV1 source generated at:"
Write-Host "  $root"

if (-not $SkipBuild) {
    Push-Location $root

    try {
        dotnet restore .\LicenseService\LicenseService.csproj

        if ($LASTEXITCODE -ne 0) {
            throw "dotnet restore failed."
        }

        dotnet build .\LicenseService\LicenseService.csproj -c Release

        if ($LASTEXITCODE -ne 0) {
            throw "dotnet build failed."
        }
    }
    finally {
        Pop-Location
    }
}

Write-Host ""
Write-Host "Next:"
Write-Host "  1. dotnet dev-certs https --trust"
Write-Host "  2. create the dedicated lease-signing certificate"
Write-Host "  3. set Lease:SigningCertificateThumbprint in appsettings.json"
Write-Host "  4. start SQL Server"
Write-Host "  5. dotnet run --project `"$root\LicenseService\LicenseService.csproj`""

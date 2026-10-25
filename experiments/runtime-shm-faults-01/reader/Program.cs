using Microsoft.Diagnostics.Tracing.Etlx;
using Microsoft.Diagnostics.Tracing.Parsers.Kernel;
using Microsoft.Diagnostics.Symbols;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;

if (args.Length == 2) {
    using var log = new StreamWriter(args[1] + ".conversion.log");
using var inspectionTrace = TraceLog.OpenOrConvert(args[0], new TraceLogOptions { LocalSymbolsOnly = true, ConversionLog = log });
var events = new Dictionary<string, long>();
foreach (var e in inspectionTrace.Events) {
    var key = e.GetType().Name + ":" + e.EventName;
    events[key] = events.GetValueOrDefault(key) + 1;
}
var modules = inspectionTrace.ModuleFiles.Where(m => m.Name.Contains("ntoskrnl", StringComparison.OrdinalIgnoreCase) ||
    m.Name.Contains("ntdll", StringComparison.OrdinalIgnoreCase) || m.Name.Contains("ServerEngineV4PublishBenchmark", StringComparison.OrdinalIgnoreCase))
    .Select(m => new {
        m.Name, m.PdbSignature, m.PdbAge,
        properties = m.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance)
            .Where(p => p.GetIndexParameters().Length == 0 &&
                (p.PropertyType.IsPrimitive || p.PropertyType == typeof(string) || p.PropertyType == typeof(Guid)))
            .ToDictionary(p => p.Name, p => { try { return p.GetValue(m)?.ToString(); } catch { return "<unavailable>"; } })
    }).ToList();
File.WriteAllText(args[1], JsonSerializer.Serialize(new { modules, events, inspectionTrace.EventsLost, inspectionTrace.Truncated }, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine(JsonSerializer.Serialize(modules));

    return;
}
if (args.Length != 7) throw new ArgumentException("KernelReader trace.etl block pids_csv app_symbols os_manifest output.jsonl capture_manifest");
var block = int.Parse(args[1]);
var pids = args[2].Split(',').Select(int.Parse).ToHashSet();
var output = args[5];
using var conversion = new StreamWriter(output + ".conversion.log");
using var trace = TraceLog.OpenOrConvert(args[0], new TraceLogOptions { LocalSymbolsOnly = true, ConversionLog = conversion });
using var capture = JsonDocument.Parse(File.ReadAllText(args[6]));
var expected = capture.RootElement.GetProperty("symbols").GetProperty("exe");
using var osDoc = JsonDocument.Parse(File.ReadAllText(args[4]));
var appPdb = Path.GetFullPath(Path.Combine(args[3], "ServerEngineV4PublishBenchmark.pdb"));
var osSpecs = osDoc.RootElement.EnumerateArray().ToList();
var allowedPaths = osSpecs.Select(s => Path.GetFullPath(s.GetProperty("pdb").GetString()!)).Append(appPdb).ToHashSet(StringComparer.OrdinalIgnoreCase);
using var symbolLog = new StreamWriter(output + ".symbols.log");
using var reader = new SymbolReader(symbolLog, Path.GetFullPath(args[3]), null);
reader.SecurityCheck = path => allowedPaths.Contains(Path.GetFullPath(path));
using var appNative = reader.OpenNativeSymbolFile(appPdb);
var appModules = trace.ModuleFiles.Where(m => m.Name.Contains("ServerEngineV4PublishBenchmark", StringComparison.OrdinalIgnoreCase)).ToList();
if (appModules.Count == 0) throw new InvalidDataException("Missing app module");
foreach (var m in appModules) {
 if (m.PdbSignature != Guid.Parse(expected.GetProperty("guid").GetString()!) || m.PdbAge != expected.GetProperty("age").GetInt32()) throw new InvalidDataException("App PDB mismatch");
 trace.CodeAddresses.LookupSymbolsForModule(reader, m);
}
var osModules = osSpecs.Select(spec => {
 var path = spec.GetProperty("pdb").GetString()!;
 var hash = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
 if (hash != spec.GetProperty("sha256").GetString()) throw new InvalidDataException("OS PDB changed");
 var guid = Guid.Parse(spec.GetProperty("guid").GetString()!);
 var age = spec.GetProperty("image_age").GetInt32();
 var module = trace.ModuleFiles.Single(m => m.Name == spec.GetProperty("module").GetString() && m.PdbSignature == guid && m.PdbAge == age);
 var native = reader.OpenNativeSymbolFile(path);
 if (native.PdbGuid != guid) throw new InvalidDataException("OS PDB GUID mismatch");
 return new { Module = module, Native = native, Spec = spec };
}).ToList();
var nameCache = new Dictionary<(string, uint), string>();
var eventCounts = new Dictionary<string, long>();
long samples = 0, missing = 0, unresolvedKernelSamples = 0, targetFaultEvents = 0;
var interval = trace.SampleProfileInterval.TotalMilliseconds;
if (interval <= 0) throw new InvalidDataException("Missing sample interval");
using (var writer = new StreamWriter(output)) {
 foreach (var e in trace.Events) {
  var eventKey = e.GetType().Name + ":" + e.EventName;
  eventCounts[eventKey] = eventCounts.GetValueOrDefault(eventKey) + 1;
  if (!pids.Contains(e.ProcessID)) continue;
  if (eventKey.Contains("fault", StringComparison.OrdinalIgnoreCase)) ++targetFaultEvents;
  if (e is not SampledProfileTraceData data) continue;
  var frames = new List<string>();
  bool unresolvedKernel = false;
  for (var stack = trace.GetCallStackForEvent(e); stack != null; stack = stack.Caller) {
   var address = stack.CodeAddress;
   string frame;
   var os = osModules.FirstOrDefault(m => address.Address >= m.Module.ImageBase && address.Address < m.Module.ImageEnd);
   if (os != null) {
    var rva = checked((uint)(address.Address - os.Module.ImageBase));
    var key = (os.Module.Name, rva);
    if (!nameCache.TryGetValue(key, out var name)) {
     uint start = 0;
     name = os.Native.FindNameForRva(rva, ref start);
     nameCache[key] = name;
    }
    frame = string.IsNullOrEmpty(name) ? $"{os.Module.Name}+0x{address.Address:x}" : $"{os.Module.Name}!{name}";
    if (os.Module.Name == "ntoskrnl" && string.IsNullOrEmpty(name)) unresolvedKernel = true;
   } else {
    var name = address.FullMethodName;
    frame = string.IsNullOrEmpty(name) ? $"{address.ModuleName}+0x{address.Address:x}" : $"{address.ModuleName}!{name}";
   }
   frames.Add(frame);
  }
  if (frames.Count == 0) missing += data.Count;
  if (unresolvedKernel) unresolvedKernelSamples += data.Count;
  frames.Reverse(); samples += data.Count;
  writer.WriteLine(JsonSerializer.Serialize(new { block, pid=e.ProcessID, tid=e.ThreadID, time_ms=e.TimeStampRelativeMSec, count=data.Count, weight_ms=interval*data.Count, frames }));
 }
}
File.WriteAllText(output + ".meta.json", JsonSerializer.Serialize(new { block, samples, missing_stack_samples=missing, unresolved_kernel_samples=unresolvedKernelSamples, target_fault_events=targetFaultEvents, sample_interval_ms=interval, events_lost=trace.EventsLost, truncated=trace.Truncated, events=eventCounts,
 os_modules=osModules.Select(m => new { m.Module.Name, m.Module.PdbSignature, m.Module.PdbAge, pdb=m.Spec.GetProperty("pdb").GetString() }) }, new JsonSerializerOptions { WriteIndented=true }));
foreach (var os in osModules) os.Native.Dispose();
Console.WriteLine(JsonSerializer.Serialize(new { block, samples, missing, unresolvedKernelSamples, targetFaultEvents }));

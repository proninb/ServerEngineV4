using Microsoft.Diagnostics.Tracing.Etlx;
using Microsoft.Diagnostics.Symbols;
using System.Globalization;
using System.Text.Json;

if (args.Length != 4) throw new ArgumentException("Reader ETL PID diagnosticRoot output.jsonl");
int pid = int.Parse(args[1]);
string root = Path.GetFullPath(args[2]);
using var conversion = new StreamWriter(args[3] + ".conversion.log");
using var trace = TraceLog.OpenOrConvert(args[0], new TraceLogOptions { ConversionLog = conversion, LocalSymbolsOnly = true });
using var symbolLog = new StreamWriter(args[3] + ".symbols.log");
using var symbols = new SymbolReader(symbolLog, Path.Combine(root,"bin") + ";srv*" + Path.Combine(root,"symbols"), null);
symbols.SecurityCheck = path => Path.GetFullPath(path).StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
using var native = symbols.OpenNativeSymbolFile(Path.Combine(root, "bin", "ServerEngineV4.pdb"));
var apps = trace.ModuleFiles.Where(m => m.Name.Contains("ServerEngineV4UnloadDiag01", StringComparison.OrdinalIgnoreCase)).ToList();
if(apps.Count == 0) throw new InvalidDataException("Missing diagnostic image");
foreach (var module in apps) {
    if(module.PdbSignature != Guid.Parse("b99e77e4-59f3-434a-abf9-39dc6920444c") || module.PdbAge != 1)
        throw new InvalidDataException("App image/PDB mismatch");
    trace.CodeAddresses.LookupSymbolsForModule(symbols, module);
}
foreach(var module in trace.ModuleFiles.Where(m => new[]{"ntdll", "kernel32", "kernelbase", "ucrtbase", "msvcrt"}.Any(n => Path.GetFileName(m.Name).StartsWith(n,StringComparison.OrdinalIgnoreCase)))) {
    try { trace.CodeAddresses.LookupSymbolsForModule(symbols,module); }
    catch(Exception error) { symbolLog.WriteLine("SYMBOL_WARNING " + module.Name + " " + error.Message); }
}
long count = 0, missing = 0;
var names = new Dictionary<string,long>();
using(var output = new StreamWriter(args[3])) {
    foreach(var e in trace.Events) {
        bool target = e.ProcessID == pid;
        var payload = new Dictionary<string,string?>();
        foreach(var name in e.PayloadNames) {
            object? value = e.PayloadByName(name);
            payload[name] = Convert.ToString(value, CultureInfo.InvariantCulture);
            if(name.Equals("ProcessID",StringComparison.OrdinalIgnoreCase) && int.TryParse(payload[name],out var dataPid) && dataPid == pid) target = true;
        }
        if(!target) continue;
        names[e.EventName] = names.GetValueOrDefault(e.EventName) + 1;
        if(!e.EventName.Contains("Virtual",StringComparison.OrdinalIgnoreCase)) continue;
        var frames = new List<string>();
        for(var stack = trace.GetCallStackForEvent(e); stack != null; stack = stack.Caller) {
            var a = stack.CodeAddress;
            frames.Add(string.IsNullOrEmpty(a.FullMethodName) ? $"{a.ModuleName}+0x{a.Address:x}" : $"{a.ModuleName}!{a.FullMethodName}");
        }
        frames.Reverse();
        count++;
        if(frames.Count == 0) missing++;
        output.WriteLine(JsonSerializer.Serialize(new {name=e.EventName,pid=e.ProcessID,tid=e.ThreadID,
            timestamp_utc=e.TimeStamp.ToUniversalTime().ToString("O"),time_ms=e.TimeStampRelativeMSec,payload,frames}));
    }
}
var metadata = new {pid, virtual_events=count, missing_stack_events=missing, events_lost=trace.EventsLost,
    truncated=trace.Truncated, has_stacks=trace.HasCallStacks, target_event_names=names,
    app_modules=apps.Select(m => new{m.Name,m.PdbSignature,m.PdbAge}), library=typeof(TraceLog).Assembly.FullName};
File.WriteAllText(args[3] + ".meta.json",JsonSerializer.Serialize(metadata,new JsonSerializerOptions{WriteIndented=true}));
Console.WriteLine(JsonSerializer.Serialize(metadata));

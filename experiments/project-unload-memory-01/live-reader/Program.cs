using Microsoft.Diagnostics.Tracing.Etlx;
using Microsoft.Diagnostics.Symbols;
using System.Text.Json;
if(args.Length!=2) throw new ArgumentException("diagnosticRoot captureRoot");
string root=Path.GetFullPath(args[0]), run=Path.GetFullPath(args[1]);
using var state=JsonDocument.Parse(File.ReadAllText(Path.Combine(run,"status.json")));
int pid=state.RootElement.GetProperty("pid").GetInt32();
var boundaries=new List<(DateTime Time,string Label)>();
var cycles=new HashSet<int>();
foreach(var snap in state.RootElement.GetProperty("snapshots").EnumerateArray()) {
    var cp=snap.GetProperty("checkpoint");
    boundaries.Add((DateTime.Parse(cp.GetProperty("timestamp_utc").GetString()!).ToUniversalTime(),snap.GetProperty("label").GetString()!));
    cycles.Add(cp.GetProperty("cycle").GetInt32());
}
foreach(var line in File.ReadLines(Path.Combine(run,"boundaries.jsonl"))) {
    using var doc=JsonDocument.Parse(line);var cp=doc.RootElement;
    if(cycles.Contains(cp.GetProperty("cycle").GetInt32())) boundaries.Add((DateTime.Parse(cp.GetProperty("timestamp_utc").GetString()!).ToUniversalTime(),"after_publish_"+cp.GetProperty("cycle").GetInt32()));
}
boundaries.Sort((a,b)=>a.Time.CompareTo(b.Time));
using var log=new StreamWriter(Path.Combine(run,"live-symbols.log"));
using var trace=TraceLog.OpenOrConvert(Path.Combine(run,"merged.etl"));
if(trace.EventsLost!=0 || trace.Truncated) throw new InvalidDataException("Trace quality gate failed");
using var symbols=new SymbolReader(log,Path.Combine(root,"bin")+";srv*"+Path.Combine(root,"symbols"),null);
symbols.SecurityCheck=p=>Path.GetFullPath(p).StartsWith(root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase);
using var pdb=symbols.OpenNativeSymbolFile(Path.Combine(root,"bin/ServerEngineV4.pdb"));
foreach(var module in trace.ModuleFiles.Where(m=>m.Name.Contains("ServerEngineV4UnloadDiag01",StringComparison.OrdinalIgnoreCase))) {
    if(module.PdbSignature!=Guid.Parse("b99e77e4-59f3-434a-abf9-39dc6920444c") || module.PdbAge!=1) throw new InvalidDataException("PDB mismatch");
    trace.CodeAddresses.LookupSymbolsForModule(symbols,module);
}
var live=new Dictionary<(ulong Heap,ulong Address),(ulong Size,TraceCallStack? Stack)>();
long alloc=0,free=0,unmatchedFree=0,overwritten=0,missingAllocStack=0;
int boundary=0;
using var output=new StreamWriter(Path.Combine(run,"live-checkpoints.jsonl"));
void Snapshot(string label,DateTime time) {
    var groups=new Dictionary<string,(ulong Bytes,int Count)>();
    foreach(var block in live.Values) {
        var frames=new List<string>();
        for(var stack=block.Stack;stack!=null;stack=stack.Caller) {
            var a=stack.CodeAddress;
            if(a.ModuleName.Contains("serverenginev4",StringComparison.OrdinalIgnoreCase)) frames.Add(a.FullMethodName.Length==0 ? $"0x{a.Address:x}" : a.FullMethodName);
        }
        string key=frames.Count==0 ? "UNKNOWN/no-app-stack" : string.Join(" <- ",frames);
        var old=groups.GetValueOrDefault(key);groups[key]=(old.Bytes+block.Size,old.Count+1);
    }
    output.WriteLine(JsonSerializer.Serialize(new{label,timestamp_utc=time.ToString("O"),live_blocks=live.Count,
        live_bytes=live.Values.Sum(b=>(long)b.Size),alloc,free,unmatchedFree,overwritten,missingAllocStack,
        groups=groups.OrderByDescending(g=>g.Value.Bytes).Select(g=>new{stack=g.Key,bytes=g.Value.Bytes,count=g.Value.Count})}));
    output.Flush();
}
foreach(var e in trace.Events) {
    while(boundary<boundaries.Count && e.TimeStamp.ToUniversalTime()>boundaries[boundary].Time) {
        Snapshot(boundaries[boundary].Label,boundaries[boundary].Time);boundary++;
    }
    if(e.ProcessID!=pid) continue;
    if(e.EventName=="HeapTrace/Alloc") {
        ulong h=Convert.ToUInt64(e.PayloadByName("HeapHandle")),a=Convert.ToUInt64(e.PayloadByName("AllocAddress")),size=Convert.ToUInt64(e.PayloadByName("AllocSize"));
        var stack=trace.GetCallStackForEvent(e);if(stack==null) missingAllocStack++;
        if(live.ContainsKey((h,a))) overwritten++;
        live[(h,a)]=(size,stack);alloc++;
    } else if(e.EventName=="HeapTrace/Free") {
        ulong h=Convert.ToUInt64(e.PayloadByName("HeapHandle")),a=Convert.ToUInt64(e.PayloadByName("FreeAddress"));
        if(!live.Remove((h,a))) unmatchedFree++;free++;
    } else if(e.EventName.Contains("Realloc",StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Unimplemented realloc schema");
}
File.WriteAllText(Path.Combine(run,"live-meta.json"),JsonSerializer.Serialize(new{pid,alloc,free,unmatchedFree,overwritten,missingAllocStack,boundaries_written=boundary,expected_boundaries=boundaries.Count,events_lost=trace.EventsLost,truncated=trace.Truncated}));

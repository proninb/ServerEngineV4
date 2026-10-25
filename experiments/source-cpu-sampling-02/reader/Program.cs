using Microsoft.Diagnostics.Tracing.Etlx;
using Microsoft.Diagnostics.Tracing.Parsers.Kernel;
using Microsoft.Diagnostics.Symbols;
using System.Text.Json;

if(args.Length!=6)throw new ArgumentException("Reader etl block pids_csv symbols_dir output.jsonl capture_manifest.json");
var pids=args[2].Split(',').Select(int.Parse).ToHashSet();
var block=int.Parse(args[1]);
using var expectedDoc=JsonDocument.Parse(File.ReadAllText(args[5]));
var expected=expectedDoc.RootElement.GetProperty("symbols").GetProperty("exe");
var guid=Guid.Parse(expected.GetProperty("guid").GetString()!);
var age=expected.GetProperty("age").GetInt32();
using var conversion=new StreamWriter(args[4]+".conversion.log");
var options=new TraceLogOptions{ConversionLog=conversion,LocalSymbolsOnly=true};
using var trace=TraceLog.OpenOrConvert(args[0],options);
using var symbolsLog=new StreamWriter(args[4]+".symbols.log");
var symbolDirectory=Path.GetFullPath(args[3]);
using var symbols=new SymbolReader(symbolsLog,symbolDirectory,null);
symbols.SecurityCheck=path=>Path.GetFullPath(path).Equals(Path.Combine(symbolDirectory,"ServerEngineV4PublishBenchmark.pdb"),StringComparison.OrdinalIgnoreCase);
using var native=symbols.OpenNativeSymbolFile(Path.Combine(symbolDirectory,"ServerEngineV4PublishBenchmark.pdb"));
var modules=trace.ModuleFiles.Where(m=>m.Name.Contains("ServerEngineV4PublishBenchmark",StringComparison.OrdinalIgnoreCase)).ToList();
if(modules.Count==0)throw new InvalidDataException("No benchmark image in ETL");
foreach(var module in modules){
 if(module.PdbSignature!=guid||module.PdbAge!=age)throw new InvalidDataException("Captured module/PDB mismatch");
 trace.CodeAddresses.LookupSymbolsForModule(symbols,module);
}
var interval=trace.SampleProfileInterval.TotalMilliseconds;
if(interval<=0)throw new InvalidDataException("No authoritative CPU sample interval");
long samples=0,rows=0,missing=0;
var processes=new Dictionary<int,long>();
using(var output=new StreamWriter(args[4])){
 foreach(var e in trace.Events){
  if(e is not SampledProfileTraceData data||!pids.Contains(e.ProcessID))continue;
  if(data.Count<=0)throw new InvalidDataException("Invalid sample count");
  var frames=new List<string>();
  for(var stack=trace.GetCallStackForEvent(e);stack!=null;stack=stack.Caller){
   var address=stack.CodeAddress;var name=address.FullMethodName;
   var module=address.ModuleName;
   frames.Add(string.IsNullOrEmpty(name)?$"{module}+0x{address.Address:x}":$"{module}!{name}");
  }
  if(frames.Count==0)missing+=data.Count;
  frames.Reverse();samples+=data.Count;rows++;
  processes[e.ProcessID]=processes.GetValueOrDefault(e.ProcessID)+data.Count;
  output.WriteLine(JsonSerializer.Serialize(new{block,pid=e.ProcessID,tid=e.ThreadID,time_ms=e.TimeStampRelativeMSec,
    count=data.Count,weight_ms=interval*data.Count,frames}));
 }
}
var metadata=new{block,events_lost=trace.EventsLost,truncated=trace.Truncated,has_stacks=trace.HasCallStacks,
 sample_interval_ms=interval,samples,rows,missing_stack_samples=missing,processes,
 modules=modules.Select(m=>new{m.Name,m.PdbSignature,m.PdbAge,m.CodeAddressesInModule}),
 library=typeof(TraceLog).Assembly.FullName};
File.WriteAllText(args[4]+".meta.json",JsonSerializer.Serialize(metadata,new JsonSerializerOptions{WriteIndented=true}));
Console.WriteLine(JsonSerializer.Serialize(metadata));

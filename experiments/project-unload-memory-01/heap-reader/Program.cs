using Microsoft.Diagnostics.Tracing.Etlx;
using System.Text.Json;
if(args.Length!=3) throw new ArgumentException("ETL PID output");
int pid=int.Parse(args[1]);
using var log=new StreamWriter(args[2]+".conversion.log");
using var trace=TraceLog.OpenOrConvert(args[0],new TraceLogOptions{ConversionLog=log,LocalSymbolsOnly=true});
var counts=new Dictionary<string,long>();
var samples=new Dictionary<string,object>();
long withStacks=0,heapEvents=0;
foreach(var e in trace.Events) {
    if(e.ProcessID!=pid) continue;
    counts[e.EventName]=counts.GetValueOrDefault(e.EventName)+1;
    if(!e.EventName.Contains("Heap",StringComparison.OrdinalIgnoreCase)) continue;
    heapEvents++;
    var stack=trace.GetCallStackForEvent(e);
    if(stack!=null) withStacks++;
    if(samples.ContainsKey(e.EventName)) continue;
    var payload=new Dictionary<string,string?>();
    foreach(var key in e.PayloadNames) payload[key]=Convert.ToString(e.PayloadByName(key),System.Globalization.CultureInfo.InvariantCulture);
    samples[e.EventName]=new{e.ProviderName,e.EventName,payload,has_stack=stack!=null};
}
var result=new{pid,counts,samples,heapEvents,withStacks,events_lost=trace.EventsLost,
    truncated=trace.Truncated,has_stacks=trace.HasCallStacks,
    app_modules=trace.ModuleFiles.Where(m=>m.Name.Contains("ServerEngineV4UnloadDiag01",StringComparison.OrdinalIgnoreCase)).Select(m=>new{m.Name,m.PdbSignature,m.PdbAge})};
File.WriteAllText(args[2],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));
Console.WriteLine(JsonSerializer.Serialize(result));

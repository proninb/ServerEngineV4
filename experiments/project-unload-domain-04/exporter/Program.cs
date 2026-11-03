using Microsoft.Diagnostics.Tracing.Etlx;
using Microsoft.Diagnostics.Symbols;
using System.Text.Json;
if(args.Length!=3)throw new ArgumentException("diagnosticRoot traceRoot outputRoot");
string root=Path.GetFullPath(args[0]),run=Path.GetFullPath(args[1]),output=Path.GetFullPath(args[2]);
using var status=JsonDocument.Parse(File.ReadAllText(Path.Combine(run,"status.json")));
int pid=status.RootElement.GetProperty("pid").GetInt32();
var boundaries=new List<(DateTime Time,string Stage,int Cycle)>();
foreach(var file in new[]{"boundaries.jsonl","checkpoints.jsonl"})foreach(var line in File.ReadLines(Path.Combine(run,file))){
 using var doc=JsonDocument.Parse(line);var cp=doc.RootElement;
 boundaries.Add((DateTime.Parse(cp.GetProperty("timestamp_utc").GetString()!).ToUniversalTime(),cp.GetProperty("stage").GetString()!,cp.GetProperty("cycle").GetInt32()));
}
boundaries.Sort((a,b)=>a.Time.CompareTo(b.Time));
File.WriteAllText(Path.Combine(output,"boundaries.json"),JsonSerializer.Serialize(boundaries.Select((b,i)=>new{id=i,timestamp_utc=b.Time.ToString("O"),stage=b.Stage,cycle=b.Cycle})));
using var trace=TraceLog.OpenOrConvert(Path.Combine(run,"merged.etl"));
if(trace.EventsLost!=0||trace.Truncated)throw new InvalidDataException("Trace gate");
using var symbolLog=new StreamWriter(Path.Combine(output,"symbols.log"));
using var symbols=new SymbolReader(symbolLog,Path.Combine(root,"bin")+";srv*"+Path.Combine(root,"symbols"),null);
symbols.SecurityCheck=p=>Path.GetFullPath(p).StartsWith(root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase);
using var pdb=symbols.OpenNativeSymbolFile(Path.Combine(root,"bin/ServerEngineV4.pdb"));
foreach(var m in trace.ModuleFiles.Where(m=>m.Name.Contains("ServerEngineV4UnloadDiag01",StringComparison.OrdinalIgnoreCase))){
 if(m.PdbSignature!=Guid.Parse("b99e77e4-59f3-434a-abf9-39dc6920444c")||m.PdbAge!=1)throw new InvalidDataException("PDB identity");
 trace.CodeAddresses.LookupSymbolsForModule(symbols,m);
}
var live=new Dictionary<(ulong,ulong),(ulong Id,byte Owner,ulong Size)>();
var stackCache=new Dictionary<string,int>();var stacks=new List<object>();
var ownerCounts=new ulong[4];var ownerBytes=new ulong[4];
ulong next=0,free=0,unmatched=0;int boundary=0;
using var binary=new BinaryWriter(new FileStream(Path.Combine(output,"events.bin"),FileMode.CreateNew));
using var stackIds=new BinaryWriter(new FileStream(Path.Combine(output,"allocation-stack-ids.bin"),FileMode.CreateNew));
// Fixed little-endian 48-byte records: op/owner/reserved/tid/id/size/heap/address/UTC ticks.
void Write(byte op,byte owner,uint tid,ulong id,ulong size,ulong heap,ulong address,long ticks){binary.Write(op);binary.Write(owner);binary.Write((ushort)0);binary.Write(tid);binary.Write(id);binary.Write(size);binary.Write(heap);binary.Write(address);binary.Write(ticks);}
foreach(var e in trace.Events){
 var time=e.TimeStamp.ToUniversalTime();
 while(boundary<boundaries.Count&&time>boundaries[boundary].Time){
  if(boundaries[boundary].Stage=="after_publish"&&live.Values.Any(v=>v.Owner==1))throw new InvalidDataException("Construction escapes PUBLISH boundary");
  Write(3,0,0,(ulong)boundary,0,0,0,boundaries[boundary].Time.Ticks);boundary++;
 }
 if(e.ProcessID!=pid)continue;
 if(e.EventName=="HeapTrace/Alloc"){
  ulong heap=Convert.ToUInt64(e.PayloadByName("HeapHandle")),addr=Convert.ToUInt64(e.PayloadByName("AllocAddress")),size=Convert.ToUInt64(e.PayloadByName("AllocSize"));
  var frames=new List<string>();for(var s=trace.GetCallStackForEvent(e);s!=null;s=s.Caller)if(s.CodeAddress.ModuleName.Contains("serverenginev4",StringComparison.OrdinalIgnoreCase))frames.Add(s.CodeAddress.FullMethodName);
  string key=string.Join(" <- ",frames);byte owner=0;
  bool duringPublish=boundary<boundaries.Count&&boundaries[boundary].Stage=="after_publish";
  if(key.Contains("create_resident_project")||key.Contains("prepare_runtime_bindings"))owner=2;
  else if(duringPublish&&new[]{"cw::server::graph::","cw::server::string_table::","cw::server::identity_space::","manifest_composer::","cw::server::lexical_generation::","cw::server::file_context::"}.Any(key.Contains))owner=1;
  else if(key.Contains("tcp_endpoint::")||key.Contains("outbound_channel::")||key.Contains("communication::"))owner=3;
  if(!stackCache.TryGetValue(key,out int stackId)){stackId=stacks.Count;stackCache[key]=stackId;stacks.Add(new{id=stackId,owner,stack=key});}
  stackIds.Write(stackId);
  if(live.ContainsKey((heap,addr)))throw new InvalidDataException("Duplicate live address");
  live[(heap,addr)]=(++next,owner,size);ownerCounts[owner]++;ownerBytes[owner]+=size;
  Write(1,owner,(uint)e.ThreadID,next,size,heap,addr,time.Ticks);
 }else if(e.EventName=="HeapTrace/Free"){
  ulong heap=Convert.ToUInt64(e.PayloadByName("HeapHandle")),addr=Convert.ToUInt64(e.PayloadByName("FreeAddress"));
  if(live.Remove((heap,addr),out var block)){Write(2,block.Owner,(uint)e.ThreadID,block.Id,block.Size,heap,addr,time.Ticks);free++;}
  else{Write(4,0,(uint)e.ThreadID,0,0,heap,addr,time.Ticks);unmatched++;}
 }else if(e.EventName.Contains("Realloc",StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Unsupported realloc");
}
File.WriteAllText(Path.Combine(output,"classification-stacks.json"),JsonSerializer.Serialize(stacks));
File.WriteAllText(Path.Combine(output,"normalization.json"),JsonSerializer.Serialize(new{pid,alloc=next,matched_free=free,unmatched_free=unmatched,live_at_end=live.Count,owner_names=new[]{"Unknown","Construction_subset","Resident_origin","Server_origin"},ownerCounts,ownerBytes,boundaries_written=boundary,events_lost=trace.EventsLost,truncated=trace.Truncated,record_bytes=48,classification="Conservative source-reviewed stack families during PUBLISH; zero live subset at every P_after. Other allocations remain Unknown."},new JsonSerializerOptions{WriteIndented=true}));

using Microsoft.Diagnostics.Tracing.Etlx;
using System.Security.Cryptography;
using System.Text.Json;
if(args.Length!=4)throw new ArgumentException("diagnosticRoot traceRoot normalizedRoot outputRoot");
string root=Path.GetFullPath(args[0]),run=Path.GetFullPath(args[1]),normal=Path.GetFullPath(args[2]),output=Path.GetFullPath(args[3]);
using var q=JsonDocument.Parse(File.ReadAllText(Path.Combine(root,"../project-unload-fidelity-05/quality.json")));
if(q.RootElement.GetProperty("pid").GetInt32()!=19288||q.RootElement.GetProperty("process_creation_filetime").GetInt64()!=134376721967437667||q.RootElement.GetProperty("events_lost").GetInt64()!=0||q.RootElement.GetProperty("truncated").GetBoolean())throw new InvalidDataException("Identity gate");
using(var image=File.OpenRead(Path.Combine(root,"bin/ServerEngineV4UnloadDiag01.exe")))if(Convert.ToHexString(SHA256.HashData(image)).ToLowerInvariant()!=q.RootElement.GetProperty("executable_sha256").GetString())throw new InvalidDataException("Image SHA");
using var trace=TraceLog.OpenOrConvert(Path.Combine(run,"merged.etl"));
if(trace.EventsLost!=0||trace.Truncated)throw new InvalidDataException("Trace gate");
var modules=trace.ModuleFiles.Where(m=>m.Name.Contains("ServerEngineV4UnloadDiag01",StringComparison.OrdinalIgnoreCase)).ToList();
if(modules.Count==0||modules.Any(m=>m.PdbSignature!=Guid.Parse("b99e77e4-59f3-434a-abf9-39dc6920444c")||m.PdbAge!=1))throw new InvalidDataException("Module PDB identity");
using var boundaries=JsonDocument.Parse(File.ReadAllText(Path.Combine(normal,"boundaries.json")));
using var binary=new BinaryReader(File.OpenRead(Path.Combine(normal,"events.bin")));
using var writer=new StreamWriter(new FileStream(Path.Combine(output,"timeline.jsonl"),FileMode.CreateNew),System.Text.Encoding.UTF8,1024*1024);
long ordinal=-1,order=0,alloc=0,free=0,threads=0;int boundary=0;long? previousTicks=null;long equalBoundary=0;
void Emit(object row)=>writer.WriteLine(JsonSerializer.Serialize(row));
foreach(var e in trace.Events){ordinal++;long now=e.TimeStamp.ToUniversalTime().Ticks;
 while(boundary<boundaries.RootElement.GetArrayLength()){
  var b=boundaries.RootElement[boundary];long ticks=DateTime.Parse(b.GetProperty("timestamp_utc").GetString()!).ToUniversalTime().Ticks;
  if(now<=ticks){if(now==ticks)equalBoundary++;break;}
  Emit(new{order=order++,etl_ordinal=(long?)null,anchor_before_etl_ordinal=ordinal,op="boundary",ticks,tid=0,heap=0UL,address=0UL,size=0UL,owner=0,id=(ulong)boundary,stage=b.GetProperty("stage").GetString(),cycle=b.GetProperty("cycle").GetInt32()});boundary++;
 }
 if(e.ProcessID!=19288)continue;
 if(e.EventName=="Thread/Start"||e.EventName=="Thread/Stop"){
  Emit(new{order=order++,etl_ordinal=ordinal,op=e.EventName=="Thread/Start"?"start":"stop",ticks=now,tid=e.ThreadID,heap=0UL,address=0UL,size=0UL,owner=0,id=0UL});threads++;continue;
 }
 bool isAlloc=e.EventName=="HeapTrace/Alloc",isFree=e.EventName=="HeapTrace/Free";
 if(!isAlloc&&!isFree){if(e.EventName.Contains("Realloc",StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Realloc unsupported");continue;}
 byte op,owner;uint tid;ulong id,size,heap,address;long time;
 do{op=binary.ReadByte();owner=binary.ReadByte();binary.ReadUInt16();tid=binary.ReadUInt32();id=binary.ReadUInt64();size=binary.ReadUInt64();heap=binary.ReadUInt64();address=binary.ReadUInt64();time=binary.ReadInt64();}while(op==3);
 if(heap!=Convert.ToUInt64(e.PayloadByName("HeapHandle"))||address!=Convert.ToUInt64(e.PayloadByName(isAlloc?"AllocAddress":"FreeAddress"))||tid!=(uint)e.ThreadID||time!=now||isAlloc!=(op==1))throw new InvalidDataException("Normalized event mismatch");
 Emit(new{order=order++,etl_ordinal=ordinal,op=isAlloc?"alloc":"free",ticks=now,tid=e.ThreadID,heap,address,size,owner,id,source=Convert.ToInt32(e.PayloadByName("SourceID")),matched=op!=4});if(isAlloc)alloc++;else free++;
 previousTicks=now;
}
if(binary.BaseStream.Position!=binary.BaseStream.Length||boundary!=boundaries.RootElement.GetArrayLength()||equalBoundary!=0)throw new InvalidDataException("Unconsumed records or ambiguous external boundary timestamp");
File.WriteAllText(Path.Combine(output,"quality.json"),JsonSerializer.Serialize(new{identity=q.RootElement.Clone(),events_lost=trace.EventsLost,truncated=trace.Truncated,alloc,free,threads,boundaries=boundary,rows=order,equal_boundary_events=equalBoundary,common_order="Single iteration of original TraceLog.Events; etl_ordinal counts every source event, not a UTC merge.",external_boundaries="Inserted before first ETL event with strictly later time; no equality encountered."},new JsonSerializerOptions{WriteIndented=true}));

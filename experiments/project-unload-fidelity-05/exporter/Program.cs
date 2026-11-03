using Microsoft.Diagnostics.Tracing.Etlx;
using Microsoft.Diagnostics.Symbols;
using System.Security.Cryptography;
using System.Text.Json;
if(args.Length!=4)throw new ArgumentException("diagnosticRoot traceRoot normalizedRoot outputRoot");
string root=Path.GetFullPath(args[0]),run=Path.GetFullPath(args[1]),normal=Path.GetFullPath(args[2]),output=Path.GetFullPath(args[3]);
using var target=JsonDocument.Parse(File.ReadAllText(Path.Combine(run,"target.json")));
using var status=JsonDocument.Parse(File.ReadAllText(Path.Combine(run,"status.json")));
using var ready=JsonDocument.Parse(File.ReadAllText(Path.Combine(run,"trace-ready.json")));
const int pid=19288;const long creation=134376721967437667;
const string imageHash="dac808b7dd4f5a72e8eec7ce5e6f8172461d5b3cf2612c38fa15f0e53b50af0d";
foreach(var doc in new[]{target,ready})if(doc.RootElement.GetProperty("pid").GetInt32()!=pid||doc.RootElement.GetProperty("creation_filetime").GetInt64()!=creation)throw new InvalidDataException("Captured identity mismatch");
if(status.RootElement.GetProperty("pid").GetInt32()!=pid||status.RootElement.GetProperty("process_creation_filetime").GetInt64()!=creation||target.RootElement.GetProperty("binary_sha256").GetString()!=imageHash)throw new InvalidDataException("Status identity mismatch");
using(var file=File.OpenRead(Path.Combine(root,"bin/ServerEngineV4UnloadDiag01.exe")))if(Convert.ToHexString(SHA256.HashData(file)).ToLowerInvariant()!=imageHash)throw new InvalidDataException("Image hash");
using var trace=TraceLog.OpenOrConvert(Path.Combine(run,"merged.etl"));
if(trace.EventsLost!=0||trace.Truncated)throw new InvalidDataException("Trace quality gate");
using var slog=new StreamWriter(Path.Combine(output,"symbols.log"));
using var symbols=new SymbolReader(slog,Path.Combine(root,"bin"),null);
symbols.SecurityCheck=p=>Path.GetFullPath(p).StartsWith(root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase);
using var pdb=symbols.OpenNativeSymbolFile(Path.Combine(root,"bin/ServerEngineV4.pdb"));
var modules=trace.ModuleFiles.Where(m=>m.Name.Contains("ServerEngineV4UnloadDiag01",StringComparison.OrdinalIgnoreCase)).ToList();
if(modules.Count==0)throw new InvalidDataException("Image not in trace");
foreach(var m in modules){if(m.PdbSignature!=Guid.Parse("b99e77e4-59f3-434a-abf9-39dc6920444c")||m.PdbAge!=1)throw new InvalidDataException("PDB identity");trace.CodeAddresses.LookupSymbolsForModule(symbols,m);}
using var boundaries=JsonDocument.Parse(File.ReadAllText(Path.Combine(normal,"boundaries.json")));
using var binary=new BinaryReader(File.OpenRead(Path.Combine(normal,"events.bin")));
using var writer=new StreamWriter(new FileStream(Path.Combine(output,"events-audit.jsonl"),FileMode.CreateNew),System.Text.Encoding.UTF8,1024*1024);
using var threadWriter=new StreamWriter(new FileStream(Path.Combine(output,"threads.jsonl"),FileMode.CreateNew));
long alloc=0,free=0,threads=0;
void Emit(object row)=>writer.WriteLine(JsonSerializer.Serialize(row));
foreach(var e in trace.Events){
 if(e.ProcessID!=pid)continue;
 if(e.EventName=="Thread/Start"||e.EventName=="Thread/Stop"){threadWriter.WriteLine(JsonSerializer.Serialize(new{op=e.EventName=="Thread/Start"?"thread_start":"thread_stop",ticks=e.TimeStamp.ToUniversalTime().Ticks,tid=e.ThreadID}));threads++;continue;}
 bool isAlloc=e.EventName=="HeapTrace/Alloc",isFree=e.EventName=="HeapTrace/Free";
 if(!isAlloc&&!isFree){if(e.EventName.Contains("Realloc",StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Unsupported realloc");continue;}
 byte op,owner;uint tid;ulong id,size,heap,address;long ticks;
 while(true){op=binary.ReadByte();owner=binary.ReadByte();binary.ReadUInt16();tid=binary.ReadUInt32();id=binary.ReadUInt64();size=binary.ReadUInt64();heap=binary.ReadUInt64();address=binary.ReadUInt64();ticks=binary.ReadInt64();
  if(op!=3)break;
  var b=boundaries.RootElement[(int)id];Emit(new{op="boundary",ticks,stage=b.GetProperty("stage").GetString(),cycle=b.GetProperty("cycle").GetInt32()});
 }
 ulong actualHeap=Convert.ToUInt64(e.PayloadByName("HeapHandle")),actualAddress=Convert.ToUInt64(e.PayloadByName(isAlloc?"AllocAddress":"FreeAddress"));
 if(heap!=actualHeap||address!=actualAddress||tid!=(uint)e.ThreadID||ticks!=e.TimeStamp.ToUniversalTime().Ticks||isAlloc!=(op==1))throw new InvalidDataException("Normalized stream mismatch");
 if(isAlloc&&size!=Convert.ToUInt64(e.PayloadByName("AllocSize")))throw new InvalidDataException("Size mismatch");
 string? source=Convert.ToString(e.PayloadByName("SourceID"),System.Globalization.CultureInfo.InvariantCulture);
 if(source==null||!int.TryParse(source,out int sourceId))throw new InvalidDataException("Missing SourceID");
 Emit(new{op=isAlloc?"alloc":"free",ticks,tid,heap,address,size=isAlloc?(ulong?)size:null,owner=isAlloc?(byte?)owner:null,source=sourceId});
 if(isAlloc)alloc++;else free++;
}
if(binary.BaseStream.Position!=binary.BaseStream.Length)throw new InvalidDataException("Unconsumed normalized records");
File.WriteAllText(Path.Combine(output,"quality.json"),JsonSerializer.Serialize(new{pid,process_creation_filetime=creation,executable_sha256=imageHash,pdb_guid="b99e77e4-59f3-434a-abf9-39dc6920444c",pdb_age=1,events_lost=trace.EventsLost,truncated=trace.Truncated,alloc,free,thread_events=threads,normalized_event_fields_verified=true,identity_provenance="Captured target/status/trace-ready agree; elevated recorder checked creation time before attach. Trace module GUID/age and current unchanged image checked; creation time and full image SHA are not reconstructed from ETL.",owner_provenance="Conservative DOMAIN-04 subset retained exactly; Unknown stays 0"},new JsonSerializerOptions{WriteIndented=true}));

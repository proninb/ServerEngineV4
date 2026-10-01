param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][ValidateRange(1,1000000)][int]$TypeCount,
    [ValidateRange(1,1000)][int]$ObjectsPerType = 10,
    [ValidateRange(1,4096)][int]$FileCount = 128
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) { throw "Output directory must be new: $root" }
# Stream through compiled C# so 100M declarations never accumulate in memory.
if (-not ('ObjectScaleFixture' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
public static class ObjectScaleFixture {
    public static void Write(string root, int types, int objects, int files) {
        Directory.CreateDirectory(Path.Combine(root, "headers"));
        Directory.CreateDirectory(Path.Combine(root, "sources"));
        for (int f = 0; f < files; ++f) {
            using (var h = new StreamWriter(Path.Combine(root, "headers", "types_" + f.ToString("D4") + ".hpp"), false, new UTF8Encoding(false), 1048576))
            using (var s = new StreamWriter(Path.Combine(root, "sources", "objects_" + f.ToString("D4") + ".cpp"), false, new UTF8Encoding(false), 1048576)) {
                long begin = (long)types * f / files, end = (long)types * (f + 1) / files;
                for (long t = begin; t < end; ++t) {
                    string name = "T" + t.ToString("D9");
                    h.Write("struct " + name + " { int value; };\n");
                    for (int o = 0; o < objects; ++o) {
                        s.Write(name); s.Write(" o"); s.Write((t * objects + o).ToString("D9")); s.Write(";\n");
                    }
                }
            }
        }
    }
}
'@
}
$files = [Math]::Min($TypeCount, $FileCount)
[ObjectScaleFixture]::Write($root, $TypeCount, $ObjectsPerType, $files)
$entries = @()
for ($i = 0; $i -lt $files; ++$i) {
    $h = 'types_{0:D4}.hpp' -f $i
    $s = 'objects_{0:D4}.cpp' -f $i
    $entries += [ordered]@{name=$h; type='header'; path="headers/$h"}
    $entries += [ordered]@{name=$s; type='source'; path="sources/$s"}
}
$config = [ordered]@{version=1; name="ObjectScale_$TypeCount"; project=$entries; preprocessor=@{predefines=@()}}
[IO.File]::WriteAllText((Join-Path $root 'project.json'), ($config | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Write-Output "Generated $TypeCount types, $([long]$TypeCount * $ObjectsPerType) objects: $root"

# UnitProXL transfer check

The original SDK and Books remain unchanged. Prepare a workspace copy:

```powershell
python scripts/prepare_unitproxl_headers.py C:/Boris/CW/UnitProXL/Types build/unitproxl-transfer-check/Types --typedef-sdk-byte --empty-simlink
python scripts/prepare_unitproxl_project.py C:/Boris/CW/UnitProXL/Books/UnityProXL/UnityProXL.book build/unitproxl-transfer-check
```

The generated root `project.json` includes `Types/unity_pro_xl.h` and declares
`Types` as a global include directory. Book includes become ordered groups and
Source nodes. Non-include text in books, including the root S and SW arrays, is
preserved in ordered source fragments. Included `.ogd` and `.page` files retain
their exact bytes. `source-transfer.json` records origins and SHA-256 hashes.
The importer rejects cycles, duplicate input files, paths outside the source
tree, and unsupported book directives instead of silently dropping content.

The current dataset produces 3330 Source nodes from 3383 inputs (54 books,
53 `.ogd` files and 3276 `.page` files).

The copied base header declares `struct SimLink {};` and
`typedef unsigned char byte;`. No textual byte expansion is used. Legacy
FOR/GRAPHICS/COLOR/HIDDEN/FUNCTIONS annotations are removed from copied headers.

The full project passes PUBLISH and LOAD/audit with compiled format 20:

- 11814 types (including the byte alias), 411415 fields
- 134474 objects, 85731 links
- compiled.bin: 90041048 bytes

This includes Source string assignments such as:

```cpp
FWM__FWM_EIO_d01_r0_s2_c0_NOMcfg.pPLC_TopAddrStr.VAL = "0.0.3";
```

`U_STRING::VAL` is `char[32]`; the Source parser initializes the characters,
terminator and zero tail. It does not change C++ header assignment syntax.

Start the ordinary server with the generated local configuration (existing
server.license and server.lease must be copied beside it without alteration):

```powershell
build/Release/ServerEngineV4.exe build/unitproxl-transfer-check/server.json
```

The configuration loads the generated project on startup, uses windows-x64,
pack 8, and SHM `CW.ServerEngineV4.UnitProXL` at `0x0000010000000000`.
Checked console values: `GET_STATE` returns `LOADED`;
`FWM__FWM_EIO_d01_r0_s2_c0_NOMcfg.pPLC_TopAddrStr.LEN` is 5;
`pFWM_FWM_HSBY.SectionOrder` is 1; and
`pFWM_FWM_HSBY.__FWM_SYS_MNGR_PLC.BlockId` is float 1.0.
These checks validate transfer and materialization, not execution of legacy
PLC function bodies.

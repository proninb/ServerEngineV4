"""Generate the single ready-cursor candidate; original frontend remains untouched."""
import re,sys
from pathlib import Path
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:])
p=repo/'server_engine/project/frontend/semantic_input.cpp'
s=p.read_text(encoding='utf-8-sig')
s=re.sub(r'#include "([^"]+)"',lambda m:'#include "'+(p.parent/m[1]).resolve().as_posix()+'"',s)
s='#include <cstdlib>\n'+s
needle='''server_status semantic_input::next(
    semantic_token& output) noexcept {
'''
assert s.count(needle)==1
s=s.replace(needle,needle+'''
    // One selected ready-input path; no new name/WHO cache or representation.
    static const bool ready_cursor=[] {
        const auto* p=std::getenv("CW_SOURCE_READY_CURSOR");
        return p && p[0]=='1';
    }();
    if(ready_cursor && mode_value==semantic_input_mode::source &&
        source_predecoded_active && (!telemetry || !telemetry->detailed_source)) {
        output={};
        if(!started || finished_value) return server_status::project_configuration_invalid;
        if(source_predecoded_offset==source_predecoded.size()) {
            finished_value=true;return server_status::success;
        }
        const auto& token=source_predecoded[source_predecoded_offset++];
        output={source_root,token.kind,token.source_offset,token.source_length,{}};
#ifdef CW_SOURCE_PREBIND_PARALLEL_V2
        output.identifier=token.known_identifier;
#endif
        if(telemetry) {
            ++telemetry->source_token_count;
            ++telemetry->source_ready_cursor_token_count;
        }
        if(directive_start(output.kind)) return fail(output.file,
            {output.source_offset,output.source_length},
            "C++ preprocessing directives are not supported in Source inputs",
            server_status::project_configuration_invalid);
        if(output.kind!=token_kind::identifier) return server_status::success;
        if(telemetry) ++telemetry->source_identifier_count;
#ifdef CW_SOURCE_PREBIND_PARALLEL_V2
        if(output.identifier) return server_status::success;
#endif
        return physical_identifier({output.file,output.kind,output.source_offset,output.source_length},
            output.identifier
#ifdef CW_SOURCE_PREHASH_PARALLEL_V2
            ,token.lexical_hash
#endif
            );
    }
''')
(out/'semantic_input.cpp').write_text(s,encoding='utf-8')

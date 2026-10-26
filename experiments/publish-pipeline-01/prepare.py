"""Freeze current packed baseline; patch only the private experimental copy."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
refresh='--refresh' in sys.argv[2:]
owned='--owned' in sys.argv[2:]
assert out.is_relative_to(repo/'build') and (refresh or not out.exists()),out
out.mkdir(parents=True,exist_ok=refresh);root=out/'source'
for name in ('server_engine','benchmarks','tests'):shutil.copytree(repo/name,root/name,dirs_exist_ok=refresh)
helper=root/'experiments/runtime-object-lanes-01';helper.mkdir(parents=True,exist_ok=refresh)
shutil.copy2(repo/'experiments/runtime-object-lanes-01/shm_audit.hpp',helper)
shutil.copy2(Path(__file__).parent/'pipeline01.hpp',root/'server_engine/project/pipeline01.hpp')

def edit(relative,transform):
 p=root/relative;text=p.read_text();updated=transform(text);assert updated!=text,relative;p.write_text(updated)

def replace(text,before,after,count=1):
 assert text.count(before)>=count,(before[:120],text.count(before),count)
 return text.replace(before,after,count)

edit('server_engine/project/shm/shm_runtime_v2.hpp',lambda s:replace(s,'class shm_runtime_v2 final {','class shm_runtime_v2 final {\n    friend struct pipeline01_access;'))

def resident(s):
 s='#include "../pipeline01.hpp"\n'+s
 s=replace(s,'    shm_layout v2_layout;\n    shm_runtime_v2 v2_runtime;','''    shm_layout owned_layout;
    shm_runtime_v2 owned_runtime;
    auto* borrowed = pipeline01::borrowed;
    auto& v2_layout = borrowed ? *borrowed->layout : owned_layout;
    auto& v2_runtime = borrowed ? *borrowed->runtime : owned_runtime;
    if (borrowed) borrowed->used = true;''')
 s=replace(s,'    attach_shm_layout_columns(\n        runtime_project,\n        v2_layout);','    if (!borrowed) attach_shm_layout_columns(\n        runtime_project,\n        v2_layout);')
 s=replace(s,'telemetry->runtime_v2_persisted_layout =\n            true;','telemetry->runtime_v2_persisted_layout =\n            borrowed == nullptr;')
 s=replace(s,'    const auto prepared_runtime =\n        prepare_shm_runtime_v2_persisted(','    const auto prepared_runtime = borrowed ? shm_runtime_v2_result::success :\n        prepare_shm_runtime_v2_persisted(')
 s=replace(s,'    v2_runtime = shm_runtime_v2{};\n    v2_layout = shm_layout{};','''    // Borrowed immutable operation storage is still being serialized by the
    // producer. Only the ordinary LOAD path owns these construction objects.
    if (!borrowed) {
        v2_runtime = shm_runtime_v2{};
        v2_layout = shm_layout{};
    }''')
 return s
edit('server_engine/project/runtime/project_runtime.cpp',resident)

def construction(s):
 s='#include "pipeline01.hpp"\n'+s
 s=replace(s,'    bool durable = true) noexcept {','    bool durable = true, pipeline01::publication* pipeline = nullptr) noexcept {')
 start=s.index('    compiled_project_runtime_type_counts\n        runtime_type_counts;',s.index('persist_compiled('))
 end=s.index('\n}\n\n[[nodiscard]] full_persistence_result\npersist_source_save',start)
 body=s[start:end]
 body=replace(body,'        runtime_type,\n        runtime_type_counts);','        pipeline ? pipeline01_access::types(runtime_plan) : runtime_type,\n        runtime_type_counts);')
 body=replace(body,'            runtime_type,\n            runtime_mapping.bytes())','            pipeline ? pipeline01_access::types(runtime_plan) : runtime_type,\n            runtime_mapping.bytes())')
 body=replace(body,'    if (runtime_mapping.create(','    if (pipeline && pipeline01::fail("before_runtime_create")) return persistence_io_failure(full_persistence_stage::create);\n    if (runtime_mapping.create(')
 body=replace(body,'    if (encode_shm_layout_columns(','    if (pipeline && pipeline01::fail("after_runtime_map")) return persistence_io_failure(full_persistence_stage::encode);\n    if (encode_shm_layout_columns(')
 body=replace(body,'    if (encode_shm_type_batch_physical_columns(','    if (pipeline && pipeline01::fail("after_abi")) return persistence_io_failure(full_persistence_stage::encode);\n    if (encode_shm_type_batch_physical_columns(')
 body=replace(body,'    if (encode_shm_runtime_v2_physical_columns(','    if (pipeline && pipeline01::fail("after_type")) return persistence_io_failure(full_persistence_stage::encode);\n    if (encode_shm_runtime_v2_physical_columns(')
 body=replace(body,'    return persistence_success();','''    if (pipeline) {
        runtime_project_view checked;
        if (checked.bind(runtime_mapping.bytes()) != runtime_project_image_result::success)
            return persistence_invalid(full_persistence_stage::validate);
        if (pipeline01::fail("after_execution")) return persistence_io_failure(full_persistence_stage::encode);
        if (pipeline01::fail("after_shm")) {
            pipeline->finished.wait(false, std::memory_order_acquire);
            return persistence_io_failure(full_persistence_stage::encode);
        }
    }
    return persistence_success();''')
 wrapped='''    if (pipeline && pipeline01::fail("plan")) return persistence_invalid(full_persistence_stage::prepare);
    if (pipeline) {
        pipeline01_access::adopt(runtime_plan, std::move(runtime_type));
        ++pipeline->stats.type_adoptions;
    }
    auto write_image = [&]() noexcept -> full_persistence_result {
'''+body+'''
    };
    if (!pipeline) return write_image();
    const auto window_started = pipeline01::now();
    auto write = [&]() noexcept {
        pipeline->stats.write_begin = pipeline01::now();
        const auto result = write_image();
        pipeline->stats.write_end = pipeline01::now();
        pipeline->stats.write_ns = pipeline->stats.write_end - pipeline->stats.write_begin;
        return result;
    };
    auto consume = [&]() noexcept {
        pipeline->attempted = true;
        pipeline->stats.resident_begin = pipeline01::now();
        if (pipeline01::fail("resident")) {
            pipeline->status = server_status::project_runtime_failed;
            pipeline01::finish_consumer(*pipeline);
            return;
        }
        try {
            read_only_file_mapping resident_mapping;
            if (resident_mapping.open(paths.compiled) != read_only_file_mapping_result::success) {
                pipeline->status = server_status::io_error;
            } else {
                compiled_project_view view;
                if (view.bind(resident_mapping.bytes()) != compiled_project_image_result::success) {
                    pipeline->status = server_status::project_artifact_invalid;
                } else {
                    pipeline01::borrowed_plan handoff{&runtime_layout, &runtime_plan};
                    pipeline01::borrow_scope borrowed{handoff};
                    pipeline->status = create_resident_project(pipeline->path, context.settings,
                        pipeline->operation, pipeline->diagnostics, std::move(resident_mapping), view,
                        read_only_file_mapping{}, runtime_project_view{}, pipeline->staged,
                        telemetry ? &pipeline->stats.resident : nullptr);
                    pipeline->stats.prepared_consumers = handoff.used;
                }
            }
        } catch (...) { pipeline->status = server_status::project_runtime_failed; }
        pipeline01::finish_consumer(*pipeline);
    };
    struct overlap_job {
        decltype(write)& writer;
        decltype(consume)& consumer;
        full_persistence_result result{};
        static void run(void* value, std::size_t lane) noexcept {
            auto& job = *static_cast<overlap_job*>(value);
            if (lane == 0) job.result = job.writer();
            else job.consumer();
        }
    } job{write, consume};
    execution_lanes overlap;
    const bool parallel = pipeline01::mode() == 2 && !std::getenv("PUBLISH_PIPELINE_01_FORCE_SERIAL") && execution_lane_capacity() >= 2 &&
        succeeded(overlap.start(2));
    if (parallel) {
        pipeline->stats.lanes = 2;
        if (!succeeded(overlap.run(2, overlap_job::run, &job))) {
            pipeline->staged.reset();
            return persistence_io_failure(full_persistence_stage::dispatch);
        }
    } else {
        // Test-only failure waits for completed SHM; avoid waiting on ourselves
        // if lane creation is unavailable and this operation falls back.
        if (pipeline01::fail("after_shm")) { consume(); job.result = write(); }
        else { job.result = write(); consume(); }
    }
    pipeline->stats.window_ns = pipeline01::now() - window_started;
    const auto begin = (std::max)(pipeline->stats.write_begin, pipeline->stats.resident_begin);
    const auto end = (std::min)(pipeline->stats.write_end, pipeline->stats.resident_end);
    pipeline->stats.overlap_ns = end > begin ? end - begin : 0;
    pipeline01::last = pipeline->stats;
    if (job.result.status != full_persistence_status::success) pipeline->staged.reset();
    return job.result;
'''
 if not owned:
  # Compact Link/Init sections must be ready before SHM uses the same 24-lane
  # executor as LOAD/baseline. Only ABI/Type columns remain concurrent writes.
  split=body.index('    if (pipeline && pipeline01::fail("after_runtime_map"))')
  image_body=body[:split]
  remaining=body[split:]
  declarations='''    compiled_project_runtime_type_counts runtime_type_counts;
    runtime_project_layout runtime_image_layout;
    writable_file_mapping runtime_mapping;
'''
  image_body=replace(image_body,'    compiled_project_runtime_type_counts\n        runtime_type_counts;','')
  image_body=replace(image_body,'    runtime_project_layout runtime_image_layout;','')
  image_body=replace(image_body,'    writable_file_mapping runtime_mapping;','')
  first=remaining.index('    if (encode_shm_runtime_v2_physical_columns(')
  last=remaining.index('    // PUBLISH owns physical correctness.',first)
  execution=remaining[first:last]
  remaining=remaining[:first]+'    if (!pipeline) {\n'+execution+'    }\n'+remaining[last:]
  remaining=replace(remaining,'    checkpoint(&full_construction_telemetry::compiled_runtime_encode_ns);',
   '    if (!pipeline) checkpoint(&full_construction_telemetry::compiled_runtime_encode_ns);\n'
   '    else if (telemetry) telemetry->compiled_runtime_encode_ns = pipeline->stats.compact_encode_ns;')
  adopt=wrapped[:wrapped.index('    auto write_image =')]
  tail=wrapped[wrapped.index('    if (!pipeline) return write_image();'):]
  tail=replace(tail,'''    execution_lanes overlap;
    const bool parallel = pipeline01::mode() == 2 && !std::getenv("PUBLISH_PIPELINE_01_FORCE_SERIAL") && execution_lane_capacity() >= 2 &&
        succeeded(overlap.start(2));''','''    // Reuse the already joined physical-plan lanes; no extra worker pool.
    const bool parallel = pipeline01::mode() == 2 && !std::getenv("PUBLISH_PIPELINE_01_FORCE_SERIAL") &&
        plan_workers.size() >= 2;''')
  tail=replace(tail,'overlap.run(2, overlap_job::run, &job)','plan_workers.run(2, overlap_job::run, &job)')
  wrapped=adopt+declarations+'    auto prepare_image = [&]() noexcept -> full_persistence_result {\n'+image_body+'''
        return persistence_success();
    };
    auto write_image = [&]() noexcept -> full_persistence_result {
        if (!pipeline) {
            const auto ready = prepare_image();
            if (ready.status != full_persistence_status::success) return ready;
        }
'''+remaining+'''
    };
    if (pipeline) {
        const auto image_started = pipeline01::now();
        const auto ready = prepare_image();
        pipeline->stats.image_setup_ns = pipeline01::now() - image_started;
        if (ready.status != full_persistence_status::success) return ready;
        const auto compact_started = pipeline01::now();
'''+execution+'''
        pipeline->stats.compact_encode_ns = pipeline01::now() - compact_started;
        runtime_project_view in_progress;
        // Directory/prefix and Execution sections are complete. ABI/Type
        // sections are not read here or by the borrowed SHM consumer.
        in_progress.attach(runtime_mapping.bytes());
        if (attach_shm_runtime_v2_physical_columns(in_progress, runtime_plan) != shm_runtime_v2_result::success)
            return persistence_invalid(full_persistence_stage::prepare);
        pipeline01_access::release_encoded_execution(runtime_plan);
        pipeline->stats.compact_setup_ns = pipeline01::now() - compact_started;
        if (pipeline01::fail("compact_setup")) return persistence_invalid(full_persistence_stage::prepare);
        // Plan workers have joined. Every physical operation is detached from
        // G; remaining writers read only layout/Type columns. The consumer
        // opens its own final G mapping, just as ordinary resident publication.
        compiled.reset();
        mapping.reset();
    }
'''+tail
 s=s[:start]+wrapped+s[end:]
 # Carry private staged state through existing artifact dispatch. REBUILD never
 # enables this pointer, and baseline takes the original resident path.
 start=s.index('struct full_persistence_job final {');end=s.index('\n};',start)
 chunk=s[start:end];chunk=replace(chunk,'    bool durable = true;','    bool durable = true;\n    pipeline01::publication* pipeline = nullptr;');s=s[:start]+chunk+s[end:]
 s=replace(s,'                    job.durable);','                    job.durable, job.pipeline);')
 s=replace(s,'                        persistence.durable);','                        persistence.durable, persistence.pipeline);')
 s=replace(s,'    full_persistence_job persistence{','''    pipeline01::publication unpublished{project_path, operation};
    full_persistence_job persistence{''')
 s=replace(s,'    persistence.durable = mode != full_construction_mode::publish;','''    persistence.durable = mode != full_construction_mode::publish;
    if (mode == full_construction_mode::publish && pipeline01::mode() != 0)
        persistence.pipeline = &unpublished;''')
 # After compiled result handling, before REBUILD cache warnings/publication.
 marker='''    if (mode ==
        full_construction_mode::rebuild) {'''
 pos=s.index(marker,s.index('    const auto& compiled_result ='))
 s=s[:pos]+'''    if (persistence.pipeline != nullptr) {
        for (const auto& record : unpublished.diagnostics.records()) diagnostics.emit(record);
        if (!succeeded(unpublished.status) || !unpublished.staged)
            return succeeded(unpublished.status) ? server_status::project_runtime_failed : unpublished.status;
        if (pipeline01::fail("before_publish")) return server_status::project_runtime_failed;
        // The consumer only built private state. Both consumers have joined,
        // encoding and structural validation succeeded; publish ownership now.
        output = std::move(unpublished.staged);
        if (telemetry) {
            telemetry->post_persistence_ns = elapsed_ns(post_persistence_started);
            telemetry->resident_ns = 0; // Already included in the joined window.
            release_started = clock_type::now();
        }
        return server_status::success;
    }

'''+s[pos:]
 s=replace(s,'        resident_runtime,\n        output);','        resident_runtime,\n        output, telemetry ? &pipeline01::last.resident : nullptr);')
 return s
edit('server_engine/project/project_full_construction.cpp',construction)

# Same experiment-only artifact directory shim for every arm. Inputs and their
# persisted source paths stay identical; all artifacts remain arm-local.
edit('server_engine/project/persistence/project_artifact.cpp',lambda s: '#include <cstdlib>\n'+replace(s,
 '        output.manifest = output.root / files.manifest;',
 '        if (const auto* directory = std::getenv("PUBLISH_PIPELINE_01_ARTIFACT_ROOT")) output.root = std::filesystem::path{directory};\n        output.manifest = output.root / files.manifest;'))

# SHA-256 and memory are sampled outside operation timing. Reuse the existing
# helper without enabling any unrelated Runtime lane experiment.
def benchmark(s):
 s='#include "project/pipeline01.hpp"\n'+s
 s=replace(s,'#ifdef CW_RUNTIME_OBJECT_LANES_AB\n#include "../experiments/runtime-object-lanes-01/shm_audit.hpp"','#if defined(CW_RUNTIME_OBJECT_LANES_AB) || defined(CW_PUBLISH_PIPELINE_01)\n#include "../experiments/runtime-object-lanes-01/shm_audit.hpp"')
 s=replace(s,'    std::cout\n        << "mode="','''    std::string pipeline_shm_hash;
    if (const auto* value = std::getenv("PUBLISH_PIPELINE_01_SHM_AUDIT"); value && std::string_view{value} == "1")
        pipeline_shm_hash = runtime_object_lanes01::sha256(project->shm().bytes().first(
            static_cast<std::size_t>(project->runtime_size())));
    PROCESS_MEMORY_COUNTERS memory{};
    GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
    std::cout
        << "mode="''')
 pos=s.rfind("    std::cout << '\\n';");assert pos>=0
 s=s[:pos]+'''    const auto& p = cw::server::pipeline01::last;
    std::cout << ",pipeline_mode=" << cw::server::pipeline01::mode()
        << ",pipeline_image_setup_ms=" << static_cast<double>(p.image_setup_ns)/1e6
        << ",pipeline_compact_setup_ms=" << static_cast<double>(p.compact_setup_ns)/1e6
        << ",pipeline_compact_encode_ms=" << static_cast<double>(p.compact_encode_ns)/1e6
        << ",pipeline_window_ms=" << static_cast<double>(p.window_ns)/1e6
        << ",pipeline_write_ms=" << static_cast<double>(p.write_ns)/1e6
        << ",pipeline_resident_ms=" << static_cast<double>(p.resident_ns)/1e6
        << ",pipeline_overlap_ms=" << static_cast<double>(p.overlap_ns)/1e6
        << ",pipeline_type_adoptions=" << p.type_adoptions
        << ",pipeline_prepared_consumers=" << p.prepared_consumers
        << ",pipeline_consumer_success=" << p.consumer_success
        << ",pipeline_lanes=" << p.lanes
        << ",pipeline_resident_prepare_ms=" << static_cast<double>(p.resident.runtime_v2_prepare_ns)/1e6
        << ",pipeline_shm_create_ms=" << static_cast<double>(p.resident.shm_create_ns)/1e6
        << ",pipeline_canonical_ms=" << static_cast<double>(p.resident.phases.canonical_ns)/1e6
        << ",pipeline_mark_ms=" << static_cast<double>(p.resident.phases.links_mark_ns)/1e6
        << ",pipeline_objects_ms=" << static_cast<double>(p.resident.phases.objects_ns)/1e6
        << ",pipeline_links_ms=" << static_cast<double>(p.resident.phases.links_materialize_ns)/1e6
        << ",pipeline_initializations_ms=" << static_cast<double>(p.resident.phases.initializations_ns)/1e6
        << ",pipeline_object_lanes=" << p.resident.runtime_v2_object_lanes
        << ",pipeline_mark_lanes=" << p.resident.runtime_v2_link_mark_lanes
        << ",peak_private_commit_bytes=" << memory.PeakPagefileUsage;
    if (!pipeline_shm_hash.empty()) std::cout << ",pipeline_shm_sha256=" << pipeline_shm_hash;
'''+s[pos:]
 s=replace(s,'    diagnostics.sort_deterministic();','''    if (std::getenv("PUBLISH_PIPELINE_01_EXPECT_FAILURE")) {
        if (cw::server::succeeded(status) || project) return 4;
        for (const auto* path : {&artifacts.compiled, &artifacts.runtime, &artifacts.manifest,
                                &artifacts.source_save, &artifacts.database})
            if (std::filesystem::exists(*path)) return 4;
        cw::server::fixed_shared_memory probe;
        if (probe.create(settings.shm.name, cw::server::fixed_shared_memory::size_alignment(),
                static_cast<std::uintptr_t>(settings.shm.fixed_base_address)) !=
                cw::server::fixed_shared_memory_result::success) return 4;
        if ((cw::server::pipeline01::fail("after_shm") || cw::server::pipeline01::fail("before_publish")) &&
            !cw::server::pipeline01::last.consumer_success) return 4;
        std::cout << "pipeline_expected_failure=1,unpublished=1,artifacts_removed=1,shm_released=1"
                  << ",consumer_completed=" << cw::server::pipeline01::last.consumer_success << '\\n';
        return 0;
    }
    diagnostics.sort_deterministic();''')
 return s
edit('benchmarks/publish_benchmark.cpp',benchmark)
cmake=(repo/'CMakeLists.txt').read_text()+'''\nif(TARGET ServerEngineV4PublishBenchmark)
 target_compile_definitions(ServerEngineV4PublishBenchmark PRIVATE CW_PUBLISH_PIPELINE_01=1)
 target_compile_options(ServerEngineV4PublishBenchmark PRIVATE /MP4)
endif()
'''
(root/'CMakeLists.txt').write_text(cmake)
manifest={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file()}
(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2))
(out/'snapshot.json').write_text(json.dumps(dict(representation='owned' if owned else 'compact',git_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
 description='Current packed engine, private single-plan serial/overlap experiment. Active engine untouched.'),indent=2))
print(root)

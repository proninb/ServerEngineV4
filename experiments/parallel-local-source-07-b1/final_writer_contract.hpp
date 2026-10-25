#pragma once
#include "../../server_engine/project/graph/graph.hpp"
#include "../../server_engine/project/frontend/source_range.hpp"
#include "../../server_engine/project/file/file_id.hpp"
#include <compare>
#include <concepts>
#include <optional>
#include <span>
#include <tuple>
#include <unordered_set>

namespace pls07b1 {
using namespace cw::server;
inline constexpr std::uint32_t contract_revision=1;
struct domain_id {std::uint64_t lineage=0,snapshot=0;friend bool operator==(domain_id,domain_id)=default;};
struct cell_id {std::uint32_t value=UINT32_MAX;friend auto operator<=>(cell_id,cell_id)=default;};
struct event_key {std::uint32_t source_order=0,local_order=0;friend auto operator<=>(event_key,event_key)=default;};
struct location {file_id file{};source_range range{};};
struct source_task {file_id file{};std::uint32_t source_order=0;};
struct provenance {event_key event;file_id producer_root{};location target{},operand{};};
enum class readiness {projection_only,closed_supported_semantics};
enum class binding_coverage {source_overlay_on_catalog_defaults};
enum class construction_origin {none,header_default,explicit_source};
struct object_info {
    identity_ref who{};type_ref type{};std::uint32_t semantic_flags=0;
    construction_origin cold_origin=construction_origin::none;construction_value cold_value{};
};
struct cell_view {identity_ref who{};type_ref root_type{},value_type{};std::span<const endpoint_path_step> steps;};
struct value_view {cell_id target;construction_value value;provenance source;bool declaration_initializer=false;};
struct binding_view {cell_id target,source;provenance declaration;};
struct declaration_view {identity_ref who{};bool is_extern=false;event_key event;file_id producer_root{};location name{},statement{};};
struct warning_view {cell_id target;bool rebind=false;event_key previous_event,current_event;file_id previous_root{},current_root{};location previous{},current{};};

// Logical reader API, not a persisted struct layout or a new production ABI.
template<class R>concept FinalStateReader=requires(const R& r,std::size_t i) {
    {r.domain()}->std::same_as<domain_id>;{r.completeness()}->std::same_as<readiness>;
    {r.coverage()}->std::same_as<binding_coverage>;
    {r.tasks()}->std::same_as<std::span<const source_task>>;
    {r.cell_count()}->std::same_as<std::size_t>;{r.cell_at(i)}->std::same_as<cell_view>;
    {r.value_count()}->std::same_as<std::size_t>;{r.value_at(i)}->std::same_as<value_view>;
    {r.binding_count()}->std::same_as<std::size_t>;{r.binding_at(i)}->std::same_as<binding_view>;
    {r.declaration_count()}->std::same_as<std::size_t>;{r.declaration_at(i)}->std::same_as<declaration_view>;
    {r.warning_count()}->std::same_as<std::size_t>;{r.warning_at(i)}->std::same_as<warning_view>;
};
template<class C>concept CatalogReader=requires(const C& c,std::size_t i,identity_ref who,type_ref type,file_id file) {
    {c.domain()}->std::same_as<domain_id>;{c.object_count()}->std::same_as<std::size_t>;
    {c.object_at(i)}->std::same_as<object_info>;{c.object(who)}->std::same_as<std::optional<object_info>>;
    {c.contains_type(type)}->std::same_as<bool>;{c.contains_file(file)}->std::same_as<bool>;
};
enum class issue {ok,invalid_domain,domain_mismatch,incomplete_projection,invalid_tasks,missing_object,
    invalid_type,invalid_path,invalid_cell,duplicate_value,duplicate_binding,invalid_provenance,invalid_warning_order};
inline bool valid_domain(domain_id d){return d.lineage&&d.snapshot;}
inline bool valid_tasks(std::span<const source_task> tasks){std::unordered_set<std::uint32_t> seen;
    for(std::size_t i=0;i<tasks.size();++i)if(!tasks[i].file||tasks[i].source_order!=i||!seen.insert(tasks[i].file.value()).second)return false;return true;}

// Optional cold diagnostic checker. Does not parse, resolve names/aliases,
// choose winners, validate layout overlap, or prove materializer support.
template<FinalStateReader R,CatalogReader C>issue inspect(const R& r,const C& catalog,bool require_closed=true){
    if(!valid_domain(r.domain())||!valid_domain(catalog.domain()))return issue::invalid_domain;
    if(r.domain()!=catalog.domain())return issue::domain_mismatch;
    if(require_closed&&r.completeness()!=readiness::closed_supported_semantics)return issue::incomplete_projection;
    const auto tasks=r.tasks();if(!valid_tasks(tasks))return issue::invalid_tasks;
    const auto root=[&](event_key e,file_id f){return e.source_order<tasks.size()&&tasks[e.source_order].file==f;};
    const auto origin=[&](location p){return p.file&&p.range.length&&catalog.contains_file(p.file);};
    const auto prov=[&](provenance p){return root(p.event,p.producer_root)&&origin(p.target)&&origin(p.operand);};
    for(std::size_t i=0;i<catalog.object_count();++i){const auto o=catalog.object_at(i);if(!o.who||o.who.kind()!=identity_kind::object)return issue::missing_object;
        if(!catalog.contains_type(o.type))return issue::invalid_type;}
    for(std::size_t i=0;i<r.cell_count();++i){const auto c=r.cell_at(i);const auto object=catalog.object(c.who);if(!object)return issue::missing_object;
        if(c.root_type!=object->type||!catalog.contains_type(c.root_type)||!catalog.contains_type(c.value_type))return issue::invalid_type;
        for(const auto& s:c.steps)if(s.kind!=endpoint_path_step_kind::member&&s.kind!=endpoint_path_step_kind::base&&s.kind!=endpoint_path_step_kind::array_index)return issue::invalid_path;}
    std::vector<bool> values(r.cell_count()),bindings(r.cell_count());
    for(std::size_t i=0;i<r.value_count();++i){const auto v=r.value_at(i);if(v.target.value>=r.cell_count())return issue::invalid_cell;
        if(values[v.target.value])return issue::duplicate_value;values[v.target.value]=true;if(!prov(v.source))return issue::invalid_provenance;}
    for(std::size_t i=0;i<r.binding_count();++i){const auto b=r.binding_at(i);if(b.target.value>=r.cell_count()||b.source.value>=r.cell_count())return issue::invalid_cell;
        if(bindings[b.target.value])return issue::duplicate_binding;bindings[b.target.value]=true;if(!prov(b.declaration))return issue::invalid_provenance;}
    for(std::size_t i=0;i<r.declaration_count();++i){const auto d=r.declaration_at(i);if(!catalog.object(d.who))return issue::missing_object;
        if(!root(d.event,d.producer_root)||!origin(d.name)||!origin(d.statement))return issue::invalid_provenance;}
    std::optional<std::tuple<event_key,bool,std::uint32_t>> prior;
    for(std::size_t i=0;i<r.warning_count();++i){const auto w=r.warning_at(i);if(w.target.value>=r.cell_count())return issue::invalid_cell;
        if(!root(w.previous_event,w.previous_root)||!root(w.current_event,w.current_root)||!origin(w.previous)||!origin(w.current))return issue::invalid_provenance;
        const auto key=std::tuple{w.current_event,w.rebind,w.target.value};if(w.current_event<w.previous_event||(prior&&key<*prior))return issue::invalid_warning_order;prior=key;}
    return issue::ok;
}

// Replay Input is supplied by Source Manager/planner, never inferred from final
// cells, winner rows, SourceMap entries, or private archived endpoint ordinals.
enum class replay_policy {reparse_all_remaining_sources_v1};
enum class replay_trigger {source_edit,source_removal,project_reorder,binding_change,header_or_configuration_change};
struct replay_revisions {std::uint64_t manifest=0,header=0,preprocessor=0,language=0,acquisition=0;};
struct replay_request {domain_id destination;replay_policy policy=replay_policy::reparse_all_remaining_sources_v1;
    replay_trigger trigger=replay_trigger::source_edit;replay_revisions revisions;std::span<const source_task> remaining_sources;};
inline bool valid_replay(const replay_request& r){return valid_domain(r.destination)&&r.revisions.manifest&&r.revisions.header&&r.revisions.preprocessor&&r.revisions.language&&r.revisions.acquisition&&valid_tasks(r.remaining_sources);}

enum class writer_status {success,invalid_input,unsupported_semantics,unsupported_materialization,
    incompatible_lineage,resource_failure};
struct writer_report {domain_id domain;std::size_t objects=0,values=0,bindings=0,provenance_records=0;};
// The seed owns candidate G/SourceMap and attests matching Header/type/object
// registration state without stale Source overlay. No such backend/token is
// implemented here. These are logical API declarations, not a persisted ABI.
class candidate_seed;
template<FinalStateReader R,CatalogReader C>
writer_status write_final_state(const R&,const C&,candidate_seed&,writer_report&);
// Deliberately no implementation in B1, and no Replay Input/history argument.
}

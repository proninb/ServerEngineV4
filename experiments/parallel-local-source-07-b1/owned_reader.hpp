#pragma once
#include "final_writer_contract.hpp"

namespace pls07b1 {
// Borrowing adapter over PLS07-A public APIs. No new result arena, history
// traversal, endpoint syntax or dependency on bound_model is introduced.
template<class Owned,CatalogReader Catalog>class owned_reader {
    const Owned& owner;const Catalog& catalog;domain_id stamp;std::span<const source_task> source_tasks;readiness ready;
    template<class O>static location at(O o){return {o.file,o.source};}
    template<class E>static event_key event(E e){return {e.source,e.local};}
    file_id root(event_key e)const{return e.source_order<source_tasks.size()?source_tasks[e.source_order].file:file_id{};}
public:
    owned_reader(const Owned& o,const Catalog& c,domain_id d,std::span<const source_task> tasks,readiness r=readiness::projection_only):owner(o),catalog(c),stamp(d),source_tasks(tasks),ready(r){}
    domain_id domain()const{return stamp;}readiness completeness()const{return ready;}
    binding_coverage coverage()const{return binding_coverage::source_overlay_on_catalog_defaults;}
    auto tasks()const->std::span<const source_task>{return source_tasks;}
    std::size_t cell_count()const{return owner.state().cells.size();}
    cell_view cell_at(std::size_t i)const{const auto& c=owner.state().cells.at(i);const auto o=catalog.object(c.who);
        return {c.who,o?o->type:type_ref{},c.type,owner.view().paths().subspan(c.steps.begin,c.steps.count)};}
    std::size_t value_count()const{return owner.state().values.size();}
    value_view value_at(std::size_t i)const{const auto& v=owner.state().values.at(i);const auto w=owner.view().write_at(v.write);const auto e=event(w.event);
        return {{v.cell},w.value,{e,root(e),at(w.at),at(w.value_at)},w.declaration};}
    std::size_t binding_count()const{return owner.state().bindings.size();}
    binding_view binding_at(std::size_t i)const{const auto& b=owner.state().bindings.at(i);const auto p=owner.view().binding_at(b.binding);const auto e=event(p.event);
        return {{b.target},{b.source},{e,root(e),at(p.at),at(p.value_at)}};}
    std::size_t declaration_count()const{return owner.view().declarations().size();}
    declaration_view declaration_at(std::size_t i)const{const auto& d=owner.view().declarations()[i];const auto e=event(d.event);
        return {d.who,d.is_extern,e,root(e),at(d.name),at(d.statement)};}
    std::size_t warning_count()const{return owner.state().warnings.size();}
    warning_view warning_at(std::size_t i)const{const auto& w=owner.state().warnings.at(i);const auto p=event(w.previous_event),c=event(w.current_event);
        return {{w.cell},w.rebind,p,c,root(p),root(c),at(w.previous),at(w.current)};}
};
}

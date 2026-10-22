#pragma once
// Experiment-only token admission. Mirrors the production preprocessor's
// empty-macro/global-boundary eligibility and ordered include/once preflight.
// No identities, G records, binding answers or Source Map contributions.
#include "../../server_engine/project/frontend/preprocessor_v2.hpp"
#include <algorithm>

namespace sb01 {
struct token_reuse {
    using pp=cw::server::semantic_preprocessor_v2;
    using E=pp::reuse_event;
    using file_id=cw::server::file_id;
    using status=cw::server::server_status;
    struct action {E kind;file_id file;std::uint32_t child;};
    struct entry {bool ready=false;std::vector<action> actions;};
    struct capture {file_id file;bool eligible;std::vector<action> actions;};
    pp& input;std::vector<entry> entries;std::vector<capture> stack;
    std::vector<std::uint32_t> once;std::uint32_t generation=0;
    std::size_t reused=0;
    token_reuse(pp& p,std::size_t count):input(p),entries(count+1),once(count+1){}
    bool preflight(file_id file,std::size_t depth){
        if(!file||file.value()>=entries.size()||depth>=cw::server::preprocessor_v2_include_depth_limit||!entries[file.value()].ready)return false;
        for(const auto& a:entries[file.value()].actions){
            if(a.kind==E::once)once[a.file.value()]=generation;
            else if(a.kind==E::include){file_id child{a.child};if(!child||child.value()>=entries.size())return false;
                if(once[child.value()]!=generation&&!input.reuse_once(child)&&!preflight(child,depth+1))return false;}
        }return true;
    }
    status replay(file_id file){
        for(const auto& a:entries[file.value()].actions){status s=status::success;
            if(a.kind==E::once)s=input.reuse_mark_once(a.file);
            else if(a.kind==E::include){file_id child{a.child};s=input.reuse_include(a.file,child);if(cw::server::succeeded(s)&&!input.reuse_once(child))s=replay(child);}
            if(!cw::server::succeeded(s))return s;
        }return status::success;
    }
    status execute(E kind,file_id file,std::uint32_t value,bool eligible,bool& skipped){
        if(kind==E::begin){
            if(eligible&&entries.at(file.value()).ready){
                if(++generation==0){std::fill(once.begin(),once.end(),0);generation=1;}
                if(preflight(file,input.reuse_depth())){const auto s=replay(file);skipped=cw::server::succeeded(s);if(skipped)++reused;return s;}
            }
            stack.push_back({file,eligible,{}});return status::success;
        }
        if(stack.empty())return status::project_configuration_invalid;
        if(kind==E::end){auto done=std::move(stack.back());stack.pop_back();if(done.file!=file)return status::project_configuration_invalid;
            if(eligible&&done.eligible){auto& e=entries.at(file.value());if(!e.ready){e.actions=std::move(done.actions);e.ready=true;}}
            else if(!stack.empty())stack.back().eligible=false;
        }else if(stack.back().eligible&&(kind==E::once||kind==E::include))stack.back().actions.push_back({kind,file,value});
        return status::success;
    }
    static status dispatch(void* state,E kind,file_id file,std::uint32_t value,bool eligible,bool& reused)noexcept{
        try{return static_cast<token_reuse*>(state)->execute(kind,file,value,eligible,reused);}catch(...){return status::io_error;}
    }
};
}

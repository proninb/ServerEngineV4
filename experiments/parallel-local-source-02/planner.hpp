#pragma once
#include "model.hpp"
#include "../../server_engine/project/project_configuration_loader.hpp"
#include "../../server_engine/project/file/file_context.hpp"
#include <unordered_set>
namespace pls02 {
inline std::vector<task> plan_tasks(file_context& files,const std::filesystem::path& root,diagnostic_collection& diagnostics){
    std::vector<task> tasks;std::unordered_set<std::uint32_t> active,seen,source_seen;
    const auto visit=[&](auto&& self,const std::filesystem::path& project,bool first)->void{
        file_id file;if(!succeeded(files.find(project,file))||!file)throw failure("Project file not registered");
        if(active.contains(file.value())||seen.contains(file.value()))throw failure("Duplicate/cyclic Project");
        active.insert(file.value());seen.insert(file.value());
        std::string text(files.content(file));std::vector<project_configuration_dependency> deps;preprocessor_configuration pp;
        if(!succeeded(read_project_configuration(text,project,operation_id{1},diagnostics,deps,
            first?project_configuration_scope::root:project_configuration_scope::nested,first?&pp:nullptr)))throw failure("Task planner configuration read failed");
        for(const auto& d:deps){const auto path=(d.path_type==project_configuration_path_type::absolute?d.path:project.parent_path()/d.path).lexically_normal();
            if(d.kind==file_kind::project)self(self,path,false);
            else if(d.kind==file_kind::source){file_id source;if(!succeeded(files.find(path,source))||!source)throw failure("Source file not registered");
                if(!source_seen.insert(source.value()).second)throw failure("Duplicate Source task");tasks.push_back({source,static_cast<id>(tasks.size())});}
        }
        active.erase(file.value());
    };
    visit(visit,std::filesystem::absolute(root).lexically_normal(),true);return tasks;
}
}

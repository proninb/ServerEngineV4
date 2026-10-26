    struct endpoint_view {identity_ref who;type_ref root,type;std::span<const endpoint_path_step> steps;};
    // The view is consumed synchronously. No path span escapes into G: its
    // existing interning API owns the encoded path before the callback returns.
    template<class F>void with_endpoint(const pls10c7::checked_endpoint& e,const std::vector<endpoint_path_step>& paths,bool final,F&& action){
        const auto steps=std::span<const endpoint_path_step>{paths}.subspan(e.steps.begin,e.steps.count);
        const bool eligible=pls10c7::fast&&e.direct_domain_valid&&
            !(final&&reference(G,e.type))&&std::none_of(steps.begin(),steps.end(),[](auto s){return s.kind==endpoint_path_step_kind::dereference;});
        pls10c7::count(eligible);
        if(eligible){
#ifdef PLS10_C7_AUDIT
            const auto original=resolve(e,paths,final);
            if(original.who!=e.who||original.root!=e.root_type||original.type!=e.type||
                original.steps.size()!=steps.size()||!std::equal(original.steps.begin(),original.steps.end(),steps.begin()))fail("C7 typed view differs from legacy resolution");
            ++pls10c7::verified;
#endif
            action(endpoint_view{e.who,e.root_type,e.type,steps});
        }else{
            const auto original=resolve(e,paths,final);
            action(endpoint_view{original.who,original.root,original.type,original.steps});
        }
    }

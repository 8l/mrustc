/*
 * MRustC - Mutabah's Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * convert/typecheck_expr.cpp
 * - Handles type checking, expansion, and method resolution for expressions (function bodies)
 */
#include <main_bindings.hpp>
#include "ast_iterate.hpp"
#include "../ast/expr.hpp"
#include "../ast/crate.hpp"
#include "../common.hpp"
#include <stdexcept>

// === PROTOTYPES ===
class CTypeChecker:
    public CASTIterator
{
    friend class CTC_NodeVisitor;
    
    struct Scope {
        ::std::vector< ::std::tuple<bool, ::std::string, TypeRef> >   vars;
        ::std::vector< ::std::tuple< ::std::string, TypeRef> >  types;
        ::std::map< ::std::string, TypeRef >    params;
        ::std::vector< AST::Path >  traits;
    };
    
    AST::Crate& m_crate;
    ::std::vector<Scope>    m_scopes;
    
public:
    CTypeChecker(AST::Crate& crate):
        m_crate(crate)
    {}
    
    virtual void start_scope() override;
    virtual void local_variable(bool is_mut, ::std::string name, const TypeRef& type) override;
    virtual void local_type(::std::string name, TypeRef type) override;
    virtual void end_scope() override;
    
    virtual void handle_params(AST::GenericParams& params) override;
    
    virtual void handle_pattern_enum(
            ::std::vector<TypeRef>& pat_args, const ::std::vector<TypeRef>& hint_args,
            const AST::GenericParams& enum_params, const AST::EnumVariant& var,
            ::std::vector<AST::Pattern>& sub_patterns
            ) override;
    
    virtual void handle_function(AST::Path path, AST::Function& fcn) override;
    // - Ignore all non-function items on this pass
    virtual void handle_enum(AST::Path path, AST::Enum& ) override {}
    virtual void handle_struct(AST::Path path, AST::Struct& str) override {}
    virtual void handle_alias(AST::Path path, AST::TypeAlias& ) override {}

private:
    TypeRef& get_local_var(const char* name);
    const TypeRef& get_local_type(const char* name);
    const TypeRef& get_type_param(const char* name);
    
    void check_enum_variant(
        ::std::vector<TypeRef>& path_args, const ::std::vector<TypeRef>& argtypes,
        const AST::GenericParams& params, const AST::EnumVariant& var
        );
    void iterate_traits(::std::function<bool(const TypeRef& trait)> fcn);
};
class CTC_NodeVisitor:
    public AST::NodeVisitorDef
{
    CTypeChecker&   m_tc;
public:
    CTC_NodeVisitor(CTypeChecker& tc):
        m_tc(tc)
    {}
    
    virtual void visit(AST::ExprNode_NamedValue& node) override;
    
    virtual void visit(AST::ExprNode_LetBinding& node) override;
    virtual void visit(AST::ExprNode_Assign& node) override;
    
    virtual void visit(AST::ExprNode_Match& node) override;
    
    virtual void visit(AST::ExprNode_Field& node) override;
    virtual void visit(AST::ExprNode_Cast& node) override;
    
    virtual void visit(AST::ExprNode_CallMethod& node) override;
    virtual void visit(AST::ExprNode_CallPath& node) override;
};

void CTypeChecker::start_scope() 
{
    m_scopes.push_back( Scope() );
}
void CTypeChecker::local_variable(bool is_mut, ::std::string name, const TypeRef& type) 
{
    DEBUG("is_mut=" << is_mut << " name=" << name << " type=" << type);
    m_scopes.back().vars.push_back( make_tuple(is_mut, name, TypeRef(type)) );
}
void CTypeChecker::local_type(::std::string name, TypeRef type)
{
    DEBUG("name=" << name << " type=" << type);
    m_scopes.back().types.push_back( make_tuple(name, ::std::move(type)) );
}
void CTypeChecker::end_scope() 
{
    m_scopes.pop_back();
}

void CTypeChecker::handle_params(AST::GenericParams& params)
{
    ::std::map<std::string,TypeRef>  trs;
    
    for( const auto& param : params.ty_params() )
    {
        trs.insert( make_pair(param.name(), TypeRef()) );
    }
    
    for( const auto& bound : params.bounds() )
    {
        if( bound.is_IsTrait() && bound.as_IsTrait().type.is_type_param() )
        {
            const auto& name = bound.as_IsTrait().type.type_param();
            int i = params.find_name(name.c_str());
            assert(i >= 0);
            // - Just assert that it's valid.
            //trs[name].add_trait( bound.bound() );
        }
    }
    
    assert(m_scopes.back().params.size() == 0);
    m_scopes.back().params = trs;
}
void CTypeChecker::handle_pattern_enum(
        ::std::vector<TypeRef>& pat_args, const ::std::vector<TypeRef>& hint_args,
        const AST::GenericParams& enum_params, const AST::EnumVariant& var,
        ::std::vector<AST::Pattern>& sub_patterns
        )
{
    #if 0
    check_enum_variant(pat_args, hint_args, enum_params, var);
    
    // Ensure that sub_patterns is the same length as the variant
    const auto& var_types = var.m_sub_types;
    if( sub_patterns.size() != var_types.size() )
        throw ::std::runtime_error(FMT("Enum pattern size mismatch"));
    for( unsigned int i = 0; i < sub_patterns.size(); i ++ )
    {
        // TODO: Need to propagate types through here correctly.
        // - hint_args -> enum -> this
        TypeRef arg = var_types[i];
        arg.resolve_args([&](const char *name){
                int i = enum_params.find_name(name);
                if(i < 0)   throw "";
                return hint_args[i];
            });
        handle_pattern(sub_patterns[i], var_types[i]);
    }
    #else
    throw ::std::runtime_error("TODO: CTypeChecker::handle_pattern_enum ");
    #endif
}

TypeRef& CTypeChecker::get_local_var(const char* name)
{
    for( auto it = m_scopes.end(); it-- != m_scopes.begin(); )
    {
        for( auto it2 = it->vars.end(); it2-- != it->vars.begin(); )
        {
            if( name == ::std::get<1>(*it2) )
            {
                return ::std::get<2>(*it2);
            }
        }
    }
    throw ::std::runtime_error(FMT("get_local_var - name " << name << " not found"));
}
const TypeRef& CTypeChecker::get_local_type(const char* name)
{
    for( auto it = m_scopes.end(); it-- != m_scopes.begin(); )
    {
        for( auto it2 = it->types.end(); it2-- != it->types.begin(); )
        {
            if( name == ::std::get<0>(*it2) )
            {
                return ::std::get<1>(*it2);
            }
        }
    }
    throw ::std::runtime_error(FMT("get_local_type - name " << name << " not found"));
}
const TypeRef& CTypeChecker::get_type_param(const char* name)
{
    DEBUG("name = " << name);
    for( auto it = m_scopes.end(); it-- != m_scopes.begin(); )
    {
        DEBUG("- params = " << it->params);
        auto ent = it->params.find(name);
        if( ent != it->params.end() )
        {
            DEBUG("> match " << ent->second);
            return ent->second;
        }
    }
    throw ::std::runtime_error(FMT("get_type_param - name " << name << " not found"));
}

void CTypeChecker::handle_function(AST::Path path, AST::Function& fcn)
{
    DEBUG("(path = " << path << ")");
    start_scope();
    
    handle_params(fcn.params());

    handle_type(fcn.rettype());
    
    for( auto& arg : fcn.args() )
    {
        handle_type(arg.second);
        handle_pattern( arg.first, arg.second );
    }

    CTC_NodeVisitor    nv(*this);
    if( fcn.code().is_valid() )
    {
        fcn.code().node().get_res_type() = fcn.rettype();
        fcn.code().visit_nodes(nv);
    }
    
    end_scope();
}

void CTypeChecker::iterate_traits(::std::function<bool(const TypeRef& trait)> fcn)
{
    for( auto scopei = m_scopes.end(); scopei-- != m_scopes.begin(); )
    {
        for( auto& trait : scopei->traits )
        {
            if( !fcn(TypeRef(Span(), trait)) )
            {
                return;
            }
        }
    }
}

void CTypeChecker::check_enum_variant(::std::vector<TypeRef>& path_args, const ::std::vector<TypeRef>& argtypes, const AST::GenericParams& params, const AST::EnumVariant& var)
{
    #if 0
    // We know the enum, but it might have type params, need to handle that case
    // TODO: Check for more parameters than required
    if( params.ty_params().size() > 0 )
    {
        // 1. Obtain the pattern set from the path (should it be pre-marked with _ types?)
        while( path_args.size() < params.ty_params().size() )
            path_args.push_back( TypeRef() );
        DEBUG("path_args = [" << path_args << "]");
        // 2. Create a pattern from the argument types and the format of the variant
        DEBUG("argtypes = [" << argtypes << "]");
        ::std::vector<TypeRef>  item_args(params.ty_params().size());
        DEBUG("variant type = " << var.m_sub_types << "");
        for( unsigned int i = 0; i < var.m_sub_types.size(); i ++ )
        {
            var.m_sub_types[i].match_args(
                TypeRef(TypeRef::TagTuple(), Span(), argtypes),
                [&](const char *name, const TypeRef& t) {
                        DEBUG("Binding " << name << " to type " << t);
                        int idx = params.find_name(name);
                        if( idx == -1 ) {
                            throw ::std::runtime_error(FMT("Can't find generic " << name));
                        }
                        item_args.at(idx).merge_with( t );
                    }
                );
        }
        DEBUG("item_args = [" << item_args << "]");
        // 3. Merge the two sets of arguments
        for( unsigned int i = 0; i < path_args.size(); i ++ )
        {
            path_args[i].merge_with( item_args[i] );
        }
        DEBUG("new path_args = [" << path_args << "]");
    }
    #else
    throw ::std::runtime_error("TODO: CTypeChecker::check_enum_variant");
    #endif
}

/// Named value - leaf
void CTC_NodeVisitor::visit(AST::ExprNode_NamedValue& node)
{
    DEBUG("ExprNode_NamedValue - " << node.m_path);
    const AST::Path&    p = node.m_path;
    if( p.is_absolute() )
    {
        // grab bound item
        TU_MATCH_DEF(AST::PathBinding, (p.binding()), (info),
        (
            throw ::std::runtime_error( FMT("Unknown binding type on named value : "<<p) );
            ),
        (Static,
            node.get_res_type() = info.static_->type();
            ),
        (EnumVar,
            const AST::Enum& enm = *info.enum_;
            auto idx = info.idx;
            // Enum variant:
            // - Check that this variant takes no arguments
            if( ! enm.variants()[idx].m_data.is_Value() )
                throw ::std::runtime_error( "Used a non-unit variant as a raw value" );
            // - Set output type to the enum (wildcard params, not default)
            AST::Path tp = p;
            tp.nodes().pop_back();
            AST::PathNode& pn = tp.nodes().back();
            unsigned int num_params = enm.params().ty_params().size();
            if(pn.args().size() > num_params)
                throw ::std::runtime_error( FMT("Too many arguments to enum variant - " << p) );
            while(pn.args().size() < num_params)
                pn.args().push_back( TypeRef() );
            node.get_res_type() = TypeRef(Span(node.get_pos()), tp);
            )
        )
    }
    else
    {
        TypeRef& local_type = m_tc.get_local_var( p[0].name().c_str() );
        node.get_res_type().merge_with( local_type );
        DEBUG("res type = " << node.get_res_type());
        local_type = node.get_res_type();
    }
}

void CTC_NodeVisitor::visit(AST::ExprNode_LetBinding& node)
{
    DEBUG("ExprNode_LetBinding");
    
    node.get_res_type() = TypeRef(TypeRef::TagUnit(), Span(node.get_pos()));
    
    // Evaluate value
    AST::NodeVisitor::visit(node.m_value);
    
    const TypeRef&  bind_type = node.m_type;
    const TypeRef&  val_type = node.m_value->get_res_type();
    
    // Obtain resultant type from value
    // Compare to binding type
    // - If both concrete, but different : error
    if( bind_type.is_concrete() && val_type.is_concrete() )
    {
        if( bind_type != val_type ) {
            throw ::std::runtime_error( FMT("Type mismatch on let, expected " << bind_type << ", got " << val_type) );
        }
    }
    // - If neither concrete, merge requirements of both
    else
    {
        node.m_type.merge_with( val_type );
        node.m_value->get_res_type() = node.m_type;
    }
    
    m_tc.handle_pattern(node.m_pat, node.m_type);
}

void CTC_NodeVisitor::visit(AST::ExprNode_Assign& node)
{
    node.get_res_type() = TypeRef(TypeRef::TagUnit(), Span(node.get_pos()));
    AST::NodeVisitor::visit(node.m_slot);
    AST::NodeVisitor::visit(node.m_value);
}

void CTC_NodeVisitor::visit(AST::ExprNode_Match& node)
{
    DEBUG("ExprNode_Match");
    AST::NodeVisitor::visit(node.m_val);
    
    for( auto& arm : node.m_arms )
    {
        m_tc.start_scope();
        for( auto& pat : arm.m_patterns )
            m_tc.handle_pattern(pat, node.m_val->get_res_type());
        AST::NodeVisitor::visit(arm.m_cond);
        AST::NodeVisitor::visit(arm.m_code);
        m_tc.end_scope();
    }
}

void CTC_NodeVisitor::visit(AST::ExprNode_Field& node)
{
    DEBUG("ExprNode_Field " << node.m_name);
    
    AST::NodeVisitor::visit(node.m_obj);
    
    TypeRef* tr = &node.m_obj->get_res_type();
    DEBUG("ExprNode_Field - tr = " << *tr);
    if( tr->is_concrete() )
    {
        // Must be a structure type (what about associated items?)
        unsigned int deref_count = 0;
        while( tr->is_reference() )
        {
            tr = &tr->inner_type();
            DEBUG("ExprNode_Field - ref deref to " << *tr);
            deref_count ++;
        }
        if( !tr->is_path() )
        {
            throw ::std::runtime_error("ExprNode_Field - Type not a path");
        }
        
        // TODO Move this logic to types.cpp?
        const AST::Path& p = tr->path();
        TU_MATCH_DEF( AST::PathBinding, (p.binding()), (info),
        (
            throw ::std::runtime_error("TODO: Get field from non-structure");
            ),
        (Struct,
            const AST::PathNode& lastnode = p.nodes().back();
            AST::Struct& s = const_cast<AST::Struct&>( *info.struct_ );
            node.get_res_type().merge_with( s.get_field_type(node.m_name.c_str(), lastnode.args()) );
            )
        )
        DEBUG("deref_count = " << deref_count);
        for( unsigned i = 0; i < deref_count; i ++ )
        {
            node.m_obj = ::std::unique_ptr<AST::ExprNode>(new AST::ExprNode_Deref( ::std::move(node.m_obj) ));
        }
    }
    else
    {
        DEBUG("ExprNode_Field - Type not concrete, can't get field");
    }
}

void CTC_NodeVisitor::visit(AST::ExprNode_Cast& node)
{
    DEBUG("ExprNode_Cast " << node.m_type);
    
    AST::NodeVisitor::visit(node.m_value);

    node.get_res_type().merge_with( node.m_type );
}

void CTC_NodeVisitor::visit(AST::ExprNode_CallMethod& node)
{
    DEBUG("ExprNode_CallMethod " << node.m_method);
    
    AST::NodeVisitor::visit(node.m_val);
    
    for( auto& arg : node.m_args )
    {
        AST::NodeVisitor::visit(arg);
    }
    
    // Locate method
    const TypeRef& type = node.m_val->get_res_type();
    DEBUG("CallMethod - type = " << type);

    // Replace generic references in 'type' (copying the type) with 
    //   '_: Bounds' (allowing method lookup to succeed)
    TypeRef ltype = type;
    unsigned int deref_count = 0;
    ltype.resolve_args( [&](const char* name) {
            return m_tc.get_type_param(name);
        } );
   
    // Begin trying options (attempting an autoderef each time) 
    const char * const name = node.m_method.name().c_str();
    AST::Function* fcnp = nullptr;
    do
    {
        // 1. Handle bounded wildcard types
        if( ltype.is_wildcard() )
        {
            throw ::std::runtime_error("TODO: _ in CallMethod");
            //if( ltype.traits().size() == 0 ) {
            //    DEBUG("- Unconstrained wildcard, returning");
            //    return ;
            //}
            //
            //for( const auto& t : ltype.traits() )
            //{
            //    DEBUG("- Trait " << t.path());
            //    AST::Trait& trait = const_cast<AST::Trait&>( t.path().binding().bound_trait() );
            //    // - Find method on one of them
            //    for( auto& m : trait.functions() )
            //    {
            //        DEBUG(" > method: " << m.name << " search: " << node.m_method.name());
            //        if( m.name == node.m_method.name() )
            //        {
            //            DEBUG(" > Found method");
            //            fcnp = &m.data;
            //            break;
            //        }
            //    }
            //    if(fcnp)    break;
            //}
            //if(fcnp)    break;
        }
        else
        {
            // 2. Find inherent impl
            auto oimpl = m_tc.m_crate.get_impl(AST::Path(), ltype);
            if( oimpl.is_some() )
            {
                AST::Impl& impl = oimpl.unwrap();
                // 1.1. Search impl for this method
                for(auto& i : impl.items())
                {
                    if( i.name == name && i.data.is_Function() )
                    {
                        fcnp = &i.data.as_Function();
                        break;
                    }
                }
                if(fcnp)    break;
            }
            
            // 2. Iterate in-scope traits
            m_tc.iterate_traits( [&](const TypeRef& trait) {
                // TODO: Check trait first, then find an impl
                auto oimpl = m_tc.m_crate.get_impl(trait.path(), ltype);
                if( oimpl.is_some() )
                {
                    AST::Impl& impl = oimpl.unwrap();
                    for(auto& i : impl.items())
                    {
                        if( i.name == name && i.data.is_Function() )
                        {
                            fcnp = &i.data.as_Function();
                            break;
                        }
                    }
                }
                return fcnp == nullptr;
            });
        }
        if( fcnp )
            break;
        deref_count ++;
    } while( ltype.deref(true) );
    
    if( fcnp )
    {
        DEBUG("deref_count = " << deref_count);
        for( unsigned i = 0; i < deref_count; i ++ )
        {
            node.m_val = ::std::unique_ptr<AST::ExprNode>(new AST::ExprNode_Deref( ::std::move(node.m_val) ));
        }
        
        AST::Function& fcn = *fcnp;
        if( fcn.params().ty_params().size() != node.m_method.args().size() )
        {
            throw ::std::runtime_error("CallMethod with param count mismatch");
        }
        if( fcn.params().ty_params().size() )
        {
            throw ::std::runtime_error("TODO: CallMethod with params");
        }
        node.get_res_type().merge_with( fcn.rettype() );
    }
}
void CTC_NodeVisitor::visit(AST::ExprNode_CallPath& node)
{
    DEBUG("ExprNode_CallPath - " << node.m_path);
    ::std::vector<TypeRef> argtypes;
    for( auto& arg : node.m_args )
    {
        AST::NodeVisitor::visit(arg);
        argtypes.push_back( arg->get_res_type() );
    }
    
    TU_MATCH_DEF( AST::PathBinding, (node.m_path.binding()), (info),
    (
        throw ::std::runtime_error("CallPath on non-function");
        ),
    (Function,
        const AST::Function& fcn = *info.func_;
        
        if( fcn.params().ty_params().size() > 0 )
        {
            throw ::std::runtime_error("CallPath - TODO: Params on functions");
        }
        
        DEBUG("ExprNode_CallPath - rt = " << fcn.rettype());
        node.get_res_type().merge_with( fcn.rettype() );
        ),
    (EnumVar,
        const AST::Enum& enm = *info.enum_;
        const unsigned int idx = info.idx;
    
        auto& path_node_enum = node.m_path[node.m_path.size()-2];
        m_tc.check_enum_variant(path_node_enum.args(), argtypes, enm.params(), enm.variants().at(idx));
    
        AST::Path   p = node.m_path;
        p.nodes().pop_back();
        TypeRef ty(Span(), ::std::move(p) );
        
        DEBUG("ExprNode_CallPath - enum t = " << ty);
        node.get_res_type().merge_with(ty);
        )
    )
}

void Typecheck_Expr(AST::Crate& crate)
{
    DEBUG(" >>>");
    CTypeChecker    tc(crate);
    tc.handle_module(AST::Path("", {}), crate.root_module());
    DEBUG(" <<<");
}


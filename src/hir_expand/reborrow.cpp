/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * hir_expand/reborrow.cpp
 * - Insert reborrows when a &mut would be moved
 */
#include <hir/visitor.hpp>
#include <hir/expr.hpp>
#include <hir_typeck/static.hpp>
#include <algorithm>
#include "main_bindings.hpp"

namespace {
    inline HIR::ExprNodeP mk_exprnodep(HIR::ExprNode* en, ::HIR::TypeRef ty){ en->m_res_type = mv$(ty); return HIR::ExprNodeP(en); }
}
#define NEWNODE(TY, CLASS, ...)  mk_exprnodep(new HIR::ExprNode_##CLASS(__VA_ARGS__), TY)

namespace {
    
    class ExprVisitor_Mutate:
        public ::HIR::ExprVisitorDef
    {
        const ::HIR::Crate& m_crate;
        ::HIR::ExprNodeP    m_replacement;
        
    public:
        ExprVisitor_Mutate(const ::HIR::Crate& crate):
            m_crate(crate)
        {
        }
        void visit_node_ptr(::HIR::ExprPtr& root) {
            const auto& node_ref = *root;
            const char* node_ty = typeid(node_ref).name();
            TRACE_FUNCTION_FR(&*root << " " << node_ty << " : " << root->m_res_type, node_ty);
            root->visit(*this);
            if( m_replacement ) {
                auto usage = root->m_usage;
                const auto* ptr = m_replacement.get();
                DEBUG("=> REPLACE " << ptr << " " << typeid(*ptr).name());
                root.reset( m_replacement.release() );
                root->m_usage = usage;
            }
        }
        
        void visit_node_ptr(::HIR::ExprNodeP& node) override {
            const auto& node_ref = *node;
            const char* node_ty = typeid(node_ref).name();
            TRACE_FUNCTION_FR(&*node << " " << node_ty << " : " << node->m_res_type, node_ty);
            assert( node );
            node->visit(*this);
            if( m_replacement ) {
                auto usage = node->m_usage;
                const auto* ptr = m_replacement.get();
                DEBUG("=> REPLACE " << ptr << " " << typeid(*ptr).name());
                node = mv$(m_replacement);
                node->m_usage = usage;
            }
        }
        
        ::HIR::ExprNodeP do_reborrow(::HIR::ExprNodeP node_ptr)
        {
            TU_IFLET( ::HIR::TypeRef::Data, node_ptr->m_res_type.m_data, Borrow, e,
                if( e.type == ::HIR::BorrowType::Unique )
                {
                    if( dynamic_cast< ::HIR::ExprNode_Index*>(node_ptr.get())
                     || dynamic_cast< ::HIR::ExprNode_Variable*>(node_ptr.get())
                     || dynamic_cast< ::HIR::ExprNode_Field*>(node_ptr.get())
                     || dynamic_cast< ::HIR::ExprNode_Deref*>(node_ptr.get())
                        )
                    {
                        DEBUG("Insert reborrow - " << node_ptr->span() << " - type=" << node_ptr->m_res_type);
                        auto sp = node_ptr->span();
                        auto ty_mut = node_ptr->m_res_type.clone();
                        auto ty = e.inner->clone();
                        node_ptr = NEWNODE(mv$(ty_mut), Borrow, sp, ::HIR::BorrowType::Unique,
                            NEWNODE(mv$(ty), Deref, sp,  mv$(node_ptr))
                            );
                    }
                }
            )
            return node_ptr;
        }
        
        void visit(::HIR::ExprNode_Assign& node) override {
            node.m_value = do_reborrow(mv$(node.m_value));
        }
        void visit(::HIR::ExprNode_CallPath& node) override {
            for(auto& arg : node.m_args)
            {
                arg = do_reborrow(mv$(arg));
            }
        }
        void visit(::HIR::ExprNode_CallValue& node) override {
            for(auto& arg : node.m_args)
            {
                arg = do_reborrow(mv$(arg));
            }
        }
        void visit(::HIR::ExprNode_CallMethod& node) override {
            for(auto& arg : node.m_args)
            {
                arg = do_reborrow(mv$(arg));
            }
        }
    };
    class OuterVisitor:
        public ::HIR::Visitor
    {
        const ::HIR::Crate& m_crate;
    public:
        OuterVisitor(const ::HIR::Crate& crate):
            m_crate(crate)
        {
        }
        
        // NOTE: This is left here to ensure that any expressions that aren't handled by higher code cause a failure
        void visit_expr(::HIR::ExprPtr& exp) override {
            BUG(Span(), "visit_expr hit in OuterVisitor");
        }
        
        void visit_type(::HIR::TypeRef& ty) override
        {
            TU_IFLET(::HIR::TypeRef::Data, ty.m_data, Array, e,
                this->visit_type( *e.inner );
                DEBUG("Array size " << ty);
                if( e.size ) {
                    ExprVisitor_Mutate  ev(m_crate);
                    ev.visit_node_ptr( e.size );
                }
            )
            else {
                ::HIR::Visitor::visit_type(ty);
            }
        }
        // ------
        // Code-containing items
        // ------
        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            //auto _ = this->m_ms.set_item_generics(item.m_params);
            if( item.m_code )
            {
                DEBUG("Function code " << p);
                ExprVisitor_Mutate  ev(m_crate);
                ev.visit_node_ptr( item.m_code );
            }
            else
            {
                DEBUG("Function code " << p << " (none)");
            }
        }
        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            if( item.m_value )
            {
                ExprVisitor_Mutate  ev(m_crate);
                ev.visit_node_ptr(item.m_value);
            }
        }
        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            if( item.m_value )
            {
                ExprVisitor_Mutate  ev(m_crate);
                ev.visit_node_ptr(item.m_value);
            }
        }
        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            for(auto& var : item.m_variants)
            {
                TU_IFLET(::HIR::Enum::Variant, var.second, Value, e,
                    DEBUG("Enum value " << p << " - " << var.first);
                    
                    ExprVisitor_Mutate  ev(m_crate);
                    ev.visit_node_ptr(e);
                )
            }
        }
    };
}   // namespace

void HIR_Expand_Reborrows(::HIR::Crate& crate)
{
    OuterVisitor    ov(crate);
    ov.visit_crate( crate );
}
//===-- Constraints.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Constraints.h"

#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprVisitor.h"
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Function.h"
#else
#include "llvm/Function.h"
#endif
#include "llvm/Support/CommandLine.h"
#include "klee/Internal/Module/KModule.h"

#include <map>

using namespace klee;

namespace {
  llvm::cl::opt<bool>
  RewriteEqualities("rewrite-equalities",
		    llvm::cl::init(true),
		    llvm::cl::desc("Rewrite existing constraints when an equality with a constant is added (default=on)"));
}


class ExprReplaceVisitor : public ExprVisitor {
private:
  ref<Expr> src, dst;

public:
  ExprReplaceVisitor(ref<Expr> _src, ref<Expr> _dst) : src(_src), dst(_dst) {}

  Action visitExpr(const Expr &e) {
    if (e == *src.get()) {
      return Action::changeTo(dst);
    } else {
      return Action::doChildren();
    }
  }

  Action visitExprPost(const Expr &e) {
    if (e == *src.get()) {
      return Action::changeTo(dst);
    } else {
      return Action::doChildren();
    }
  }
};

class ExprReplaceVisitor2 : public ExprVisitor {
private:
  const std::map< ref<Expr>, ref<Expr> > &replacements;

public:
  ExprReplaceVisitor2(const std::map< ref<Expr>, ref<Expr> > &_replacements) 
    : ExprVisitor(true),
      replacements(_replacements) {}

  Action visitExprPost(const Expr &e) {
    std::map< ref<Expr>, ref<Expr> >::const_iterator it =
      replacements.find(ref<Expr>(const_cast<Expr*>(&e)));
    if (it!=replacements.end()) {
      return Action::changeTo(it->second);
    } else {
      return Action::doChildren();
    }
  }
};

bool ConstraintManager::rewriteConstraints(ExprVisitor &visitor) {
  ConstraintManager::constraints_ty old;
  bool changed = false;

  constraints.swap(old);
  for (ConstraintManager::constraints_ty::iterator 
         it = old.begin(), ie = old.end(); it != ie; ++it) {
    ref<Expr> &ce = *it;
    ref<Expr> e = visitor.visit(ce);

    if (e!=ce) {
      addConstraintInternal(e); // enable further reductions
      changed = true;
    } else {
      constraints.push_back(ce);
    }
  }

  return changed;
}

void ConstraintManager::simplifyForValidConstraint(ref<Expr> e) {
  // XXX 
}

ref<Expr> ConstraintManager::simplifyExpr(ref<Expr> e) const {
  if (isa<ConstantExpr>(e))
    return e;

  std::map< ref<Expr>, ref<Expr> > equalities;
  
  for (ConstraintManager::constraints_ty::const_iterator 
         it = constraints.begin(), ie = constraints.end(); it != ie; ++it) {
    if (const EqExpr *ee = dyn_cast<EqExpr>(*it)) {
      if (isa<ConstantExpr>(ee->left)) {
        equalities.insert(std::make_pair(ee->right,
                                         ee->left));
      } else {
        equalities.insert(std::make_pair(*it,
                                         ConstantExpr::alloc(1, Expr::Bool)));
      }
    } else {
      equalities.insert(std::make_pair(*it,
                                       ConstantExpr::alloc(1, Expr::Bool)));
    }
  }

  return ExprReplaceVisitor2(equalities).visit(e);
}

void ConstraintManager::addConstraintInternal(ref<Expr> e) {
  // rewrite any known equalities and split Ands into different conjuncts

  switch (e->getKind()) {
  case Expr::Constant:
    assert(cast<ConstantExpr>(e)->isTrue() && 
           "attempt to add invalid (false) constraint");
    break;
    
    // split to enable finer grained independence and other optimizations
  case Expr::And: {
    BinaryExpr *be = cast<BinaryExpr>(e);
    addConstraintInternal(be->left);
    addConstraintInternal(be->right);
    break;
  }

  case Expr::Eq: {
    if (RewriteEqualities) {
      // XXX: should profile the effects of this and the overhead.
      // traversing the constraints looking for equalities is hardly the
      // slowest thing we do, but it is probably nicer to have a
      // ConstraintSet ADT which efficiently remembers obvious patterns
      // (byte-constant comparison).
      BinaryExpr *be = cast<BinaryExpr>(e);
      if (isa<ConstantExpr>(be->left)) {
	ExprReplaceVisitor visitor(be->right, be->left);
	rewriteConstraints(visitor);
      }
    }
    constraints.push_back(e);
    break;
  }
    
  default:
    constraints.push_back(e);
    break;
  }
}

void ConstraintManager::addConstraint(ref<Expr> e) {
  e = simplifyExpr(e);
  addConstraintInternal(e);

#if defined(CRETE_CONFIG)
  m_crete_cs_deps.add_dep(e);
#endif
}

#if defined(CRETE_CONFIG)
void CreteConstraintDependency::add_dep(ref<Expr> e)
{
    m_last_cs_deps.clear();

    get_expr_cs_deps(e, m_last_cs_deps);
    m_complete_cs_deps.insert(m_last_cs_deps.begin(), m_last_cs_deps.end());
}

void CreteConstraintDependency::get_expr_cs_deps(ref<Expr> e,
        constraint_dependency_ty &deps,
        uint64_t caller_num)
{
    static set< ref<Expr> > scaned_sub_exprs;

    if(caller_num == 0)
        scaned_sub_exprs.clear();

    if (!isa<ConstantExpr>(e) && scaned_sub_exprs.insert(e).second)
    {
        if (const ReadExpr *re = dyn_cast<ReadExpr>(e)) {
            assert(isa<ConstantExpr>(re->index));
            assert(re->updates.head == NULL &&
                    "[CRETE Error] There should be no updatelist for symbolic array, as "
                    "there is no write to symbolic array with symbolic index.\n");
            assert(re->updates.root != NULL &&
                    "[CRETE Error] All ReadExpr should read symbolic array.\n");

            uint64_t index= cast<ConstantExpr>(re->index)->getZExtValue();
            const Array* sym_array = re->updates.root;
            deps.insert(std::make_pair(sym_array, index));
        } else {
            for (unsigned i=0; i<e->getNumKids(); i++)
            {
                get_expr_cs_deps(e->getKid(i), deps, ++caller_num);
            }
        }
    }
}

#include <stdio.h>
void CreteConstraintDependency::print_deps() const
{
    fprintf(stderr, "complete constraint deps size: %lu\n", m_complete_cs_deps.size());
    for(constraint_dependency_ty::const_iterator it = m_complete_cs_deps.begin();
            it != m_complete_cs_deps.end(); ++it) {
        fprintf(stderr, "%s[%lu]\n", it->first->getName().c_str(), it->second);
    }
}

void ConstraintManager::print_constraints() const
{
    fprintf(stderr, "=============================\n");
    unsigned int i = 0;
    fprintf(stderr, "print_constraints():\n");
    for(constraint_iterator it = constraints.begin();
            it != constraints.end(); ++it) {
        fprintf(stderr, "[%u] ", i++);
        (*it)->dump();
    }
    fprintf(stderr, "=============================\n");
}
#endif

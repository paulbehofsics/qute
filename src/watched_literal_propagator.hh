#ifndef watched_literal_propagator_hh
#define watched_literal_propagator_hh

#include <vector>
#include <algorithm>
#include "propagator.hh"
#include "solver_types.hh"
#include "qcdcl.hh"

using std::vector;

namespace Qute {

class WatchedLiteralPropagator: public Propagator {
  friend class DecisionHeuristic;

public:
  WatchedLiteralPropagator(QCDCL_solver& solver, bool model_generation_approx_hs, double exponent, double scaling_factor, double universal_penalty);
  virtual void addVariable();
  virtual CRef propagate(ConstraintType& constraint_type);
  virtual void addConstraint(CRef constraint_reference, ConstraintType constraint_type);
  virtual void notifyAssigned(Literal l);
  virtual void notifyBacktrack(uint32_t decision_level_before);
  virtual void notifyStart();
  virtual void relocConstraintReferences(ConstraintType constraint_type);

protected:
  uint32_t findFirstWatcher(Constraint& constraint, ConstraintType constraint_type);
  uint32_t findSecondWatcher(Constraint& constraint, ConstraintType constraint_type);
  bool isUnassignedOrDisablingPrimary(Literal literal, ConstraintType constraint_type);
  bool isBlockedOrDisablingSecondary(Literal literal, ConstraintType constraint_type, Literal primary);
  bool constraintIsWatchedByLiteral(Constraint& constraint, Literal l);
  bool disablesConstraint(Literal literal, ConstraintType constraint_type);
  bool propagateUnwatched(CRef constraint_reference, ConstraintType constraint_type, bool& watchers_found);
  bool isDisabled(Constraint& constraint, ConstraintType constraint_type);
  bool isUnassignedPrimary(Literal literal, ConstraintType constraint_type);
  bool isBlockedSecondary(Literal literal, ConstraintType constraint_type, Literal primary);
  bool updateWatchedLiterals(Constraint& constraint, CRef constraint_reference, ConstraintType constraint_type, bool& watcher_changed);
  bool propagationCorrect();
  Literal findDisabling(Constraint& constraint, ConstraintType constraint_type, bool variable_type);

  void (WatchedLiteralPropagator::*generateModel)(vector<Literal>& model);
  void generateModelSimple(vector<Literal>& model);
  void generateModelApproxHittingSet(vector<Literal>& model);

  struct WatchedRecord
  {
    CRef constraint_reference;
    Literal blocker;

    WatchedRecord(CRef constraint_reference, Literal blocker): constraint_reference(constraint_reference), blocker(blocker) {}

    WatchedRecord& operator=(const WatchedRecord& other) {
      constraint_reference = other.constraint_reference;
      blocker = other.blocker;
      return *this;
    }
  };

  QCDCL_solver& solver;

  vector<Literal> propagation_queue;
  vector<vector<WatchedRecord>> constraints_watched_by[2];
  vector<CRef> constraints_without_two_watchers[2];
  double exponent;
  double scaling_factor;
  double universal_penalty;
  vector<double> variable_weights;

};

// Implementation of inline methods.
inline void WatchedLiteralPropagator::addVariable() {
  for (ConstraintType constraint_type: constraint_types) {
    // Add entries for both literals.
    constraints_watched_by[constraint_type].emplace_back();
    constraints_watched_by[constraint_type].emplace_back();
  }
}

// inline void WatchedLiteralPropagator::removeConstraint(CRef constraint_reference, ConstraintType constraint_type) {
//   Constraint& constraint = solver.constraint_database->getConstraint(constraint_reference, constraint_type);
//   constraint.mark();
// }

inline void WatchedLiteralPropagator::notifyAssigned(Literal l) {
  propagation_queue.push_back(l);
}

inline void WatchedLiteralPropagator::notifyBacktrack(uint32_t decision_level_before) {
  propagation_queue.clear();
}

}

#endif
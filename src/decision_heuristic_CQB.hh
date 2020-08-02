#ifndef decision_heuristic_CQB_hh
#define decision_heuristic_CQB_hh

#include "qcdcl.hh"
#include "solver_types.hh"
#include "reward_learning.hh"

namespace Qute {

class DecisionHeuristicCQB: public DecisionHeuristic {

public:
  DecisionHeuristicCQB(QCDCL_solver& solver, bool no_phase_saving);
  virtual void addVariable(bool auxiliary);
  virtual void notifyStart();
  virtual void notifyAssigned(Literal l);
  virtual void notifyUnassigned(Literal l);
  virtual void notifyEligible(Variable v);
  virtual void notifyLearned(Constraint& c, ConstraintType constraint_type, vector<Literal>& conflict_side_literals);
  virtual void notifyBacktrack(uint32_t decision_level_before);
  virtual Literal getDecisionLiteral();

protected:
  virtual Variable popFromVariableQueue();
  bool isAuxiliary(Variable v);
  lbool getPhase(Variable v);
  void savePhase(Variable v, lbool phase);

  const bool no_phase_saving;
  uint32_t backtrack_decision_level_before;
  vector<bool> is_auxiliary;
  RewardLearning learning;
};

// Implementation of inline methods

inline void DecisionHeuristicCQB::notifyBacktrack(uint32_t decision_level_before) {
  backtrack_decision_level_before = decision_level_before;
}

inline Variable DecisionHeuristicCQB::popFromVariableQueue() {
  assert(learning.hasBestVariable());
  return learning.popBestVariable();
}

inline bool DecisionHeuristicCQB::isAuxiliary(Variable v) {
  return is_auxiliary[v - 1];
}

inline lbool DecisionHeuristicCQB::getPhase(Variable v) {
  return saved_phase[v - 1];
}

inline void DecisionHeuristicCQB::savePhase(Variable v, lbool phase) {
  saved_phase[v - 1] = phase;
}

}

#endif
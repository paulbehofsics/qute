#include "decision_heuristic_EMAB.hh"

namespace Qute {
  
DecisionHeuristicEMAB::DecisionHeuristicEMAB(QCDCL_solver& solver, bool no_phase_saving):
  DecisionHeuristic(solver), no_phase_saving(no_phase_saving) {}

void DecisionHeuristicEMAB::addVariable(bool auxiliary) {
  saved_phase.push_back(l_Undef);
  is_auxiliary.push_back(auxiliary);
  learning.addVariable();
}

void DecisionHeuristicEMAB::notifyStart() {
  for (Variable v = 1; v <= solver.variable_data_store->lastVariable(); v++) {
    if (!isAuxiliary(v) && solver.dependency_manager->isDecisionCandidate(v)) {
      learning.addCandidateVariable(v);
    }
  }
}

void DecisionHeuristicEMAB::notifyAssigned(Literal l) {
  Variable v = var(l);
  savePhase(v, sign(l));
  if (!isAuxiliary(v)) {
    learning.assign(v);
  }
}

void DecisionHeuristicEMAB::notifyUnassigned(Literal l) {
  Variable v = var(l);
  if (!isAuxiliary(v)) {
    Variable watcher = solver.dependency_manager->watcher(v);
    // If variable will be unassigned after backtracking but its watcher still assigned,
    // variable is eligible for assignment after backtracking.
    bool unwatched = watcher == 0 || (
      solver.variable_data_store->isAssigned(watcher) &&
      solver.variable_data_store->varDecisionLevel(watcher) < backtrack_decision_level_before
    );
    if (unwatched) {
      learning.addCandidateVariableIfMissing(v);
    }

    learning.unassign(var(l));
  }
}

void DecisionHeuristicEMAB::notifyEligible(Variable v) {
  if (!isAuxiliary(v)) {
    learning.updateCandidateVariable(v);
  }
}

void DecisionHeuristicEMAB::notifyLearned(Constraint& c, ConstraintType constraint_type, vector<Literal>& conflict_side_literals) {
  for (Literal l: c) {
    Variable v = var(l);
    learning.setReward(v, 1);
  }
  learning.finalizeRewardCycle();
}

Literal DecisionHeuristicEMAB::getDecisionLiteral() {
  Variable candidate = 0;
  while (learning.hasBestVariable() && !solver.dependency_manager->isDecisionCandidate(learning.peekBestVariable())) {
    popFromVariableQueue();
  }
  candidate = popFromVariableQueue();
  assert(candidate != 0);
  assert(!isAuxiliary(candidate));
  assert(solver.dependency_manager->isDecisionCandidate(candidate));
  if (no_phase_saving || getPhase(candidate) == l_Undef) {
    savePhase(candidate, phaseHeuristic(candidate));
  }
  return mkLiteral(candidate, getPhase(candidate));
}

}
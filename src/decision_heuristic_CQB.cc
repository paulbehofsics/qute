#include "decision_heuristic_CQB.hh"

namespace Qute {
  
DecisionHeuristicCQB::DecisionHeuristicCQB(QCDCL_solver& solver, bool no_phase_saving):
  DecisionHeuristic(solver), no_phase_saving(no_phase_saving), variable_queue(CompareVariables(variable_quality)) {}

void DecisionHeuristicCQB::addVariable(bool auxiliary) {
  saved_phase.push_back(l_Undef);
  is_auxiliary.push_back(auxiliary);
  assigned_vars.addVariable();
}

void DecisionHeuristicCQB::notifyStart() {
  for (Variable v = 1; v <= solver.variable_data_store->lastVariable(); v++) {
    if (!isAuxiliary(v) && solver.dependency_manager->isDecisionCandidate(v)) {
      variable_queue.insert(v);
    }
  }
}

void DecisionHeuristicCQB::notifyAssigned(Literal l) {
  Variable v = var(l);
  savePhase(v, sign(l));
  if (!isAuxiliary(v)) {
    assigned_vars.insert(v);
  }
}

void DecisionHeuristicCQB::notifyUnassigned(Literal l) {
  Variable v = var(l);
  if (!isAuxiliary(v)) {
    Variable watcher = solver.dependency_manager->watcher(v);
    // If variable will be unassigned after backtracking but its watcher still assigned,
    // variable is eligible for assignment after backtracking.
    bool unwatched = watcher == 0 || (
      solver.variable_data_store->isAssigned(watcher) &&
      solver.variable_data_store->varDecisionLevel(watcher) < backtrack_decision_level_before
    );
    if (unwatched && !variable_queue.inHeap(v)) {
      variable_queue.insert(v);
    }

    assigned_vars.remove(var(l));
  }
}

void DecisionHeuristicCQB::notifyEligible(Variable v) {
  if (!isAuxiliary(v)) {
    variable_queue.update(v);
  }
}

void DecisionHeuristicCQB::notifyLearned(Constraint& c, ConstraintType constraint_type, vector<Literal>& conflict_side_literals) {
  size_t constr_size = 1; // TODO: get constraint size or LBD
  for (auto iter = assigned_vars.begin(); iter != assigned_vars.end(); ++iter) {
    // TODO: modify variable_quality of variable *iter according to constraint size or LBD
  }
}

Literal DecisionHeuristicCQB::getDecisionLiteral() {
  Variable candidate = 0;
  while (!variable_queue.empty() && !solver.dependency_manager->isDecisionCandidate(variable_queue[0])) {
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
#include "decision_heuristic_VMTF_order.hh"

namespace Qute {

DecisionHeuristicVMTForder::DecisionHeuristicVMTForder(QCDCL_solver& solver, bool no_phase_saving): DecisionHeuristicVMTFdeplearn(solver, no_phase_saving) {}

void DecisionHeuristicVMTForder::notifyLearned(Constraint& c, ConstraintType constraint_type, vector<Literal>& conflict_side_literals) {
  // Move to front all assigned variables in the learned constraint sorted by their id (position in the prefix).
  priority_queue<Variable> vars_to_move;
  for (Literal l: c) {
    Variable v = var(l);
    if (solver.variable_data_store->isAssigned(v)) {
      vars_to_move.push(v);
    }
  }
  while (!vars_to_move.empty()) {
    Variable v = vars_to_move.top();
    vars_to_move.pop();
    moveToFront(v);
  }
}

}
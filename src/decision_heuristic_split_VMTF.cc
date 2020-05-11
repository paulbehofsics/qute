#include "decision_heuristic_split_VMTF.hh"

namespace Qute {

DecisionHeuristicSplitVMTF::DecisionHeuristicSplitVMTF(QCDCL_solver& solver, bool no_phase_saving, uint32_t mode_cycles): 
  DecisionHeuristic(solver), mode_cycles(mode_cycles), cycle_counter(0), current_mode(UnivMode), mode(&univ_mode),
  exist_mode(), univ_mode(), timestamp(0), no_phase_saving(no_phase_saving) {}

void DecisionHeuristicSplitVMTF::addVariable(bool auxiliary) {
  saved_phase.push_back(l_Undef);
  is_auxiliary.push_back(auxiliary);
  addVariable(auxiliary, exist_mode);
  addVariable(auxiliary, univ_mode);
}

void DecisionHeuristicSplitVMTF::notifyUnassigned(Literal l) {
  Variable variable = var(l);
  if (!is_auxiliary[variable - 1]) {
    Variable watcher = solver.dependency_manager->watcher(variable);
    /* If variable will be unassigned after backtracking but its watcher still assigned,
      variable is eligible for assignment after backtracking. If its timestamp is better
      than that of next_search, we must update next_search. */
    if ((watcher == 0 || (solver.variable_data_store->isAssigned(watcher) &&
        (solver.variable_data_store->varDecisionLevel(watcher) < backtrack_decision_level_before))) &&
        (mode->decision_list[variable - 1].timestamp > mode->decision_list[mode->next_search - 1].timestamp)) {
      mode->next_search = variable;
    }
  }
}

void DecisionHeuristicSplitVMTF::notifyLearned(Constraint& c, ConstraintType constraint_type, vector<Literal>& conflict_side_literals) {
  // Bump every assigned variable in the learned constraint, if the constraint fits the current mode.
  if (isConstraintTypeOfMode(constraint_type)) {
    for (Literal l: c) {
      Variable v = var(l);
      if (solver.variable_data_store->isAssigned(v)) {
        moveToFront(v);
      }
    }
  }
}

void DecisionHeuristicSplitVMTF::notifyBacktrack(uint32_t decision_level_before) {
  backtrack_decision_level_before = decision_level_before;
  clearOverflowQueue();
}

void DecisionHeuristicSplitVMTF::notifyRestart() {
  cycle_counter++;
  if (cycle_counter >= mode_cycles) {
    toggleMode();
    cycle_counter = 0;
  }
}

Literal DecisionHeuristicSplitVMTF::getDecisionLiteral() {
  Variable candidate = 0;
  // First, check the overflow queue for an unassigned variable.
  while (!mode->overflow_queue.empty() && solver.variable_data_store->isAssigned(mode->overflow_queue.top())) {
    mode->overflow_queue.pop();
  }
  if (!mode->overflow_queue.empty()) {
    candidate = mode->overflow_queue.top();
    mode->overflow_queue.pop();
  } else {
    // If no suitable variable was found in the overflow queue, search in the linked list, starting from next_search.
    while (!solver.dependency_manager->isDecisionCandidate(mode->next_search) &&
        mode->decision_list[mode->next_search - 1].next != mode->list_head) {
      mode->next_search = mode->decision_list[mode->next_search - 1].next;
    }
    candidate = mode->next_search;
  }
  assert(candidate != 0);
  assert(!is_auxiliary[candidate - 1]);
  assert(solver.dependency_manager->isDecisionCandidate(candidate));
  assert(mode->decision_list[candidate - 1].timestamp == maxTimestampEligible());
  if (no_phase_saving || saved_phase[candidate - 1] == l_Undef) {
    saved_phase[candidate - 1] = phaseHeuristic(candidate);
  }
  return mkLiteral(candidate, saved_phase[candidate - 1]);
}

void DecisionHeuristicSplitVMTF::resetTimestamps() {
  /* Assign timestamps in ascending order, starting from the back of the
     list. */
  timestamp = 0;
  Variable list_ptr = mode->list_head;
  do {
    list_ptr = mode->decision_list[list_ptr - 1].prev;
    mode->decision_list[list_ptr - 1].timestamp = timestamp++;
  } while (list_ptr != mode->list_head);
}

void DecisionHeuristicSplitVMTF::moveToFront(Variable variable) {
  Variable current_head = mode->list_head;

  /* If the variable is already at the head of the list or an auxiliary variable,
     don't do anything. */
  if (current_head == variable || is_auxiliary[variable - 1]) {
    return;
  }

  /* Increase timestamp value */
  if (timestamp == ((uint32_t)-1)) {
    resetTimestamps();
  }
  mode->decision_list[variable - 1].timestamp = ++timestamp;

  /* Detach variable from list */
  Variable current_prev = mode->decision_list[variable - 1].prev;
  Variable current_next = mode->decision_list[variable - 1].next;
  mode->decision_list[current_prev - 1].next = current_next;
  mode->decision_list[current_next - 1].prev = current_prev;

  /* Insert variable as list head */
  Variable current_head_prev = mode->decision_list[current_head - 1].prev;
  mode->decision_list[current_head - 1].prev = variable;
  mode->decision_list[variable - 1].next = current_head;
  mode->decision_list[variable - 1].prev = current_head_prev;
  mode->decision_list[current_head_prev - 1].next = variable;
  mode->list_head = variable;
  assert(checkOrder());
}

bool DecisionHeuristicSplitVMTF::checkOrder() {
  Variable list_ptr = mode->list_head;
  Variable next = mode->decision_list[list_ptr - 1].next;
  while (mode->decision_list[list_ptr - 1].timestamp > mode->decision_list[next - 1].timestamp) {
    list_ptr = mode->decision_list[list_ptr - 1].next;
    next = mode->decision_list[list_ptr - 1].next;
  }
  return next == mode->list_head;
}

void DecisionHeuristicSplitVMTF::toggleMode() {
  if (current_mode == ExistMode) {
    current_mode = UnivMode;
    mode = &univ_mode;
  } else if (current_mode == UnivMode) {
    current_mode = ExistMode;
    mode = &exist_mode;
  }
  resetTimestamps();
  mode->next_search = mode->list_head;
}

void DecisionHeuristicSplitVMTF::addVariable(bool auxiliary, DecisionModeData& mode) {
  if (mode.decision_list.empty()) {
    mode.decision_list.emplace_back();
    mode.list_head = mode.next_search = 1;
  } else {
    /* Add new variable at the end of the list if it's not an auxiliary variable.
     * Each auxiliary is contained in a singleton list. */
    Variable old_last = mode.decision_list[mode.list_head - 1].prev;
    ListEntry new_entry;
    if (!auxiliary) {
      new_entry.next = mode.list_head;
      new_entry.timestamp = 0;
      new_entry.prev = old_last;
      mode.decision_list[mode.list_head - 1].prev = mode.decision_list.size() + 1;
      mode.decision_list[old_last - 1].next = mode.decision_list.size() + 1;
    } else {
      new_entry.next = mode.decision_list.size() + 1;
      new_entry.prev = mode.decision_list.size() + 1;
      new_entry.timestamp = 0;
    }
    mode.decision_list.push_back(new_entry);
  }
}

}
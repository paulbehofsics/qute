#include "decision_heuristic_split_VMTF.hh"

namespace Qute {

DecisionHeuristicSplitVMTF::DecisionHeuristicSplitVMTF(QCDCL_solver& solver, bool no_phase_saving,
    uint32_t mode_cycles, bool always_move, bool move_by_prefix, bool split_phase_saving, bool start_univ_mode):
      DecisionHeuristic(solver), always_move(always_move), move_by_prefix(move_by_prefix),
      mode_cycles(mode_cycles), cycle_counter(0), exist_mode(), univ_mode(), timestamp(0),
      no_phase_saving(no_phase_saving) {

  if (start_univ_mode) {
    mode_type = UnivMode;
    mode = &univ_mode;
  } else {
    mode_type = ExistMode;
    mode = &exist_mode;
  }

  if (split_phase_saving) {
    phase_saving = SplitPhaseSaving(mode_type);
  } else {
    phase_saving = PhaseSaving();
  }
}

void DecisionHeuristicSplitVMTF::notifyStart(DecisionModeData& mode) {
  Variable list_ptr = mode.list_head;
  if (mode.list_head) {
    do {
      list_ptr = mode.decision_list[list_ptr - 1].prev;
      mode.decision_list[list_ptr - 1].timestamp = timestamp++;
    } while (list_ptr != mode.list_head);
  }
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
  if (always_move) {
    if (constraint_type == terms) {
      moveVariables(c, exist_mode);
    } else if (constraint_type == clauses) {
      moveVariables(c, univ_mode);
    }
  }
  else if (isConstraintTypeOfMode(constraint_type)) {
    moveVariables(c, *mode);
  } else {
    moveVariablesBack(c, *mode);
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
  if (no_phase_saving || !phase_saving.hasPhase(candidate)) {
    phase_saving.savePhase(candidate, phaseHeuristic(candidate));
  }
  return mkLiteral(candidate, phase_saving.getPhase(candidate));
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

void DecisionHeuristicSplitVMTF::moveVariables(Constraint& c, DecisionModeData& mode) {
  if (move_by_prefix) {
    moveVariablesByPrefix(c, mode);
  } else {
    moveVariablesArbitrary(c, mode);
  }
}

void DecisionHeuristicSplitVMTF::moveVariablesByPrefix(Constraint& c, DecisionModeData& mode) {
  // Move to front all assigned variables in the learned constraint sorted by their id (position in the prefix).
  // The variable with the lowest id (lowest quantifier depth) will end up on the first position of the list.
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
    moveToFront(v, mode);
  }
}

void DecisionHeuristicSplitVMTF::moveVariablesArbitrary(Constraint& c, DecisionModeData& mode) {
  // Move to front all assigned variables in the learned constraint in arbitrary order
  for (Literal l: c) {
    Variable v = var(l);
    if (solver.variable_data_store->isAssigned(v)) {
      moveToFront(v, mode);
    }
  }
}

void DecisionHeuristicSplitVMTF::moveVariablesBack(Constraint& c, DecisionModeData& mode) {
  // Move to back all assigned variables in the learned constraint in arbitrary order
  for (Literal l: c) {
    Variable v = var(l);
    if (solver.variable_data_store->isAssigned(v)) {
      moveToBack(v, mode);
    }
  }
}

void DecisionHeuristicSplitVMTF::moveToFront(Variable variable, DecisionModeData& mode) {
  Variable current_head = mode.list_head;

  /* If the variable is already at the head of the list or an auxiliary variable,
     don't do anything. */
  if (current_head == variable || is_auxiliary[variable - 1]) {
    return;
  }

  /* Increase timestamp value */
  if (timestamp == (std::numeric_limits<int>::max() - 1)) {
    resetTimestamps();
  }
  mode.decision_list[variable - 1].timestamp = ++timestamp;
  
  /* Detach variable from list */
  Variable current_prev = mode.decision_list[variable - 1].prev;
  Variable current_next = mode.decision_list[variable - 1].next;
  mode.decision_list[current_prev - 1].next = current_next;
  mode.decision_list[current_next - 1].prev = current_prev;

  /* Insert variable as list head */
  Variable current_head_prev = mode.decision_list[current_head - 1].prev;
  mode.decision_list[current_head - 1].prev = variable;
  mode.decision_list[variable - 1].next = current_head;
  mode.decision_list[variable - 1].prev = current_head_prev;
  mode.decision_list[current_head_prev - 1].next = variable;
  mode.list_head = variable;
  assert(checkOrder());
}

void DecisionHeuristicSplitVMTF::moveToBack(Variable variable, DecisionModeData& mode) {
  Variable current_head = mode.list_head;

  /* If the variable is the only variable in the list or an auxiliary variable,
     don't do anything. */
  if ((current_head == variable && mode.decision_list[variable - 1].next == 0) || 
      is_auxiliary[variable - 1]) {
    return;
  }

  /* Increase timestamp value */
  if (timestamp == (std::numeric_limits<int>::max() - 1)) {
    resetTimestamps();
  }
  mode.decision_list[variable - 1].timestamp = -(++timestamp);

  /* Adjust next search */
  if (mode.next_search == variable) {
    mode.next_search = mode.decision_list[variable - 1].next;
  }
  /* Adjust list head */
  if (mode.list_head == variable) {
    mode.list_head = mode.decision_list[variable - 1].next;
    } else {
    /* Detach variable from list */
    Variable current_prev = mode.decision_list[variable - 1].prev;
    Variable current_next = mode.decision_list[variable - 1].next;
    mode.decision_list[current_prev - 1].next = current_next;
    mode.decision_list[current_next - 1].prev = current_prev;

    /* Insert variable as list tail */
    Variable current_head_prev = mode.decision_list[current_head - 1].prev;
    mode.decision_list[current_head - 1].prev = variable;
    mode.decision_list[variable - 1].next = current_head;
    mode.decision_list[variable - 1].prev = current_head_prev;
    mode.decision_list[current_head_prev - 1].next = variable;
  }
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
  if (mode_type == ExistMode) {
    mode_type = UnivMode;
    mode = &univ_mode;
  } else if (mode_type == UnivMode) {
    mode_type = ExistMode;
    mode = &exist_mode;
  }
  resetTimestamps();
  mode->next_search = mode->list_head;
  phase_saving.notifyToggleDecisionMode();
}

void DecisionHeuristicSplitVMTF::addVariable(bool auxiliary) {
  is_auxiliary.push_back(auxiliary);
  phase_saving.addVariable();
  addVariable(auxiliary, exist_mode);
  addVariable(auxiliary, univ_mode);
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

void DecisionHeuristicSplitVMTF::clearOverflowQueue() {
  while (!mode->overflow_queue.empty()) {
    auto variable = mode->overflow_queue.top();
    auto watcher = solver.dependency_manager->watcher(variable);
    if ((watcher == 0 || (solver.variable_data_store->isAssigned(watcher) &&
        (solver.variable_data_store->varDecisionLevel(watcher) < backtrack_decision_level_before))) &&
        (mode->decision_list[variable - 1].timestamp > mode->decision_list[mode->next_search - 1].timestamp)) {
      mode->next_search = variable;
    }
    mode->overflow_queue.pop();
  }
}

int32_t DecisionHeuristicSplitVMTF::maxTimestampEligible() {
  Variable v = mode->list_head;
  int32_t max_timestamp = 0;
  do {
    if (solver.dependency_manager->isDecisionCandidate(v) && mode->decision_list[v - 1].timestamp > max_timestamp) {
      assert(!is_auxiliary[v - 1]);
      max_timestamp = mode->decision_list[v - 1].timestamp;
    }
    v = mode->decision_list[v - 1].next;
  } while (v != mode->list_head);
  return max_timestamp;
}

bool DecisionHeuristicSplitVMTF::isConstraintTypeOfMode(ConstraintType constraint_type) {
  if (mode_type == ExistMode) {
    return constraint_type == terms;
  } else if (mode_type == UnivMode) {
    return constraint_type == clauses;
  }
  assert(false);
  return false;
}

}
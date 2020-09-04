#include "decision_heuristic_split_VSIDS.hh"

namespace Qute {

DecisionHeuristicSplitVSIDS::DecisionHeuristicSplitVSIDS(QCDCL_solver& solver,
    bool no_phase_saving, uint32_t mode_cycles, double score_decay_factor, double score_increment,
    bool always_bump, bool split_phase_saving, bool start_univ_mode, bool tiebreak_scores,
    bool use_secondary_occurrences_for_tiebreaking, bool prefer_fewer_occurrences):
      DecisionHeuristic(solver), no_phase_saving(no_phase_saving), always_bump(always_bump),
      tiebreak_scores(tiebreak_scores),
      use_secondary_occurrences_for_tiebreaking(use_secondary_occurrences_for_tiebreaking),
      score_decay_factor(score_decay_factor), mode_cycles(mode_cycles), cycle_counter(0),
      exist_mode(nr_literal_occurrences, score_increment, tiebreak_scores, prefer_fewer_occurrences),
      univ_mode(nr_literal_occurrences, score_increment, tiebreak_scores, prefer_fewer_occurrences) {

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

void DecisionHeuristicSplitVSIDS::notifyStart() {
  precomputeVariableOccurrences(use_secondary_occurrences_for_tiebreaking);
  for (Variable v = 1; v <= solver.variable_data_store->lastVariable(); v++) {
    if (!is_auxiliary[v - 1] && solver.dependency_manager->isDecisionCandidate(v)) {
      exist_mode.variable_queue.insert(v);
      univ_mode.variable_queue.insert(v);
    }
  }
}

void DecisionHeuristicSplitVSIDS::notifyUnassigned(Literal l) {
  Variable v = var(l);
  if (!is_auxiliary[v - 1]) {
    Variable watcher = solver.dependency_manager->watcher(v);
    /* If variable will be unassigned after backtracking but its watcher still assigned,
      variable is eligible for assignment after backtracking. */
    if ((watcher == 0 || (solver.variable_data_store->isAssigned(watcher) &&
        solver.variable_data_store->varDecisionLevel(watcher) < backtrack_decision_level_before)) &&
        !mode->variable_queue.inHeap(v)) {

      // Add variable to both queues. Redundant copies will be removed when a decision literal is requested.
      exist_mode.variable_queue.insert(v);
      univ_mode.variable_queue.insert(v);
    }
  }
}

void DecisionHeuristicSplitVSIDS::notifyLearned(Constraint& c, ConstraintType constraint_type,
    vector<Literal>& conflict_side_literals) {

  if (always_bump) {
    if (constraint_type == terms) {
      bumpVariableScores(c, exist_mode);
      decayVariableScores(exist_mode);
    } else if (constraint_type == clauses) {
      bumpVariableScores(c, univ_mode);
      decayVariableScores(univ_mode);
    }
  } else if (isConstraintTypeOfMode(constraint_type)) {
    bumpVariableScores(c, *mode);
    decayVariableScores(*mode);
  }
}

void DecisionHeuristicSplitVSIDS::notifyRestart() {
  cycle_counter++;
  if (cycle_counter >= mode_cycles) {
    toggleMode();
    cycle_counter = 0;
  }
}

Literal DecisionHeuristicSplitVSIDS::getDecisionLiteral() {
  Variable candidate = 0;
  while (!mode->variable_queue.empty() && !solver.dependency_manager->isDecisionCandidate(mode->variable_queue[0])) {
    popFromVariableQueue();
  }
  candidate = popFromVariableQueue();
  assert(candidate != 0);
  assert(!is_auxiliary[candidate - 1]);
  assert(solver.dependency_manager->isDecisionCandidate(candidate));
  assert(mode->variable_activity[candidate] == getBestDecisionVariableScore());
  if (no_phase_saving || !phase_saving.hasPhase(candidate)) {
    phase_saving.savePhase(candidate, phaseHeuristic(candidate));
  }
  return mkLiteral(candidate, phase_saving.getPhase(candidate));
}

double DecisionHeuristicSplitVSIDS::getBestDecisionVariableScore() {
  bool assigned = false;
  double best_decision_variable_score = 0;
  for (Variable v = 1; v <= solver.variable_data_store->lastVariable(); v++) {
    if (((mode->variable_activity[v] > best_decision_variable_score) || !assigned) && solver.dependency_manager->isDecisionCandidate(v)) {
      best_decision_variable_score = mode->variable_activity[v];
      assigned = true;
    }
  }
  return best_decision_variable_score;
}

void DecisionHeuristicSplitVSIDS::precomputeVariableOccurrences(bool use_secondary_occurrences_for_tiebreaking) {
  for (Variable v = 1; v <= solver.variable_data_store->lastVariable(); v++) {
    if (!is_auxiliary[v - 1] && solver.dependency_manager->isDecisionCandidate(v)) {
      /* Existentials are 'primary' literals in clauses and 'secondary' in terms.
         Conversely, universals are 'primary' in terms and 'secondary' in clauses. */
      ConstraintType constraint_type =
        constraint_types[use_secondary_occurrences_for_tiebreaking ^ solver.variable_data_store->varType(v)];
      nr_literal_occurrences.insert(v, nrLiteralOccurrences(mkLiteral(v, true), constraint_type) +
        nrLiteralOccurrences(mkLiteral(v, false), constraint_type));
    }
  }
}

void DecisionHeuristicSplitVSIDS::bumpVariableScores(Constraint& c, DecisionModeData& mode) {
  for (auto literal: c) {
    Variable v = var(literal);
    if (solver.variable_data_store->isAssigned(v) && !is_auxiliary[v - 1]) {
      bumpVariableScore(v, mode);
    }
  }
}

void DecisionHeuristicSplitVSIDS::bumpVariableScore(Variable v, DecisionModeData& mode) {
  mode.variable_activity[v] += mode.score_increment;
  if (mode.variable_queue.inHeap(v)) {
    mode.variable_queue.update(v);
  }
  if (mode.variable_activity[v] > 1e60) {
    rescaleVariableScores(mode);
  }
}

void DecisionHeuristicSplitVSIDS::rescaleVariableScores(DecisionModeData& mode) {
  for (Variable v = 1; v <= solver.variable_data_store->lastVariable(); v++) {
    mode.variable_activity[v] *= 1e-60;
    if (mode.variable_queue.inHeap(v)) {
      mode.variable_queue.update(v);
    }
  }
  mode.score_increment *= 1e-60;
}

void DecisionHeuristicSplitVSIDS::addVariable(bool auxiliary) {
  is_auxiliary.push_back(auxiliary);
  phase_saving.addVariable();
  exist_mode.variable_activity.insert(solver.variable_data_store->lastVariable(), 0);
  univ_mode.variable_activity.insert(solver.variable_data_store->lastVariable(), 0);
}

void DecisionHeuristicSplitVSIDS::toggleMode() {
  if (mode_type == ExistMode) {
    mode_type = UnivMode;
    mode = &univ_mode;
  } else if (mode_type == UnivMode) {
    mode_type = ExistMode;
    mode = &exist_mode;
  }
  phase_saving.notifyToggleDecisionMode();
}

bool DecisionHeuristicSplitVSIDS::isConstraintTypeOfMode(ConstraintType constraint_type) {
  if (mode_type == ExistMode) {
    return constraint_type == terms;
  } else if (mode_type == UnivMode) {
    return constraint_type == clauses;
  }
  assert(false);
  return false;
}

}
#ifndef decision_heuristic_split_VSIDS_hh
#define decision_heuristic_split_VSIDS_hh

#include "qcdcl.hh"
#include "solver_types.hh"
#include "constraint.hh"
#include "phase_saving.hh"
#include "split_phase_saving.hh"

#include "minisat/mtl/Heap.h"
#include "minisat/mtl/IntMap.h"

using std::find;
using Minisat::Heap;
using Minisat::IntMap;

namespace Qute {

class DecisionHeuristicSplitVSIDS: public DecisionHeuristic {

public:
  DecisionHeuristicSplitVSIDS(QCDCL_solver& solver, bool no_phase_saving, uint32_t mode_cycles,
    double score_decay_factor, double score_increment, bool always_bump, bool split_phase_saving, bool start_univ_mode,
    bool tiebreak_scores, bool use_secondary_occurrences_for_tiebreaking, bool prefer_fewer_occurrences);
  virtual void addVariable(bool auxiliary);
  virtual void notifyStart();
  virtual void notifyAssigned(Literal l);
  virtual void notifyUnassigned(Literal l);
  virtual void notifyEligible(Variable v);
  virtual void notifyLearned(Constraint& c, ConstraintType constraint_type, vector<Literal>& conflict_side_literals);
  virtual void notifyBacktrack(uint32_t decision_level_before);
  virtual void notifyRestart();
  virtual Literal getDecisionLiteral();

protected:
  struct DecisionModeData;

  void precomputeVariableOccurrences(bool use_secondary_occurrences_for_tiebreaking);
  void bumpVariableScore(Variable v, DecisionModeData& mode);
  void bumpVariableScores(Constraint& c, DecisionModeData& mode);
  void rescaleVariableScores(DecisionModeData& mode);
  void decayVariableScores(DecisionModeData& mode);
  Variable popFromVariableQueue();
  double getBestDecisionVariableScore();
  vector<Variable> getVariablesWithTopScore();
  void popVariableWithTopScore(Variable v);
  Variable pickVarUsingOccurrences(vector<Variable>& candidates, bool prefer_fewer_occurrences);
  void toggleMode();
  bool isConstraintTypeOfMode(ConstraintType constraint_type);

  struct CompareVariables
  {
    bool tiebreak_scores;
    bool prefer_fewer_occurrences;
    const IntMap<Variable, double>& variable_activity;
    const IntMap<Variable, int>& nr_literal_occurrences;

    bool operator()(const Variable first, const Variable second) const {
      if (!tiebreak_scores) {
        return variable_activity[first] > variable_activity[second];
      } else if (prefer_fewer_occurrences) {
        return (variable_activity[first] > variable_activity[second]) || (
            variable_activity[first] == variable_activity[second] &&
            nr_literal_occurrences[first] < nr_literal_occurrences[second]
          );
      } else {
        return (variable_activity[first] > variable_activity[second]) || (
          variable_activity[first] == variable_activity[second] &&
          nr_literal_occurrences[first] > nr_literal_occurrences[second]
        );
      }
    }

    CompareVariables(const IntMap<Variable, double>& variable_activity,
      const IntMap<Variable, int>& nr_literal_occurrences, bool tiebreak_scores,
      bool prefer_fewer_occurrences):
        tiebreak_scores(tiebreak_scores), prefer_fewer_occurrences(prefer_fewer_occurrences),
        variable_activity(variable_activity), nr_literal_occurrences(nr_literal_occurrences) {}
  };

  struct DecisionModeData
  {
    double score_increment;
    IntMap<Variable, double> variable_activity;
    Heap<Variable,CompareVariables> variable_queue;
    DecisionModeData(const IntMap<Variable, int>& nr_literal_occurrences, double score_increment,
      bool tiebreak_scores, bool prefer_fewer_occurrences):
        score_increment(score_increment), variable_activity(),
        variable_queue(CompareVariables(variable_activity, nr_literal_occurrences,
          tiebreak_scores, prefer_fewer_occurrences)) {}
  };

  const bool no_phase_saving;
  const bool always_bump;
  const bool tiebreak_scores;
  const bool use_secondary_occurrences_for_tiebreaking;
  const double score_decay_factor;
  const uint32_t mode_cycles;
  u_int32_t cycle_counter;

  DecisionMode mode_type;
  DecisionModeData* mode;
  DecisionModeData exist_mode;
  DecisionModeData univ_mode;
  PhaseSaving phase_saving;
  
  uint32_t backtrack_decision_level_before;

  vector<bool> is_auxiliary;
  IntMap<Variable, int> nr_literal_occurrences;
};

// Implementation of inline methods

inline void DecisionHeuristicSplitVSIDS::notifyAssigned(Literal l) {
  phase_saving.savePhase(var(l), sign(l));
}

inline void DecisionHeuristicSplitVSIDS::notifyEligible(Variable v) {
  if (!is_auxiliary[v - 1]) {
    mode->variable_queue.update(v);
  }
}

inline void DecisionHeuristicSplitVSIDS::notifyBacktrack(uint32_t decision_level_before) {
  backtrack_decision_level_before = decision_level_before;
}

inline void DecisionHeuristicSplitVSIDS::decayVariableScores(DecisionModeData& mode) {
  mode.score_increment *= (1 / score_decay_factor);
}

inline Variable DecisionHeuristicSplitVSIDS::popFromVariableQueue() {
  assert(!mode->variable_queue.empty());
  return mode->variable_queue.removeMin();
}

}

#endif
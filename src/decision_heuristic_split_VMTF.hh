#ifndef decision_heuristic_split_VMTF_hh
#define decision_heuristic_split_VMTF_hh

#include <vector>
#include <queue>
#include <random>
#include "decision_heuristic.hh"
#include "phase_saving.hh"
#include "split_phase_saving.hh"

using std::vector;
using std::priority_queue;
using std::random_device;
using std::bernoulli_distribution;

namespace Qute {

class DecisionHeuristicSplitVMTF: public DecisionHeuristic {

public:
  DecisionHeuristicSplitVMTF(QCDCL_solver& solver, bool no_phase_saving,
    uint32_t mode_cycles, bool always_move, bool move_by_prefix,
    bool split_phase_saving, bool start_univ_mode);

  virtual void addVariable(bool auxiliary);
  virtual void notifyStart();
  virtual void notifyAssigned(Literal l);
  virtual void notifyEligible(Variable v);
  virtual void notifyUnassigned(Literal l);
  virtual void notifyLearned(Constraint& c, ConstraintType constraint_type,
    vector<Literal>& conflict_side_literals);
  virtual void notifyBacktrack(uint32_t decision_level_before);
  virtual void notifyRestart();
  virtual Literal getDecisionLiteral();

protected:
  struct DecisionModeData;

  void resetTimestamps();
  void moveToFront(Variable variable, DecisionModeData& mode);
  void clearOverflowQueue();
  uint32_t maxTimestampEligible();
  bool checkOrder();
  void toggleMode();
  void addVariable(bool auxiliary, DecisionModeData& mode);
  void notifyStart(DecisionModeData& mode);
  bool isConstraintTypeOfMode(ConstraintType constraint_type);
  void moveVariables(Constraint& c, DecisionModeData& mode);
  void moveVariablesByPrefix(Constraint& c, DecisionModeData& mode);
  void moveVariablesArbitrary(Constraint& c, DecisionModeData& mode);

  struct ListEntry
  {
    Variable prev;
    uint32_t timestamp;
    Variable next;
    ListEntry(): prev(1), timestamp(0), next(1) {}
  };
  
  struct CompareVariables
  {
    vector<ListEntry>& decision_list;
    bool operator()(const Variable& first, const Variable& second) const {
      return decision_list[first - 1].timestamp < decision_list[second - 1].timestamp;
    }
    CompareVariables(vector<ListEntry>& decision_list): decision_list(decision_list) {}
  };

  struct DecisionModeData
  {
    Variable list_head;
    Variable next_search;
    vector<ListEntry> decision_list;
    priority_queue<Variable, vector<Variable>, CompareVariables> overflow_queue;
    DecisionModeData(): list_head(0), next_search(0), overflow_queue(CompareVariables(decision_list)) {}
  };

  const bool always_move;
  const bool move_by_prefix;
  const uint32_t mode_cycles;
  u_int32_t cycle_counter;
  
  DecisionMode mode_type;
  DecisionModeData* mode;
  DecisionModeData exist_mode;
  DecisionModeData univ_mode;
  PhaseSaving phase_saving;

  uint32_t timestamp;
  uint32_t backtrack_decision_level_before;

  bool no_phase_saving;
  vector<bool> is_auxiliary;

};

// Implementation of inline methods.

inline void DecisionHeuristicSplitVMTF::notifyStart() {
  notifyStart(exist_mode);
  notifyStart(univ_mode);
}

inline void DecisionHeuristicSplitVMTF::notifyAssigned(Literal l) {
  phase_saving.savePhase(var(l), sign(l));
}

inline void DecisionHeuristicSplitVMTF::notifyEligible(Variable v) {
  if (mode->decision_list[v - 1].timestamp > mode->decision_list[mode->next_search - 1].timestamp &&
      !is_auxiliary[v - 1]) {
    mode->overflow_queue.push(v);
  }
}

}

#endif
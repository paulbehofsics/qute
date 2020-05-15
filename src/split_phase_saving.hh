#ifndef split_phase_saving_hh
#define split_phase_saving_hh

#include <vector>
#include "solver_types.hh"
#include "phase_saving.hh"
#include "decision_heuristic_split_VMTF.hh"

namespace Qute {

class SplitPhaseSaving: public PhaseSaving {

public:
  SplitPhaseSaving(DecisionMode mode);
  virtual void addVariable();
  virtual void notifyToggleDecisionMode();
  virtual bool hasPhase(Variable v);
  virtual lbool getPhase(Variable v);
  virtual void savePhase(Variable v, lbool phase);

protected:
  DecisionMode mode;
  vector<lbool>* curr_saved_phase;
  vector<lbool> saved_phase_exist_mode;
  vector<lbool> saved_phase_univ_mode;

};

inline bool SplitPhaseSaving::hasPhase(Variable v) {
  return (*curr_saved_phase)[v] == l_Undef;
}

inline lbool SplitPhaseSaving::getPhase(Variable v) {
  return (*curr_saved_phase)[v];
}

inline void SplitPhaseSaving::savePhase(Variable v, lbool phase) {
  (*curr_saved_phase)[v] = phase;
}

}

#endif
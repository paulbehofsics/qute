#ifndef phase_saving_hh
#define phase_saving_hh

#include <vector>
#include "solver_types.hh"

namespace Qute {

class PhaseSaving {

public:
  PhaseSaving();
  virtual void addVariable();
  virtual void notifyToggleDecisionMode();
  virtual bool hasPhase(Variable v);
  virtual lbool getPhase(Variable v);
  virtual void savePhase(Variable v, lbool phase);

protected:
  vector<lbool> saved_phase;

};

inline bool PhaseSaving::hasPhase(Variable v) {
  return saved_phase[v] != l_Undef;
}

inline lbool PhaseSaving::getPhase(Variable v) {
  return saved_phase[v];
}

inline void PhaseSaving::savePhase(Variable v, lbool phase) {
  saved_phase[v] = phase;
}

}

#endif
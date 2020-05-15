#include "phase_saving.hh"

namespace Qute {

  PhaseSaving::PhaseSaving() {
    // dummy element at beginning of list to avoid decrementing indices
    // (variable indices start at 1)
    saved_phase.push_back(l_Undef);
  }

  void PhaseSaving::addVariable() {
    saved_phase.push_back(l_Undef);
  }

  void PhaseSaving::notifyToggleDecisionMode() {}

}
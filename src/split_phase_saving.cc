#include "split_phase_saving.hh"

namespace Qute {

  SplitPhaseSaving::SplitPhaseSaving(DecisionMode mode): mode(mode) {
    // dummy element at beginning of list to avoid decrementing indices
    // (variable indices start at 1)
    saved_phase_exist_mode.push_back(l_Undef);
    saved_phase_univ_mode.push_back(l_Undef);

    if (mode == ExistMode) {
      curr_saved_phase = &saved_phase_exist_mode;
    } else if (mode == UnivMode) {
      curr_saved_phase = &saved_phase_univ_mode;
    }
  }

  void SplitPhaseSaving::addVariable() {
    saved_phase_exist_mode.push_back(l_Undef);
    saved_phase_univ_mode.push_back(l_Undef);
  }

  void SplitPhaseSaving::notifyToggleDecisionMode() {
    if (mode == ExistMode) {
      mode = UnivMode;
      curr_saved_phase = &saved_phase_univ_mode;
    } else if (mode == UnivMode) {
      mode = ExistMode;
      curr_saved_phase = &saved_phase_exist_mode;
    }
  }

}
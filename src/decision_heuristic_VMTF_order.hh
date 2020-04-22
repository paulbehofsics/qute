#ifndef decision_heuristic_VMTF_order_hh
#define decision_heuristic_VMTF_order_hh

#include <vector>
#include <queue>
#include <random>
#include "decision_heuristic_VMTF_deplearn.hh"

using std::vector;
using std::priority_queue;
using std::random_device;
using std::bernoulli_distribution;

namespace Qute {

class DecisionHeuristicVMTForder: public DecisionHeuristicVMTFdeplearn {
  
public:
  DecisionHeuristicVMTForder(QCDCL_solver& solver, bool no_phase_saving);

  virtual void notifyLearned(Constraint& c, ConstraintType constraint_type, vector<Literal>& conflict_side_literals);

};

}

#endif
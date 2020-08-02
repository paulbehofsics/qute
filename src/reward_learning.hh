#ifndef reward_learning_hh
#define reward_learning_hh

#include "solver_types.hh"

#include "minisat/mtl/Heap.h"
#include "minisat/mtl/IntMap.h"

using Minisat::Heap;
using Minisat::IntMap;

namespace Qute {

// Manages the reward based learning of variable quality.
class RewardLearning {
  public:
    RewardLearning();
    void addVariable();
    void addCandidateVariable(Variable v);
    void addCandidateVariableIfMissing(Variable v);
    void updateCandidateVariable(Variable v);
    void assign(Variable v);
    void unassign(Variable v);
    void setRewardForAssigned(double reward);
    void finalizeRewardCycle();
    Variable popBestVariable();
    Variable peekBestVariable();
    bool hasBestVariable();

  private:
    struct CompareVariables
    {
      CompareVariables(const IntMap<Variable, double>& variable_quality): variable_quality(variable_quality) {}
      bool operator()(const Variable first, const Variable second) const {
        return variable_quality[first] > variable_quality[second];
      }
      const IntMap<Variable, double>& variable_quality;
    };

    struct VariableNode {
      Variable prev;
      Variable v;
      double reward;
      Variable next;
      VariableNode(): prev(0), v(0), reward(0), next(0) {}
    };

    class Iterator{
      RewardLearning& container;
      VariableNode* node;
    public:
      Iterator(RewardLearning& _container, VariableNode* _node) : container(_container), node(_node) {}

      void operator++()   { node = &container.nodes[node->next]; }
      Variable operator*() const { return node->v; }

      bool operator==(const Iterator& ti) const { return node->v == ti.node->v; }
      bool operator!=(const Iterator& ti) const { return node->v != ti.node->v; }
    };
    
    void acceptReward(Variable v);
    void resetReward(Variable v);
    Iterator begin();
    Iterator end();
    size_t size();

    const double step_size = 0.2;
    // The list nodes of all variables. nodes[0] represents the base of the circular list of assigned variables.
    vector<VariableNode> nodes;
    // The learnt quality of all variables. The best variable is determined this way.
    IntMap<Variable, double> variable_quality;
    // Heap that contains the best variable at the top.
    Heap<Variable,CompareVariables> variable_heap;
};

}

#endif
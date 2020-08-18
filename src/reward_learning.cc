#include "reward_learning.hh"

namespace Qute {

RewardLearning::RewardLearning(double step_size):
    variable_heap(CompareVariables(variable_quality)), step_size(step_size) {
  // Insert empty node that represents the base of the circular list
  nodes.emplace_back();
}

void RewardLearning::addVariable() {
  Variable v = size();
  VariableNode node = VariableNode();
  node.v = v;
  nodes.push_back(node);
  variable_quality.insert(v, 0);
}

void RewardLearning::addCandidateVariable(Variable v) {
  variable_heap.insert(v);
}

void RewardLearning::addCandidateVariableIfMissing(Variable v) {
  if (!variable_heap.inHeap(v)) {
    variable_heap.insert(v);
  }
}

void RewardLearning::updateCandidateVariable(Variable v) {
  variable_heap.update(v);
}

void RewardLearning::assign(Variable v) {
  Variable prev = nodes[0].prev;
  nodes[prev].next = v;
  nodes[0].prev = v;
  nodes[v].prev = prev;
  nodes[v].next = 0;
}

void RewardLearning::unassign(Variable v) {
  Variable prev = nodes[v].prev;
  Variable next = nodes[v].next;
  nodes[prev].next = next;
  nodes[next].prev = prev;
  nodes[v].prev = 0;
  nodes[v].next = 0;
}

void RewardLearning::setReward(Variable v, double reward) {
  nodes[v].reward = reward;
}

void RewardLearning::finalizeRewardCycle() {
  for (auto iter = begin(); iter != end(); ++iter) {
    acceptReward(*iter);
    resetReward(*iter);
  }
}

Variable RewardLearning::popBestVariable() {
  return variable_heap.removeMin();
}

Variable RewardLearning::peekBestVariable() {
  return variable_heap[0];
}

bool RewardLearning::hasBestVariable() {
  return !variable_heap.empty();
}

void RewardLearning::acceptReward(Variable v) {
  double reward = nodes[v].reward;
  variable_quality[v] = (1 - step_size) * variable_quality[v] + step_size * reward;
  variable_heap.update(v);
}

void RewardLearning::resetReward(Variable v) {
  nodes[v].reward = 0;
}

RewardLearning::Iterator RewardLearning::begin() {
  return Iterator(*this, &nodes[nodes[0].next]);
}

RewardLearning::Iterator RewardLearning::end() {
  return Iterator(*this, &nodes[0]);
}

size_t RewardLearning::size() {
  return nodes.size();
}

}
#include "variable_subset.hh"

namespace Qute {

VariableSubset::VariableSubset() {
  // Insert empty node that represents the base of the circular list
  nodes.emplace_back();
}

void VariableSubset::insert(Variable v) {
  Variable prev = nodes[0].prev;
  nodes[prev].next = v;
  nodes[0].prev = v;
  nodes[v].prev = prev;
  nodes[v].next = 0;
}

void VariableSubset::remove(Variable v) {
  Variable prev = nodes[v].prev;
  Variable next = nodes[v].next;
  nodes[prev].next = next;
  nodes[next].prev = prev;
  nodes[v].prev = 0;
  nodes[v].next = 0;
}

void VariableSubset::addVariable() {
  VariableNode node = VariableNode();
  node.v = size();
  nodes.push_back(node);
}

size_t VariableSubset::size() {
  return nodes.size();
}

VariableSubset::Iterator VariableSubset::begin() {
  return Iterator(*this, &nodes[nodes[0].next]);
}

VariableSubset::Iterator VariableSubset::end() {
  return Iterator(*this, &nodes[0]);
}

}
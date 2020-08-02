#ifndef variable_subset_hh
#define variable_subset_hh

#include "solver_types.hh"

namespace Qute {

// Manages a subset of all added variables by providing constant time insert and remove operations.
class VariableSubset {
  private:
    struct VariableNode {
      Variable prev;
      Variable v;
      Variable next;
      VariableNode(): prev(0), v(0), next(0) {}
    };
  public:
    class Iterator{
      VariableSubset& container;
      VariableNode* node;
    public:
      Iterator(VariableSubset& _container, VariableNode* _node) : container(_container), node(_node) {}

      void operator++()   { node = &container.nodes[node->next]; }
      Variable operator*() const { return node->v; }

      bool operator==(const Iterator& ti) const { return node->v == ti.node->v; }
      bool operator!=(const Iterator& ti) const { return node->v != ti.node->v; }
    };

    VariableSubset();
    void insert(Variable v);
    void remove(Variable v);
    void addVariable();
    Iterator begin();
    Iterator end();

  private:
    size_t size();

    vector<VariableNode> nodes;
};

}

#endif
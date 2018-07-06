#include "watched_literal_propagator.hh"
#include "logging.hh"

namespace Qute {

WatchedLiteralPropagator::WatchedLiteralPropagator(QCDCL_solver& solver, bool model_generation_approx_hs, double exponent, double scaling_factor, double universal_penalty): solver(solver), constraints_watched_by{vector<vector<WatchedRecord>>(2), vector<vector<WatchedRecord>>(2)}, exponent(exponent), scaling_factor(scaling_factor), universal_penalty(universal_penalty) {
  if (model_generation_approx_hs) {
    generateModel = &WatchedLiteralPropagator::generateModelApproxHittingSet;
  } else {
    generateModel = &WatchedLiteralPropagator::generateModelSimple;
  }
}

void WatchedLiteralPropagator::notifyStart() {
  /* Weights are always only assigned up to the last universal, because
   * the final existential block, if there is any, has weight 0.
   */
  Variable last_universal = 0;
  for (Variable v = 1; v <= solver.variable_data_store->lastVariable(); v++) {
    if (solver.variable_data_store->varType(v) == 1) {
      last_universal = v;
    }
  }
  variable_weights.push_back(1.0); // dummy value at index 0 in order to be able to index by variable name
  /* In weighted mode, the following model is used to distribute weights.
   * By w(v) we denote the weight of a variable v, by c(v), we denote
   * an auxiliary quantity called the cost. Furthermore, for an existential v,
   * let Q(v) be the total number of universal variables to the right of v,
   * and for a universal v, let Q(v) be the total number of existential variables
   * to the left of v, and let E be the total number of existential variables, and
   * U be the total number of universal variables.
   * Let the real parameters to this function be denoted by e, s, and p
   * respectively.
   *
   * c(v) = Q(v)/E if v is universal, and Q(v)/U if v is existential
   * w(v) = 1 + (c(v)^e)*s + p if v is universal
   * w(v) = 1 + (c(v)^e)*s     if v is existential
   *
   * In order to set all weights to 1 (and obtain what was previously called
   * 'heuristic' mode), set s=0 and p=0, i.e. run with
   *   --variable-weights-scaling-factor=0
   *   --variable-weights-universal-penalty=0
   */
  uint32_t variables_seen[2] = {1, 0};
  /* As we iterate over the variables, we will keep track
   * how many of each type we have seen to determine the
   * costs. As the cost of an existential is based on the
   * number of universals to the right of it, we will push
   * negative numbers to the cost, and at the end add the
   * total number of universals to the cost of every existential.
   *
   * We offset the number of existential variables seen by 1,
   * because if there is only one universal block (except for the
   * final existential one), we would have division by 0.
   */
  vector<uint32_t> costs;
  costs.push_back(1);
  for (Variable v = 1; v <= last_universal; v++) {
    uint32_t qtype = solver.variable_data_store->varType(v);
    uint32_t multiplier = qtype*2 - 1;
    costs.push_back(variables_seen[1-qtype]*multiplier);
    variables_seen[qtype]++;
  }
  for (Variable v = 1; v <= last_universal; v++) {
    uint32_t qtype = solver.variable_data_store->varType(v);
    if(qtype == 0)
      costs[v] += variables_seen[1];
  }
  for (Variable v = 1; v <= last_universal; v++) {
    bool qtype = solver.variable_data_store->varType(v);
    double cost = ((double) costs[v]) / variables_seen[1-qtype];
    double penalty = (qtype == 1) ? universal_penalty : 0.0;
    variable_weights.push_back(scaling_factor*std::pow(cost, exponent) + 1 + penalty);
  }
}

CRef WatchedLiteralPropagator::propagate(ConstraintType& constraint_type) {
  if (solver.variable_data_store->decisionLevel() == 0) {
    for (ConstraintType _constraint_type: constraint_types) {
      vector<CRef>::iterator i, j;
      for (i = j = constraints_without_two_watchers[_constraint_type].begin(); i != constraints_without_two_watchers[_constraint_type].end(); ++i) {
        bool watchers_found = false;
        if (!propagateUnwatched(*i, _constraint_type, watchers_found)) {
          CRef empty_constraint_reference = *i;
           // Constraint is empty: clean up, return constraint_reference.
          for (; i != constraints_without_two_watchers[_constraint_type].end(); i++, j++) {
            *j = *i;
          }
          constraints_without_two_watchers[_constraint_type].resize(j - constraints_without_two_watchers[_constraint_type].begin(), CRef_Undef);
          constraint_type = _constraint_type;
          return empty_constraint_reference;
        } else if (!watchers_found) {
          *j++ = *i;
        }
      }
      constraints_without_two_watchers[_constraint_type].resize(j - constraints_without_two_watchers[_constraint_type].begin(), CRef_Undef);
    }
  }
  while (!propagation_queue.empty()) {
    Literal to_propagate = propagation_queue.back();
    propagation_queue.pop_back();
    //LOG(trace) << "Propagating literal: " << (sign(to_propagate) ? "" : "-") << var(to_propagate) << std::endl;
    for (ConstraintType _constraint_type: constraint_types) {
      Literal watcher = ~(to_propagate ^ _constraint_type);
      vector<WatchedRecord>& record_vector = constraints_watched_by[_constraint_type][toInt(watcher)];
      vector<WatchedRecord>::iterator i, j;
      for (i = j = record_vector.begin(); i != record_vector.end(); ++i) {
        WatchedRecord& record = *i;
        CRef constraint_reference = record.constraint_reference;
        Literal blocker = record.blocker;
        bool watcher_changed = false;
        if (!disablesConstraint(blocker, _constraint_type)) {
          Constraint& constraint = solver.constraint_database->getConstraint(constraint_reference, _constraint_type);
          if (constraintIsWatchedByLiteral(constraint, watcher)) { // if we want to allow for removal, add "&& !constraint.isMarked()"
            if (!updateWatchedLiterals(constraint, constraint_reference, _constraint_type, watcher_changed)) {
              // Constraint is empty: clean up, return constraint_reference.
              for (; i != record_vector.end(); i++, j++) {
                *j = *i;
              }
              record_vector.resize(j - record_vector.begin(), WatchedRecord(CRef_Undef, Literal_Undef));
              constraint_type = _constraint_type;
              return constraint_reference;
            }
          } else {
            watcher_changed = true;
          }
        }
        if (!watcher_changed) {
          *j++ = record;
        }
      }
      record_vector.resize(j - record_vector.begin(), WatchedRecord(CRef_Undef, Literal_Undef));
    }
  }
  assert(propagationCorrect());
  if (solver.variable_data_store->allAssigned()) { 
  /* Every variable is assigned but no conflict/solution is detected.
     Use the model generation rule to obtain an initial term. */
    vector<Literal> initial_term;
    (*this.*generateModel)(initial_term);
    CRef initial_term_reference = solver.constraint_database->addConstraint(initial_term, ConstraintType::terms, true);
    solver.constraint_database->getConstraint(initial_term_reference, ConstraintType::terms).mark(); // Immediately mark for removal upon constraint cleaning.
    assert(solver.debug_helper->isEmpty(solver.constraint_database->getConstraint(initial_term_reference, ConstraintType::terms), ConstraintType::terms));
    constraint_type = ConstraintType::terms;
    return initial_term_reference;
  } else {
    return CRef_Undef;
  }
}


void WatchedLiteralPropagator::generateModelSimple(vector<Literal>& model) {
  vector<bool> characteristic_function(solver.variable_data_store->lastVariable() + solver.variable_data_store->lastVariable() + 2);
  fill(characteristic_function.begin(), characteristic_function.end(), false);
  for (vector<CRef>::const_iterator it = solver.constraint_database->constraintReferencesBegin(ConstraintType::clauses, false); 
       it != solver.constraint_database->constraintReferencesEnd(ConstraintType::clauses, false); 
       ++it) {
    CRef constraint_reference = *it;
    Constraint& input_clause = solver.constraint_database->getConstraint(constraint_reference, ConstraintType::clauses);
    Literal disabling = findDisabling(input_clause, ConstraintType::clauses, false);
    if (disabling == Literal_Undef) {
      disabling = findDisabling(input_clause, ConstraintType::clauses, true);
    }
    characteristic_function[toInt(disabling)] = true;
  }
  for (unsigned i = 0; i < characteristic_function.size(); i++) {
    if (characteristic_function[i]) {
      model.push_back(toLiteral(i));
    }
  }
}

void WatchedLiteralPropagator::generateModelApproxHittingSet(vector<Literal>& model) {
  /* Create an initial term from a satisfying assignment
   * using the greedy hitting-set approximation algorithm,
   * which keeps taking the literal that satisfies the
   * most unsatisfied clauses at every step. This
   * algorithm is guaranteed to produce a hitting set
   * at most lg(n) times larger than the optimal one.
   *
   * Furter possible optimization in the case of QBF is automatically
   * adding innermost existential variables to the initial term.
   * These variables can be reduced from any term, so they are never
   * actually added to the term, but clauses covered by them are
   * skipped in the following.
   */
  Variable last_universal = 0;
  for (Variable v = 1; v <= solver.variable_data_store->lastVariable(); v++) {
    if (solver.variable_data_store->varType(v) == 1) {
      last_universal = v;
    }
  }

  // build a vector of sets of clauses such that occurences[var] is
  // the set that contains all clauses in which the literal of var
  // which is currently set to true occurs. The inner data structure
  // must be a set, because we need fast iteration and removal by value.
  vector<std::unordered_set<CRef>> occurrences(last_universal+1);
  vector<Variable> temp_true_variables_of_clause;
  for (vector<CRef>::const_iterator it = solver.constraint_database->constraintReferencesBegin(ConstraintType::clauses, false);
      it != solver.constraint_database->constraintReferencesEnd(ConstraintType::clauses, false);
      it++) {
    Constraint& clause = solver.constraint_database->getConstraint(*it, ConstraintType::clauses);
    bool already_covered = false;
    for (Literal lit : clause) {
      Variable litvar = var(lit);
      if (disablesConstraint(lit, ConstraintType::clauses)) {
        if (litvar > last_universal) {
          already_covered = true;
          break;
        }
        temp_true_variables_of_clause.push_back(litvar);
      }
    }
    if (!already_covered) {
      for (Variable current_var : temp_true_variables_of_clause)
        occurrences[current_var].insert(*it);
    }
    temp_true_variables_of_clause.clear();
  }

  int max_occurrences = -1;
  for (Variable current_var = 1; current_var <= last_universal; current_var++) {
    int num_occurences = int(occurrences[current_var].size() / variable_weights[current_var]);
    if (num_occurences > max_occurrences)
      max_occurrences = num_occurences; 
  }

  // distribute variables into buckets based on the number of clauses satisfied by them
  // this serves as an efficiently updateable sorted list
  vector<vector<Variable>> buckets(max_occurrences + 1);

  // helper_data[var][0] is true iff var is to be moved
  // to a different bucket during the processing of another variable
  // (this happens because some clauses will be covered by the other
  // variable and therefore will be removed from the occurrences of var
  // helper_data[var][1] is the index of var in its bucket
  //
  // note that the bucket to which a variable corresponds can be retrieved
  // as occurrences[var].size() and this will be maintained in the following
  uint32_t helper_data[last_universal+1][2];
  for (Variable current_var = 1; current_var <= last_universal; current_var++) {
    if (occurrences[current_var].size() > 0){
      vector<Variable>& bucket = buckets[(int) (occurrences[current_var].size() / variable_weights[current_var])];
      helper_data[current_var][0] = 0;
      helper_data[current_var][1] = bucket.size();
      bucket.push_back(current_var);
    }
  }

  // if a variable is to be moved to a different bucket, we will push it to
  // this stack so that we can later traverse affected variables efficiently
  vector<Variable> affected;
  while (max_occurrences >= 0) {
    assert(buckets[max_occurrences].size() > 0);

    Variable current_var = buckets[max_occurrences].back();
    buckets[max_occurrences].pop_back();
    model.push_back(mkLiteral(current_var, solver.variable_data_store->assignment(current_var)));

    for (CRef cref : occurrences[current_var]) {
      Constraint& clause = solver.constraint_database->getConstraint(cref, ConstraintType::clauses);
      for (Literal lit : clause) {
        // if lit is satisfied, remove clause from the occurrences of lit,
        // because clause is now covered by current_var
        Variable litvar = var(lit);
        if (litvar != current_var && disablesConstraint(lit, ConstraintType::clauses)) {
          if (helper_data[litvar][0] == 0) {
            // the variable has become affected now and must be
            // unlinked from its bucket and added to the list of
            // affected variables
            helper_data[litvar][0] = 1;
            affected.push_back(litvar);

            vector<Variable>& bucket = buckets[(int) (occurrences[litvar].size() / variable_weights[litvar])];
            // unlink by moving last element of bucket to position of litvar
            // abd updating helper_data accordingly
            helper_data[bucket.back()][1] = helper_data[litvar][1];
            bucket[helper_data[litvar][1]] = bucket.back();
            bucket.pop_back();
          }
          occurrences[litvar].erase(cref);
        }
      }
    }
    for (Variable var : affected) {
      if (occurrences[var].size() > 0) {
        vector<Variable>& bucket = buckets[(int) (occurrences[var].size() / variable_weights[var])];
        helper_data[var][0] = 0;
        helper_data[var][1] = bucket.size();
        bucket.push_back(var);
      }
    }
    affected.clear();
    while (max_occurrences >= 0 && buckets[max_occurrences].size() == 0)
      max_occurrences--;
  }
}

void WatchedLiteralPropagator::addConstraint(CRef constraint_reference, ConstraintType constraint_type) {
  Constraint& constraint = solver.constraint_database->getConstraint(constraint_reference, constraint_type);
  uint32_t first_watcher_index = findFirstWatcher(constraint, constraint_type);
  if (first_watcher_index < constraint.size) {
    std::swap(constraint[0], constraint[first_watcher_index]);
  } else {
    constraints_without_two_watchers[constraint_type].push_back(constraint_reference);
    return;
  }
  uint32_t second_watcher_index = findSecondWatcher(constraint, constraint_type);
  if (second_watcher_index < constraint.size) {
    std::swap(constraint[1], constraint[second_watcher_index]);
  } else {
    constraints_without_two_watchers[constraint_type].push_back(constraint_reference);
    return;
  }
  constraints_watched_by[constraint_type][toInt(constraint[0])].emplace_back(constraint_reference, constraint[1]);
  constraints_watched_by[constraint_type][toInt(constraint[1])].emplace_back(constraint_reference, constraint[0]);
}

void WatchedLiteralPropagator::relocConstraintReferences(ConstraintType constraint_type) {
  for (unsigned literal_int = Min_Literal_Int; literal_int < constraints_watched_by[constraint_type].size(); literal_int++) {
    vector<WatchedRecord>& watched_records = constraints_watched_by[constraint_type][literal_int];
    vector<WatchedRecord>::iterator i, j;
    for (i = j = watched_records.begin(); i != watched_records.end(); ++i) {
      WatchedRecord& record = *i;
      Constraint& constraint = solver.constraint_database->getConstraint(record.constraint_reference, constraint_type);
      if (!constraint.isMarked()) {
        solver.constraint_database->relocate(record.constraint_reference, constraint_type);
        *j++ = *i;
      }
    }
    watched_records.resize(j - watched_records.begin(), WatchedRecord(CRef_Undef, Literal_Undef));
  }
  vector<CRef>::iterator i, j;
  for (i = j = constraints_without_two_watchers[constraint_type].begin(); i != constraints_without_two_watchers[constraint_type].end(); ++i) {
    Constraint& constraint = solver.constraint_database->getConstraint(*i, constraint_type); 
    if (!constraint.isMarked()) {
      solver.constraint_database->relocate(*i, constraint_type);
      *j++ = *i;
    }
  }
  constraints_without_two_watchers[constraint_type].resize(j - constraints_without_two_watchers[constraint_type].begin());
}

bool WatchedLiteralPropagator::propagateUnwatched(CRef constraint_reference, ConstraintType constraint_type, bool& watchers_found) {
  Constraint& constraint = solver.constraint_database->getConstraint(constraint_reference, constraint_type);
  if ((constraint.size == 0 || solver.variable_data_store->varType(var(constraint[0])) != constraint_type) && !isDisabled(constraint, constraint_type)) {
    //LOG(trace) << (constraint_type ? "Term " : "Clause ") << "empty: " << solver.variable_data_store->constraintToString(constraint) << std::endl;
    assert(solver.debug_helper->isEmpty(constraint, constraint_type));
    return false;
  } else if (!isDisabled(constraint, constraint_type)) { // First watcher is a primary literal and constraint is not disabled. 
    uint32_t second_watcher_index = findSecondWatcher(constraint, constraint_type);
    if (second_watcher_index < constraint.size) {
      std::swap(constraint[1], constraint[second_watcher_index]);
      watchers_found = true;
      constraints_watched_by[constraint_type][toInt(constraint[0])].emplace_back(constraint_reference, constraint[1]);
      constraints_watched_by[constraint_type][toInt(constraint[1])].emplace_back(constraint_reference, constraint[0]);
      return true;
    } else {
      assert(solver.debug_helper->isEmpty(constraint, constraint_type) || solver.debug_helper->isUnit(constraint, constraint_type));
      //LOG(trace) << (constraint_type ? "Term " : "Clause ") << (isEmpty(constraint, constraint_type) ? "empty" : "unit") << ": " << solver.variable_data_store->constraintToString(constraint) << std::endl;
      return solver.enqueue(constraint[0] ^ constraint_type, constraint_reference);
    }
  }
  return true;
}

bool WatchedLiteralPropagator::updateWatchedLiterals(Constraint& constraint, CRef constraint_reference, ConstraintType constraint_type, bool& watcher_changed) {
  watcher_changed = false;
  if (isDisabled(constraint, constraint_type)) {
    return true;
  }
  /* If both watchers must be updated, it can happen that the first watcher can be updated, but not the second.
     In such a case we may end up with a primary in constraint[0] (which is set to the disabling polarity by propagation)
     and a secondary in constraint[1] that constraint[0] does not depend on. This would not constitute a valid pair
     of watchers after backtracking. To catch this case, we keep track of the index i of the old first watcher, in case
     it is updated. If we only fail to update the second watcher (constraint[1]), i == 1 and swapping constraint[i] with
     constraint[1] has no effect. */
  unsigned int i = 1; 
  if (solver.variable_data_store->isAssigned(var(constraint[0]))) {
    /* First watcher (constraint[0]) must be updated.
       If the second watcher (constraint[1]) is a primary that is assigned or a secondary,
       we _must_ find a new unassigned primary or a disabling literal unless the constraint is empty. */
    if (solver.variable_data_store->varType(var(constraint[1])) != constraint_type || solver.variable_data_store->isAssigned(var(constraint[1]))) {
      for (i = 2; i < constraint.size; i++) {
        if (isUnassignedPrimary(constraint[i], constraint_type)) {
          std::swap(constraint[0], constraint[i]);
          constraints_watched_by[constraint_type][toInt(constraint[0])].emplace_back(constraint_reference, constraint[1]);
          watcher_changed = true;
          break;
        }
      }
      if (!watcher_changed) {
        // Constraint is empty, see above.
        //LOG(trace) << (constraint_type ? "Term " : "Clause ") << "empty: " << solver.variable_data_store->constraintToString(constraint) << std::endl;
        assert(solver.debug_helper->isEmpty(constraint, constraint_type));
        return false;
      }
    } else {
      // If the second watcher is an unassigned primary literal, we swap the first and second watcher.
      std::swap(constraint[0], constraint[1]);
    }
  }
  // The second watcher (constraint[1]) must be updated, and the first watcher is an unassigned primary.
  for (unsigned i = 1; i < constraint.size; i++) {
    if (isUnassignedPrimary(constraint[i], constraint_type)) {
      assert(!solver.debug_helper->isEmpty(constraint, constraint_type) && !solver.debug_helper->isUnit(constraint, constraint_type));
      std::swap(constraint[1], constraint[i]);
      constraints_watched_by[constraint_type][toInt(constraint[1])].emplace_back(constraint_reference, constraint[0]);
      watcher_changed = true;
      return true;
    } else if (isBlockedSecondary(constraint[i], constraint_type, constraint[0])) {
      assert(!solver.debug_helper->isEmpty(constraint, constraint_type) && !solver.debug_helper->isUnit(constraint, constraint_type));
      // Make constraint[i] the second watcher.
      std::swap(constraint[1], constraint[i]);
      constraints_watched_by[constraint_type][toInt(constraint[1])].emplace_back(constraint_reference, constraint[0]);
      watcher_changed = true;
      return true;
    }
  }
  // No new watcher has been found. Try to propagate the first watcher.
  //LOG(trace) << (constraint_type ? "Term " : "Clause ") << (isEmpty(constraint, constraint_type) ? "empty" : "unit") << ": " << solver.variable_data_store->constraintToString(constraint) << std::endl;
  assert(solver.debug_helper->isEmpty(constraint, constraint_type) || solver.debug_helper->isUnit(constraint, constraint_type));
  std::swap(constraint[1], constraint[i]); // See the comment towards the top.
  watcher_changed = false;
  return solver.enqueue(constraint[0] ^ constraint_type, constraint_reference);
}

inline uint32_t WatchedLiteralPropagator::findFirstWatcher(Constraint& constraint, ConstraintType constraint_type) {
  uint32_t i;
  for (i = 0; i < constraint.size; i++) {
    if (isUnassignedOrDisablingPrimary(constraint[i], constraint_type)) {
      break;
    }
  }
  return i;
}

inline uint32_t WatchedLiteralPropagator::findSecondWatcher(Constraint& constraint, ConstraintType constraint_type) {
  uint32_t i;
  for (i = 1; i < constraint.size; i++) {
    if (isUnassignedOrDisablingPrimary(constraint[i], constraint_type) || isBlockedOrDisablingSecondary(constraint[i], constraint_type, constraint[0])) {
      break;
    }
  }
  if (i < constraint.size) {
    return i;
  } else {
    /* No other unassigned or disabling primary or blocked or disabling secondary was found.
       If there are any other assigned primaries or assigned secondaries the first watcher depends on,
       take the one with maximum decision level. */
    uint32_t index_with_highest_decision_level = constraint.size;
    for (i = 1; i < constraint.size; i++) {
      if ((solver.variable_data_store->varType(var(constraint[i])) == constraint_type ||
           solver.dependency_manager->dependsOn(var(constraint[0]), var(constraint[i]))) &&
          solver.variable_data_store->isAssigned(var(constraint[i])) &&
          (index_with_highest_decision_level == constraint.size ||
           solver.variable_data_store->varDecisionLevel(var(constraint[i])) > solver.variable_data_store->varDecisionLevel(var(constraint[index_with_highest_decision_level])))) {
        index_with_highest_decision_level = i;
      }
    }
    return index_with_highest_decision_level; 
  }
}

bool WatchedLiteralPropagator::isUnassignedOrDisablingPrimary(Literal literal, ConstraintType constraint_type) {
  return (solver.variable_data_store->varType(var(literal)) == constraint_type &&
          (!solver.variable_data_store->isAssigned(var(literal)) ||
           (solver.variable_data_store->assignment(var(literal)) == sign(literal)) == disablingPolarity(constraint_type)));
}

bool WatchedLiteralPropagator::isBlockedOrDisablingSecondary(Literal literal, ConstraintType constraint_type, Literal primary) {
  return ((solver.variable_data_store->varType(var(literal)) != constraint_type && solver.dependency_manager->dependsOn(var(primary), var(literal))) &&
          (!solver.variable_data_store->isAssigned(var(literal)) ||
           (solver.variable_data_store->assignment(var(literal)) == sign(literal)) == disablingPolarity(constraint_type) ||
           ((solver.variable_data_store->assignment(var(primary)) == sign(literal)) == disablingPolarity(constraint_type) &&
            solver.variable_data_store->varDecisionLevel(var(primary)) <= solver.variable_data_store->varDecisionLevel(var(literal)))));
}

bool WatchedLiteralPropagator::constraintIsWatchedByLiteral(Constraint& constraint, Literal l) {
  return (l == constraint[0]) || (l == constraint[1]);
}

bool WatchedLiteralPropagator::disablesConstraint(Literal literal, ConstraintType constraint_type) {
  return solver.variable_data_store->isAssigned(var(literal)) && (solver.variable_data_store->assignment(var(literal)) == sign(literal)) == disablingPolarity(constraint_type);
}

bool WatchedLiteralPropagator::isDisabled(Constraint& constraint, ConstraintType constraint_type) {
  for (unsigned i = 0; i < constraint.size; i++) {
    if (disablesConstraint(constraint[i], constraint_type)) {
      return true;
    }
  }
  return false;
}

bool WatchedLiteralPropagator::isUnassignedPrimary(Literal literal, ConstraintType constraint_type) {
  return solver.variable_data_store->varType(var(literal)) == constraint_type && !solver.variable_data_store->isAssigned(var(literal));
}

bool WatchedLiteralPropagator::isBlockedSecondary(Literal literal, ConstraintType constraint_type, Literal primary) {
  return !solver.variable_data_store->isAssigned(var(literal)) && solver.dependency_manager->dependsOn(var(primary), var(literal));
}

bool WatchedLiteralPropagator::propagationCorrect() {
  for (ConstraintType constraint_type: constraint_types) {
    for (unsigned learnt = 0; learnt <= 1; learnt++) {
      for (vector<CRef>::const_iterator constraint_reference_it = solver.constraint_database->constraintReferencesBegin(constraint_type, static_cast<bool>(learnt));
           constraint_reference_it != solver.constraint_database->constraintReferencesEnd(constraint_type, static_cast<bool>(learnt));
           ++constraint_reference_it) {
        Constraint& constraint = solver.constraint_database->getConstraint(*constraint_reference_it, constraint_type);
        if (!constraint.isMarked() && (solver.debug_helper->isUnit(constraint, constraint_type) || solver.debug_helper->isEmpty(constraint, constraint_type))) {
          LOG(error) << (learnt ? "Learnt ": "Input ") << (constraint_type ? "term " : "clause ") << (solver.debug_helper->isEmpty(constraint, constraint_type) ? "empty" : "unit") << ": " << solver.variable_data_store->constraintToString(constraint) << std::endl;
          return false;
        }
      }
    }
  }
  return true;
}

Literal WatchedLiteralPropagator::findDisabling(Constraint& constraint, ConstraintType constraint_type, bool variable_type) {
  for (Literal l: constraint) {
    auto v = var(l);
    if (solver.variable_data_store->varType(v) == variable_type && disablesConstraint(l, constraint_type)) {
      return l;
    }
  }
  return Literal_Undef;
}

}
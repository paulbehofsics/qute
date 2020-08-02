#include <limits>
#include <functional>
#include <csignal>
#include <iostream>
#include <string>

#include "main.hh"
#include "logging.hh"
#include "qcdcl.hh"
#include "parser.hh"
#include "solver_types.hh"
#include "constraint_DB.hh"
#include "decision_heuristic_VMTF_deplearn.hh"
#include "decision_heuristic_VMTF_prefix.hh"
#include "decision_heuristic_VMTF_order.hh"
#include "decision_heuristic_VSIDS_deplearn.hh"
#include "decision_heuristic_SGDB.hh"
#include "decision_heuristic_split_VMTF.hh"
#include "decision_heuristic_split_VSIDS.hh"
#include "decision_heuristic_CQB.hh"
#include "dependency_manager_watched.hh"
#include "restart_scheduler_none.hh"
#include "restart_scheduler_inner_outer.hh"
#include "restart_scheduler_ema.hh"
#include "restart_scheduler_luby.hh"
#include "standard_learning_engine.hh"
#include "variable_data.hh"
#include "watched_literal_propagator.hh"

using namespace Qute;
using namespace std::placeholders;
using std::cerr;
using std::cout;
using std::ifstream;
using std::to_string;
using std::string;

static unique_ptr<QCDCL_solver> solver;

void signal_handler(int signal)
{
  solver->interrupt();
}

static const char USAGE[] =
R"(Usage: qute [options] [<path>]

General Options:
  --initial-clause-DB-size <int>        initial learnt clause DB size [default: 4000]
  --initial-term-DB-size <int>          initial learnt term DB size [default: 500]
  --clause-DB-increment <int>           clause database size increment [default: 4000]
  --term-DB-increment <int>             term database size increment [default: 500]
  --clause-removal-ratio <double>       fraction of clauses removed while cleaning [default: 0.5]
  --term-removal-ratio <double>         fraction of terms removed while cleaning [default: 0.5]
  --use-activity-threshold              remove all constraints with activities below threshold
  --LBD-threshold <int>                 only remove constraints with LBD larger than this [default: 2]
  --constraint-activity-inc <double>    constraint activity increment [default: 1]
  --constraint-activity-decay <double>  constraint activity decay [default: 0.999]
  --decision-heuristic arg              variable decision heuristic [default: VMTF]
                                        (VSIDS | VMTF | VMTF_ORD | SGDB | SPLIT_VMTF | SPLIT_VSIDS | CQB)
  --restarts arg                        restart strategy [default: inner-outer]
                                        (off | luby | inner-outer | EMA)
  --model-generation arg                model generation strategy for initial terms [default: depqbf]
                                        (off | depqbf | weighted)
  --dependency-learning arg             dependency learning strategy
                                        (off | outermost | fewest | all) [default: all]
  --no-phase-saving                     deactivate phase saving
  --phase-heuristic arg                 phase selection heuristic [default: watcher]
                                        (invJW, qtype, watcher, random, false, true) 
  --partial-certificate                 output assignment to outermost block
  -v --verbose                          output information during solver run
  --print-stats                         print statistics on termination

Weighted Model Generation Options:
  --exponent <double>                   exponent skewing the distribution of weights [default: 1]
  --scaling-factor <double>             scaling factor for variable weights [default: 1]
  --universal-penalty <double>          additive penalty for universal variables [default: 0]

VSIDS Options:
  --tiebreak arg                        tiebreaking strategy for equally active variables [default: arbitrary]
                                        (arbitrary, more-primary, fewer-primary, more-secondary, fewer-secondary)
  --var-activity-inc <double>           variable activity increment [default: 1]
  --var-activity-decay <double>         variable activity decay [default: 0.95]

SGDB Options:
  --initial-learning-rate <double>      Initial learning rate [default: 0.8]
  --learning-rate-decay <double>        Learning rate additive decay [default: 2e-6]
  --learning-rate-minimum <double>      Minimum learning rate [default: 0.12]
  --lambda-factor <double>              Regularization parameter [default: 0.1]

Split Heuristic Options:
  --mode-cycles <int>                   The number of restarts after which a mode switch happens [default: 1]
  --split-phase-saving                  Force the heuristic to keep track of saved phases for the decision modes separately
  --start-univ-mode                     Start the heuristic in universal mode instead of existential mode

Split VMTF Options:
  --always-move                         Force the heuristic to move variables for every learnt constraint
  --move-by-prefix                      Move variables sorted by their quantifier depth when learning constraints

Split VSIDS Options:
  --always-bump                         Force the heuristic to bump variable scores for every learnt constraint

Luby Restart Options:
  --luby-restart-multiplier <int>       Multiplier for restart intervals [default: 50]

EMA Restart Options:
  --alpha <double>                      Weight of new constraint LBD [default: 2e-5]
  --minimum-distance <int>              Minimum restart distance [default: 20]
  --threshold-factor <double>           Restart if short term LBD is this much larger than long term LBD [default: 1.4]

Outer-Inner Restart Options:
  --inner-restart-distance <int>        initial number of conflicts until inner restart [default: 100]
  --outer-restart-distance <int>        initial number of conflicts until outer restart [default: 100]
  --restart-multiplier <double>         restart limit multiplier [default: 1.1]

)";

int main(int argc, const char** argv)
{
  std::map<std::string, docopt::value> args = docopt::docopt(USAGE, { argv + 1, argv + argc }, true, "Qute v.1.1");

  /*for (auto arg: args) { // For debugging only.
    std::cout << arg.first << " " << arg.second << "\n";
  }*/

  // BEGIN Command Line Parameter Validation

  vector<unique_ptr<ArgumentConstraint>> argument_constraints;
  regex non_neg_int("[[:digit:]]+");
  argument_constraints.push_back(make_unique<RegexArgumentConstraint>(non_neg_int, "--initial-clause-DB-size", "unsigned int"));
  argument_constraints.push_back(make_unique<RegexArgumentConstraint>(non_neg_int, "--initial-term-DB-size", "unsigned int"));
  argument_constraints.push_back(make_unique<RegexArgumentConstraint>(non_neg_int, "--clause-DB-increment", "unsigned int"));
  argument_constraints.push_back(make_unique<RegexArgumentConstraint>(non_neg_int, "--term-DB-increment", "unsigned int"));

  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, 1, "--clause-removal-ratio"));
  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, 1, "--term-removal-ratio"));

  argument_constraints.push_back(make_unique<DoubleConstraint>("--constraint-activity-inc"));
  // argument_constraints.push_back(make_unique<DoubleConstraint>("--activity-threshold"));
  argument_constraints.push_back(make_unique<RegexArgumentConstraint>(non_neg_int, "--LBD-threshold", "unsigned int"));
  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, 1, "--constraint-activity-decay"));

  vector<string> decision_heuristics = {"VSIDS", "VMTF", "VMTF_ORD", "SGDB", "SPLIT_VMTF", "SPLIT_VSIDS", "CQB"};
  argument_constraints.push_back(make_unique<ListConstraint>(decision_heuristics, "--decision-heuristic"));
  
  vector<string> restart_strategies = {"off", "luby", "inner-outer", "EMA"};
  argument_constraints.push_back(make_unique<ListConstraint>(restart_strategies, "--restarts"));

  vector<string> model_generation_strategies = {"off", "depqbf", "weighted"};
  argument_constraints.push_back(make_unique<ListConstraint>(model_generation_strategies, "--model-generation"));

  vector<string> dependency_learning_strategies = {"off", "outermost", "fewest", "all"};
  argument_constraints.push_back(make_unique<ListConstraint>(dependency_learning_strategies, "--dependency-learning"));

  vector<string> phase_heuristics = {"invJW", "qtype", "watcher", "random", "false", "true"};
  argument_constraints.push_back(make_unique<ListConstraint>(phase_heuristics, "--phase-heuristic"));

  vector<string> VSIDS_tiebreak_strategies = {"arbitrary", "more-primary", "fewer-primary", "more-secondary", "fewer-secondary"};
  argument_constraints.push_back(make_unique<ListConstraint>(VSIDS_tiebreak_strategies, "--tiebreak"));

  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0.5, 2, "--exponent"));
  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, 1, "--scaling-factor"));
  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, 1, "--universal-penalty"));

  argument_constraints.push_back(make_unique<DoubleConstraint>("--var-activity-inc"));
  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, 1, "--var-activity-decay"));

  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, 1, "--initial-learning-rate"));
  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, 1, "--learning-rate-decay"));
  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, 1, "--learning-rate-minimum"));
  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, 1, "--lambda-factor"));

  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(1, std::numeric_limits<double>::infinity(), "--luby-restart-multiplier", false, true));

  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, 1, "--alpha"));
  argument_constraints.push_back(make_unique<RegexArgumentConstraint>(non_neg_int, "--minimum-distance", "unsigned int"));
  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(0, std::numeric_limits<double>::infinity(), "--threshold-factor", false, true));

  argument_constraints.push_back(make_unique<RegexArgumentConstraint>(non_neg_int, "--inner-restart-distance", "unsigned int"));
  argument_constraints.push_back(make_unique<RegexArgumentConstraint>(non_neg_int, "--outer-restart-distance", "unsigned int"));
  argument_constraints.push_back(make_unique<DoubleRangeConstraint>(1, std::numeric_limits<double>::infinity(), "--restart-multiplier", false, true));

  argument_constraints.push_back(make_unique<IfThenConstraint>("--dependency-learning", "off", "--decision-heuristic", "VMTF",
    "decision heuristic must be VMTF if dependency learning is deactivated"));
    
  argument_constraints.push_back(make_unique<RegexArgumentConstraint>(non_neg_int, "--mode-cycles", "unsigned int"));

  for (auto& constraint_ptr: argument_constraints) {
    if (!constraint_ptr->check(args)) {
      std::cout << constraint_ptr->message() << "\n\n";
      std::cout << USAGE;
      return 0;
    }
  }

  // END Command Line Parameter Validation
  solver = make_unique<QCDCL_solver>();

  ConstraintDB constraint_database(*solver,
                                    false,
                                    std::stod(args["--constraint-activity-decay"].asString()), 
                                    static_cast<uint32_t>(args["--initial-clause-DB-size"].asLong()),
                                    static_cast<uint32_t>(args["--initial-term-DB-size"].asLong()),
                                    static_cast<uint32_t>(args["--clause-DB-increment"].asLong()),
                                    static_cast<uint32_t>(args["--term-DB-increment"].asLong()),
                                    std::stod(args["--clause-removal-ratio"].asString()),
                                    std::stod(args["--term-removal-ratio"].asString()),
                                    args["--use-activity-threshold"].asBool(),
                                    std::stod(args["--constraint-activity-inc"].asString()),
                                    static_cast<uint32_t>(args["--LBD-threshold"].asLong())
                                    );
  solver->constraint_database = &constraint_database;
  DebugHelper debug_helper(*solver);
  solver->debug_helper = &debug_helper;
  VariableDataStore variable_data_store(*solver);
  solver->variable_data_store = &variable_data_store;
  DependencyManagerWatched dependency_manager(*solver, args["--dependency-learning"].asString());
  solver->dependency_manager = &dependency_manager;
  unique_ptr<DecisionHeuristic> decision_heuristic;

  if (args["--dependency-learning"].asString() == "off") {
    decision_heuristic = make_unique<DecisionHeuristicVMTFprefix>(*solver, args["--no-phase-saving"].asBool());
  } else if (args["--decision-heuristic"].asString() == "VMTF") {
    decision_heuristic = make_unique<DecisionHeuristicVMTFdeplearn>(*solver, args["--no-phase-saving"].asBool());
  } else if (args["--decision-heuristic"].asString() == "VMTF_ORD") {
    decision_heuristic = make_unique<DecisionHeuristicVMTForder>(*solver, args["--no-phase-saving"].asBool());
  } else if (args["--decision-heuristic"].asString() == "SPLIT_VMTF") {
    decision_heuristic = make_unique<DecisionHeuristicSplitVMTF>(
      *solver, args["--no-phase-saving"].asBool(),
      static_cast<uint32_t>(args["--mode-cycles"].asLong()),
      args["--always-move"].asBool(),
      args["--move-by-prefix"].asBool(),
      args["--split-phase-saving"].asBool(),
      args["--start-univ-mode"].asBool()
    );
  } else if (args["--decision-heuristic"].asString() == "VSIDS" ||
      args["--decision-heuristic"].asString() == "SPLIT_VSIDS") {
    bool tiebreak_scores;
    bool use_secondary_occurrences;
    bool prefer_fewer_occurrences;
    if (args["--tiebreak"].asString() == "arbitrary") {
      tiebreak_scores = false;
    } else if (args["--tiebreak"].asString() == "more-primary") {
      tiebreak_scores = true;
      use_secondary_occurrences = false;
      prefer_fewer_occurrences = false;
    } else if (args["--tiebreak"].asString() == "fewer-primary") {
      tiebreak_scores = true;
      use_secondary_occurrences = false;
      prefer_fewer_occurrences = true;
    } else if (args["--tiebreak"].asString() == "more-secondary") {
      tiebreak_scores = true;
      use_secondary_occurrences = true;
      prefer_fewer_occurrences = false;
    } else if (args["--tiebreak"].asString() == "fewer-secondary") {
      tiebreak_scores = true;
      use_secondary_occurrences = true;
      prefer_fewer_occurrences = true;
    } else {
      assert(false);
    }
    if (args["--decision-heuristic"].asString() == "VSIDS") {
      decision_heuristic = make_unique<DecisionHeuristicVSIDSdeplearn>(*solver,
        args["--no-phase-saving"].asBool(),
        std::stod(args["--var-activity-decay"].asString()),
        std::stod(args["--var-activity-inc"].asString()),
        tiebreak_scores, use_secondary_occurrences, prefer_fewer_occurrences);
    } else if (args["--decision-heuristic"].asString() == "SPLIT_VSIDS") {
      decision_heuristic = make_unique<DecisionHeuristicSplitVSIDS>(*solver,
        args["--no-phase-saving"].asBool(),
        static_cast<uint32_t>(args["--mode-cycles"].asLong()),
        std::stod(args["--var-activity-decay"].asString()),
        std::stod(args["--var-activity-inc"].asString()),
        args["--always-bump"].asBool(),
        args["--split-phase-saving"].asBool(),
        args["--start-univ-mode"].asBool(),
        tiebreak_scores, use_secondary_occurrences, prefer_fewer_occurrences);
    } else {
      assert(false);
    }
  } else if (args["--decision-heuristic"].asString() == "SGDB") {
    decision_heuristic = make_unique<DecisionHeuristicSGDB>(*solver,
                                                    args["--no-phase-saving"].asBool(),
                                                    std::stod(args["--initial-learning-rate"].asString()),
                                                    std::stod(args["--learning-rate-decay"].asString()),
                                                    std::stod(args["--learning-rate-minimum"].asString()),
                                                    std::stod(args["--lambda-factor"].asString()));
  } else if (args["--decision-heuristic"].asString() == "CQB") {
    decision_heuristic = make_unique<DecisionHeuristicCQB>(*solver,
      args["--no-phase-saving"].asBool());
  } else {
    assert(false);
  }
  solver->decision_heuristic = decision_heuristic.get();

  DecisionHeuristic::PhaseHeuristicOption phase_heuristic = DecisionHeuristic::PhaseHeuristicOption::PHFALSE;
  if (args["--phase-heuristic"].asString() == "qtype") {
    phase_heuristic = DecisionHeuristic::PhaseHeuristicOption::QTYPE;
  } else if (args["--phase-heuristic"].asString() == "watcher") {
    phase_heuristic = DecisionHeuristic::PhaseHeuristicOption::WATCHER;
  } else if (args["--phase-heuristic"].asString() == "random") {
    phase_heuristic = DecisionHeuristic::PhaseHeuristicOption::RANDOM;
  } else if (args["--phase-heuristic"].asString() == "false") {
    phase_heuristic = DecisionHeuristic::PhaseHeuristicOption::PHFALSE;
  } else if (args["--phase-heuristic"].asString() == "true") {
    phase_heuristic = DecisionHeuristic::PhaseHeuristicOption::PHTRUE;
  } else if (args["--phase-heuristic"].asString() == "invJW") {
    phase_heuristic = DecisionHeuristic::PhaseHeuristicOption::INVJW;
  } else {
    assert(false);
  }
  decision_heuristic->setPhaseHeuristic(phase_heuristic);

  unique_ptr<RestartScheduler> restart_scheduler;

  if (args["--restarts"].asString() == "off") {
    restart_scheduler = make_unique<RestartSchedulerNone>();
  } else if (args["--restarts"].asString() == "inner-outer") {
    restart_scheduler = make_unique<RestartSchedulerInnerOuter>(
      static_cast<uint32_t>(args["--inner-restart-distance"].asLong()),
      static_cast<uint32_t>(args["--outer-restart-distance"].asLong()),
      std::stod(args["--restart-multiplier"].asString())
    );
  } else if (args["--restarts"].asString() == "luby") {
    restart_scheduler = make_unique<RestartSchedulerLuby>(static_cast<uint32_t>(args["--luby-restart-multiplier"].asLong()));
  } else if (args["--restarts"].asString() == "EMA") {
    restart_scheduler = make_unique<RestartSchedulerEMA>(
      std::stod(args["--alpha"].asString()),
      static_cast<uint32_t>(args["--minimum-distance"].asLong()),
      std::stod(args["--threshold-factor"].asString())
    );
  } else {
    assert(false);
  }

  solver->restart_scheduler = restart_scheduler.get();
  StandardLearningEngine learning_engine(*solver);
  solver->learning_engine = &learning_engine;
  WatchedLiteralPropagator propagator(
    *solver, 
    args["--model-generation"].asString() == "weighted",
    std::stod(args["--exponent"].asString()),
    std::stod(args["--scaling-factor"].asString()),
    std::stod(args["--universal-penalty"].asString())
  );
  
  solver->propagator = &propagator;

  Parser parser(*solver, args["--model-generation"].asString() != "off");

  // PARSER
  if (args["<path>"]) {
    string filename = args["<path>"].asString();
  	ifstream ifs(filename);
    if (!ifs.is_open()) {
      cerr << "qute: cannot access '" << filename << "': no such file or directory \n";
      return 2;
    } else {
      parser.readAUTO(ifs);
      ifs.close();
    }
  }
  else {
    parser.readAUTO();
  }

  // LOGGING
  if (args["--verbose"].asBool()) {
    Logger::get().setOutputLevel(Loglevel::info);
  }

  // Register signal handler
  std::signal(SIGTERM, signal_handler);
  std::signal(SIGINT, signal_handler);

  lbool result = solver->solve();

  if (args["--partial-certificate"].asBool() && ((result == l_True && !solver->variable_data_store->varType(1)) ||
                                                 (result == l_False && solver->variable_data_store->varType(1)))) {
    cout << learning_engine.reducedLast() << "\n";
  }

  if (args["--print-stats"].asBool()) {
    solver->printStatistics();
  }

  if (result == l_True) {
    cout << "SAT\n";
    return 10;
  } else if (result == l_False) {
    cout << "UNSAT\n";
    return 20;
  } else {
    cout << "UNDEF\n";
    return 0;
  }
}
#include <chrono>
#include <torch/torch.h>

#include "comm.h"
#include "comm2.h"
#include "cxxopts/include/cxxopts.hpp"
#include "kuhn.h"
#include "simple_bidding.h"
#include "two_suited_bridge.h"

#include "cfr_opt.h"
#include "search.h"

using namespace std::chrono;

int main(int argc, char* argv[]) {
  cxxopts::Options cmdOptions(
      "Tabular Game Solver", "Simple Game Solver for Tabular Games");

  cmdOptions.add_options()
    ("g,game", "Game Name", cxxopts::value<std::string>()->default_value("comm"))
    ("method", "Name of method", cxxopts::value<std::string>()->default_value("search"))
    ("load_pi", "Load policy", cxxopts::value<std::string>())
    ("num_round", "#round for comm", cxxopts::value<int>()->default_value("4"))
    ("num_card", "#card for comm", cxxopts::value<int>()->default_value("-1"))
    ("seed", "Random Seed", cxxopts::value<int>()->default_value("1"))
    ("first_random_infoset", "Random Seed", cxxopts::value<std::string>()->default_value(""))
    ("gt_compute", "Compute exhaustive search", cxxopts::value<bool>()->default_value("false"))
    ("gt_override", "Override research with gt result", cxxopts::value<bool>()->default_value("false"))
    ("perturb_chance", "Whether we purturb chance node", cxxopts::value<float>()->default_value("0.0"))
    ("perturb_policy", "Whether we purturb policy", cxxopts::value<float>()->default_value("0.0"))
    ("verbose", "Verbose", cxxopts::value<bool>()->default_value("false"))
    ("compute_reach", "Whether we compute reach", cxxopts::value<bool>()->default_value("false"))
    ("no_opt", "Not compare with optimal strategy", cxxopts::value<bool>()->default_value("false"))
    ("iter", "#Iteration", cxxopts::value<int>()->default_value("100"))
    ("iter_cfr", "#Iteration for cfr", cxxopts::value<int>()->default_value("1000"))
    ("no_cfr_init", "Do not use CFR for initialization", cxxopts::value<bool>()->default_value("false"))
    ("show_better", "whether show better policy when there is improvement", cxxopts::value<bool>()->default_value("false"))
    ("N", "N in MiniBridge", cxxopts::value<int>()->default_value("3"))
    ("use_2nd_order", "", cxxopts::value<bool>()->default_value("false"))
    ("max_depth", "Max optimization depth (0 mean till the end)", cxxopts::value<int>()->default_value("0"))
    ("skip_single_infoset_opt", "", cxxopts::value<bool>()->default_value("false"))
    ("skip_same_delta_policy", "", cxxopts::value<bool>()->default_value("false"))
    ("num_samples", "#samples used in each iteration, 0 = use all", cxxopts::value<int>()->default_value("0"));

  auto result = cmdOptions.parse(argc, argv);

  std::string gameName = result["game"].as<std::string>();

  tabular::Options options;
  options.seed = result["seed"].as<int>();
  options.method = result["method"].as<std::string>();
  options.verbose = result["verbose"].as<bool>();
  options.perturbChance = result["perturb_chance"].as<float>();
  options.perturbPolicy = result["perturb_policy"].as<float>();
  options.firstRandomInfoSetKey =
      result["first_random_infoset"].as<std::string>();
  options.gtCompute = result["gt_compute"].as<bool>();
  options.gtOverride = result["gt_override"].as<bool>();
  options.computeReach = result["compute_reach"].as<bool>();
  options.use2ndOrder = result["use_2nd_order"].as<bool>();
  options.maxDepth = result["max_depth"].as<int>();
  options.showBetter = result["show_better"].as<bool>();

  options.skipSingleInfoSetOpt = result["skip_single_infoset_opt"].as<bool>();
  options.skipSameDeltaPolicy = result["skip_same_delta_policy"].as<bool>();

  int numIter = result["iter"].as<int>();
  int numIterCFR = result["iter_cfr"].as<int>();
  int numSamples = result["num_samples"].as<int>();
  bool noCFRInit = result["no_cfr_init"].as<bool>();

  simple::CommOptions gameOptions;
  gameOptions.numRound = result["num_round"].as<int>();
  gameOptions.possibleCards = result["num_card"].as<int>();
  gameOptions.N = result["N"].as<int>();

  std::string method = result["method"].as<std::string>();

  auto start = high_resolution_clock::now();

  std::unique_ptr<rela::Env> game;
  std::unique_ptr<rela::OptimalStrategy> strategy;

  if (gameName == "kuhn") {
    game = std::make_unique<simple::KuhnPoker>();
  } else if (gameName == "comm") {
    game = std::make_unique<simple::Communicate>(gameOptions);
    strategy = std::make_unique<simple::CommunicatePolicy>(gameOptions);
  } else if (gameName == "comm2") {
    game = std::make_unique<simple::Communicate2>(gameOptions);
    strategy = std::make_unique<simple::Communicate2Policy>();
  } else if (gameName == "simplebidding") {
    game = std::make_unique<simple::SimpleBidding>(gameOptions);
  } else if (gameName == "2suitedbridge") {
    game = std::make_unique<simple::TwoSuitedBridge>(gameOptions);
  } else {
    throw std::runtime_error(gameName + " is not implemented");
  }

  game->reset();

  /*
  tabular_cfr::CFR cfr(options);
  std::cout << "Initialize search tree.. " << std::endl;
  cfr.init(*game);
  std::cout << "Initialize done. #infoSet: " << cfr.infoSets().numInfoSets()
            << ", #node: " << cfr.infoSets().numNodes() << std::endl;
  cfr.infoSets().printInfoSetTree();
  */

  tabular::Policies policies;
  std::vector<float> v;
  std::vector<float> vPure;

  if (!noCFRInit) {
    tabular::cfr::CFRSolver cfrSolver(options.seed, options.verbose);
    std::cout << "Initialize CFR search tree and run it for " << numIterCFR << " iterations." << std::endl;
    cfrSolver.init(*game);
    v = cfrSolver.run(numIterCFR);

    std::cout << "Result after CFR " << numIterCFR
              << " iterations with seed: " << options.seed << std::endl;
    for (int i = 1; i < (int)v.size(); ++i) {
      std::cout << "CFR Player " << i << " expected value: " << v[i] << std::endl;
    }

    policies = cfrSolver.getInfos().getStrategies();
    // Also get the value after purification.
    cfrSolver.getInfos().purifyStrategies();
    vPure = cfrSolver.evaluate();
  }

  if (options.method == "cfr") {
    std::cout << "CFR / CFR pure: " << v[1] << " " << vPure[1] << std::endl;
    return 0;
  }

  tabular::search::Solver solver(options);
  std::cout << "Initialize search tree.. " << std::endl;
  solver.init(*game);
  std::cout << "Initialize done. #infoSet: " << solver.manager().numInfoSets()
            << ", #states: " << solver.manager().numStates() << std::endl;
  if (options.verbose) {
    solver.manager().printInfoSetTree();
  }

  if (result["load_pi"].count()) {
    auto filename = result["load_pi"].as<std::string>();
    solver.loadPolicies(filename);
  } else {
    if (noCFRInit) {
      solver.manager().randomizePolicy();
    } else {
      solver.loadPolicies(policies);
    }
  }

  auto vSearch = solver.runSearch(1, numSamples, numIter);

  std::cout << "Final result after " << numIter
            << " iterations with seed: " << options.seed << std::endl;
  for (int i = 1; i < (int)vSearch.size(); ++i) {
    std::cout << "Search Player " << i << " expected value: " << vSearch[i] << std::endl;
  }

  if (!noCFRInit) {
    std::cout << "CFR / CFR pure / Search: " << v[1] << " " << vPure[1] << " " << vSearch[1] << std::endl;
  }

  solver.manager().printStrategy();

  /*
  std::cout << "Improving strategy with joint search: " << std::endl;
  std::cout << solver.enumPolicies(1);
  */

  auto stop = high_resolution_clock::now();
  std::cout << "Time spent: "
            << duration_cast<microseconds>(stop - start).count() / 1e6 << "s"
            << std::endl;

  if (options.verbose) {
    std::cout << solver.printTree() << std::endl;
  }
  //
  if (strategy != nullptr && !result["no_opt"].as<bool>()) {
    auto strategies = [&](const std::string& key) {
      return strategy->getOptimalStrategy(key);
    };
    std::cout << "Optimal strategy: " << std::endl;
    solver.manager().setStrategies(strategies);

    std::cout << "Evaluating.. " << std::endl;
    solver.evaluate();
    const auto& v = solver.root()->u();
    for (int i = 1; i < (int)v.size(); ++i) {
      std::cout << "Player " << i << " optimal value: " << v[i] << std::endl;
    }
  }

  return 0;
}
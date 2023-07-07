#include <iostream>
#include <csignal>
#include <cstring>

#include "mapping/fused-mapping.hpp"
#include "workload/fused-workload.hpp"
#include "util/args.hpp"

extern bool gTerminateEval;

using problem::EinsumId;
using problem::DimensionId;

struct EinsumSet
{
  void AddEinsum(EinsumId einsum)
  {
    einsums_.emplace(einsum);
    hash_ ^= einsum;
  }

  void RemoveEinsum(EinsumId einsum)
  {
    einsums_.erase(einsum);
    hash_ ^= einsum;
  }

  inline size_t GetHash() const
  {
    return hash_;
  }

 private:
  std::set<EinsumId> einsums_;
  size_t hash_;
};

template<>
struct std::hash<EinsumSet>
{
  size_t operator()(const EinsumSet& set) const
  {
    return set.GetHash();
  }
};

struct EinsumDimGraph
{
  std::set<DimensionId>& TilableDimensions(const std::set<EinsumId>& einsums);
};

struct WorkloadGraph
{
  std::set<EinsumId> NextEinsums(const std::set<EinsumId>& cur_einsums);
};

template<typename T>
struct Memo
{
  std::optional<std::reference_wrapper<T>>
  GetMemoizedValue(const EinsumSet& einsum_set)
  {
    auto it = memo_.find(einsum_set);
    if (it == memo_.end())
    {
      return std::nullopt;
    }
    else
    {
      return std::ref(it->second);
    }
  }

  void Memoize(const EinsumSet& einsum_set, const T& val)
  {
    memo_.emplace(einsum_set, val);
  }

 private:
  std::unordered_map<EinsumSet, T> memo_;
};

struct MapperResult;

struct Mapper
{
  const MapperResult&
  Run(const EinsumSet& cur_fused_set, const EinsumSet& rest_of_einsums)
  {
    auto memoized_val_opt = memo_.GetMemoizedValue(rest_of_einsums);
    if (memoized_val_opt)
    {
      return *memoized_val_opt;
    }

    auto dfs_stack = std::vector<EinsumId>();
    while (dfs_stack.size() > 0)
    {
      auto e = dfs_stack.back();
      dfs_stack.pop_back();

      cur_fused_set.AddEinsum(e);
    }
  }

 private:
  Memo<MapperResult> memo_;
};

void
Mapper(std::set<EinsumId> cur_fused_set, std::set<EinsumId> rest_of_einsums)
{
  if (Memoized(rest_of_einsums))
  {
    return Memoized(rest_of_einsums);
  }

  // DFS to go through all possible fused sets
  auto dfs_stack;
  while (dfs_stack.size() > 0)
  {
    auto e = dfs_stack.back().first;
    auto cur_fused_set = dfs_stack.back().second;
    auto new_rest_of_einsums = dfs_stack.back().third;

    // If we stop here
    auto cur_pareto = ExploreTilingAndReuseLevel(cur_fused_set);
    auto rest_pareto = Mapper(new_rest_of_einsums);
    auto pareto = CombinePareto(cur_pareto, rest_pareto);
    Memoize(rest_of_einsums);

    auto next_einsums = workload_graph.NextEinsums(cur_fused_set);
    for (auto e : next_einsums)
    {
      dfs_stack.emplace_back(e, cur_fused_set);
    }
  }
}

void handler(int s)
{
  if (!gTerminateEval)
  {
    std::cerr << "First " << strsignal(s) << " caught. Abandoning "
              << "ongoing evaluation and terminating immediately."
              << std::endl;
    gTerminateEval = true;
  }
  else
  {
    std::cerr << "Second " << strsignal(s) << " caught. Existing disgracefully."
              << std::endl;
    exit(0);
  }
}


int main(int argc, char* argv[])
{
  assert(argc >= 2);

  struct sigaction action;
  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  sigaction(SIGINT, &action, NULL);
  
  std::vector<std::string> input_files;
  std::string output_dir = ".";
  bool success = ParseArgs(argc, argv, input_files, output_dir);
  if (!success)
  {
    std::cerr << "ERROR: error parsing command line." << std::endl;
    exit(1);
  }

  auto config = config::CompoundConfig(input_files);
  auto root = config.getRoot();

  auto workload = problem::ParseFusedWorkload(root.lookup("problem"));

  std::cout << EnumerateMappings(workload, 3) << std::endl;

  return 0;
}
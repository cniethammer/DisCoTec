#ifndef PROCESSGROUPWORKER_HPP_
#define PROCESSGROUPWORKER_HPP_

#include <chrono>
#include "sgpp/distributedcombigrid/fullgrid/FullGrid.hpp"
#include "sgpp/distributedcombigrid/manager/CombiParameters.hpp"
#include "sgpp/distributedcombigrid/manager/ProcessGroupSignals.hpp"
#include "sgpp/distributedcombigrid/mpi/MPISystem.hpp"
#include "sgpp/distributedcombigrid/mpi_fault_simulator/MPI-FT.h"
#include "sgpp/distributedcombigrid/task/Task.hpp"
#include "sgpp/distributedcombigrid/loadmodel/LearningLoadModel.hpp"

namespace combigrid {

class ProcessGroupWorker {
 public:
  explicit ProcessGroupWorker();

  ProcessGroupWorker(ProcessGroupWorker const&) = delete;

  ProcessGroupWorker& operator=(ProcessGroupWorker const&) = delete;

  ~ProcessGroupWorker();

  // wait for command from manager
  SignalType wait();

  // send ready signal to manager
  void ready();

  // decides if current Task needs to be killed
  void decideToKill();

  // todo: maybe only needed for gene?
  inline Task* getCurrentTask();

  // getter for tasks
  inline const TaskContainer& getTasks() const;

  // Perform combination
  void combine();

  /** initializes all subspace sizes in the dsgu according to the dfgs in the
   * global reduce comm*/
  void initCombinedUniDSGVector();

  /** hierarchizes all fgs */
  void hierarchizeFullGrids();

  /** local reduce */
  void addFullGridsToUniformSG();

  /** extracts and dehierarchizes */
  void integrateCombinedSolution();

  /** reduction */
  void reduceUniformSG();

  // combine on sparse grid with uniform decomposition of domain
  void combineUniform();

  void combineLocalAndGlobal();

  // outdated!
  void combineFG();

  void deleteTasks();

  void gridEval();

  // parallel file io of final output grid
  void parallelEval();

  // parallel file io of final output grid for uniform decomposition
  void parallelEvalUniform();

  /** send back the Lp Norm to Manager */
  void sendLpNorms(int p);

  /** evaluate norms on (newly created) reference grid */
  void parallelEvalNorm();

  /** evaluate norms of Task's analytical solution on reference grid */
  void evalAnalyticalOnDFG();

  /** evaluate norms of combi solution error on reference grid  */
  void evalErrorOnDFG();

  /** interpolate values on all tasks' component grids  */
  std::vector<CombiDataType> interpolateValues();

  /** update combination parameters (for init or after change in FTCT) */
  void updateCombiParameters();

  // returns the combi parameters
  inline CombiParameters& getCombiParameters();

  // initializes the component grid from the sparse grid; used to reinitialize tasks after fault
  void setCombinedSolutionUniform(Task* t);

 private:
  TaskContainer tasks_;  // task storage

  Task* currentTask_;  // task that is currently processed

  StatusType status_;  // current status of process group (wait -> 0; busy -> 1; fail -> 2)

  /**
   * Vector containing all distributed sparse grids
   */
  std::vector<std::unique_ptr<DistributedSparseGridUniform<CombiDataType>>> combinedUniDSGVector_;

  bool combinedFGexists_;

  CombiParameters combiParameters_;

  bool combiParametersSet_;  // indicates if combi parameters variable set

  // fault parameters
  real t_fault_;  // time to fault

  IndexType currentCombi_;  // current combination; increased after every combination

  std::chrono::high_resolution_clock::time_point
      startTimeIteration_;  // starting time of process computation

  // std::ofstream betasFile_;

  void initializeTaskAndFaults(bool mayAlreadyExist = true);

  /** sets all subspaces in all dsgs to zero and allocates them if necessary */
  void zeroDsgsData();

  /** deallocates all data elements stored in the dsgs */
  void deleteDsgsData();

  void processDuration(const Task& t, const Stats::Event e, unsigned int numProcs);

  void updateTaskWithCurrentValues(Task& taskToUpdate, size_t numGrids);

  /** helper functions for parallelEval and norm calculations*/
  LevelVector receiveLevalAndBroadcast();
  void fillDFGFromDSGU(DistributedFullGrid<CombiDataType>& dfg, IndexType g = 0);
};

inline Task* ProcessGroupWorker::getCurrentTask() {
  assert(currentTask_ != nullptr);
  return currentTask_;
}

inline CombiParameters& ProcessGroupWorker::getCombiParameters() {
  assert(combiParametersSet_);

  return combiParameters_;
}

inline const TaskContainer& ProcessGroupWorker::getTasks() const {
  return tasks_;
}

} /* namespace combigrid */

#endif /* PROCESSGROUPWORKER_HPP_ */

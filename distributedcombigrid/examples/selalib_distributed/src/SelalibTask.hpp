/*
 * SelalibTask.hpp
 *
 *  Created on: Nov 19, 2020
 *      Author: obersteiner
 */

#ifndef SELALIBTASK_HPP_
#define SELALIBTASK_HPP_

#include <stddef.h>

#include <cassert>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "sgpp/distributedcombigrid/fault_tolerance/FTUtils.hpp"
#include "sgpp/distributedcombigrid/fullgrid/DistributedFullGrid.hpp"
#include "sgpp/distributedcombigrid/task/Task.hpp"
#include "sgpp/distributedcombigrid/utils/IndexVector.hpp"
#include "sgpp/distributedcombigrid/utils/LevelVector.hpp"
#include "sgpp/distributedcombigrid/utils/Types.hpp"

extern "C" {
//  void __sll_m_collective_MOD_sll_s_boot_collective(int32_t *mpi_mode);
void sll_s_allocate_collective();
void sll_s_set_communicator_collective(MPI_Fint* mpi_comm);
void sll_s_halt_collective();

void sim_bsl_vp_3d3v_cart_dd_slim_movingB_init(const char* filename);
void sim_bsl_vp_3d3v_cart_dd_slim_movingB_run();
void sim_bsl_vp_3d3v_cart_dd_slim_movingB_delete();
void sim_bsl_vp_3d3v_cart_dd_slim_movingB_get_distribution(void* cPtr);
void sim_bsl_vp_3d3v_cart_dd_slim_movingB_set_distribution(void* cPtr);
void sim_bsl_vp_3d3v_cart_dd_slim_movingB_get_local_size(int32_t* cPtr);
void sim_bsl_vp_3d3v_cart_dd_slim_movingB_advect_v(double* delta_t);
void sim_bsl_vp_3d3v_cart_dd_slim_movingB_advect_x(double* delta_t);
}

namespace combigrid {

class SelalibTask : public combigrid::Task {
 public:
  SelalibTask(DimType dim, LevelVector& l, std::vector<bool>& boundary, real coeff,
              LoadModel* loadModel, std::string& path, real dt, real combitime, size_t nsteps,
              IndexVector p = IndexVector(0),
              FaultCriterion* faultCrit = (new StaticFaults({0, IndexVector(0), IndexVector(0)}))) {
    assert("not yet implemented");
  }

  SelalibTask() { assert("not yet implemented"); }

  virtual ~SelalibTask() { assert("not yet implemented"); }
  /**
   * lcomm is the local communicator of the process group.
   */
  void run(CommunicatorType lcomm) { assert("not yet implemented"); }

  /**
   * This method changes the folder to the folder of the task
   * lcomm is the local communicator of the process group.
   */
  void changeDir(CommunicatorType lcomm) { assert("not yet implemented"); }

  /**
   * This method initializes the task
   * lcomm is the local communicator of the process group.
   * decomposition is the spatial decomposition of the component grid
   */
  void init(CommunicatorType lcomm,
            std::vector<IndexVector> decomposition = std::vector<IndexVector>()) {
    assert("not yet implemented");
  }

  /**
   * This method returns the decomposition of the grid of the specified species
   */
  std::vector<IndexVector> getDecomposition(int species) { return dfg_->getDecomposition(); }

  /**
   * This method is used to decide if the execution of the task should fail
   */
  void decideToKill() { assert("not yet implemented"); }

  /**
   * Returns the path of the task
   */
  inline const std::string& getPath() const { assert("not yet implemented"); }

  /**
   * This method writes the Selalib grid to the local checkpoint
   */
  void writeLocalDistribution(double* data) { assert("not yet implemented"); }

  /**
   * Returns the distributed full grid of the specified specie
   */
  DistributedFullGrid<CombiDataType>& getDistributedFullGrid(int specie) { return *dfg_; }

  /**
   * sets the dfg content to zero
   */
  void setZero() { assert("not yet implemented"); }

  /**
   * Sets the total number of timesteps computed so far. Used in case of restart of component grids
   * during fault recovery. Only valid if combitime is not used
   */
  inline void setStepsTotal(size_t stepsTotal) { stepsTotal_ = stepsTotal; }
  /**
   * Sets the current combination step
   */
  inline void setCombiStep(int ncombi) { combiStep_ = ncombi; }
  /**
   * Returns the current combination step
   */
  inline int getCombiStep() { return combiStep_; }
  /**
   * Return boolean to indicate whether SelalibTask is initialized.
   */
  inline bool isInitialized() { return initialized_; }

  /**
   * Returns the time that is simulated between combinations.
   * This is only used in case we do not want to use a fixed number of timesteps
   * but a fixed period of time between combinations for each component grids.
   */
  real getCombiTime() { return combitime_; }

  /**
   * Returns the current time in the simulation. This is uses to update the time in BSL after
   * restart.
   */
  real getCurrentTime() const override { return currentTime_; }

  /**
   * Sets the current time in the simulation. This is uses to update the time in BSL after restart.
   */
  void setCurrentTime(real currentTime) { currentTime_ = currentTime; }
  /**
   * Returns the current timestep in the simulation. This is uses to update the timestep in BSL
   * after restart.
   */
  real getCurrentTimestep() { return currentTimestep_; }
  /**
   * Sets the current timestep in the simulation. This is uses to update the timestep in BSL after
   * restart.
   */
  void setCurrentTimestep(real currentTimestep) { currentTimestep_ = currentTimestep; }

 private:
  friend class boost::serialization::access;

  // following variables are set in manager and thus need to be included in
  // serialize function
  std::string path_;  // directory in which task should be executed
  IndexVector p_;

  // following variables are only accessed in worker and do not need to be
  // serialized
  std::array<int32_t, 6> localSize_;
  double* localDistribution_;
  DistributedFullGrid<CombiDataType>* dfg_;


  bool initialized_;  // indicates if SelalibTask is initialized

  /*
   * simulation time specific parameters
   */
  real currentTime_;      // current time in the simulation
  real currentTimestep_;  // curent time step length in the simulation
  real dt_;
  real combitime_;  // simulation time interval between combinations
  /**number of time-steps in between two combinations (is set very large in case combitime should be
   * used); this requires equal time-steps for every component grid
   */
  size_t nsteps_;
  size_t stepsTotal_;  // number of time-steps done so far (there might be multiple timesteps in
                       // between two combinations)
  size_t combiStep_;   // number of combinations done so far

  // std::chrono::high_resolution_clock::time_point  startTimeIteration_;

  // serialize
  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& boost::serialization::base_object<Task>(*this);
    ar& path_;
    ar& dt_;
    ar& combitime_;
    ar& nsteps_;
    ar& stepsTotal_;
    ar& combiStep_;
    // ar& localDistribution_;
    ar& p_;
    ar& currentTime_;
  }
};

inline const std::string& SelalibTask::getPath() const { return path_; }

inline std::ostream& operator<<(std::ostream& os, const SelalibTask& t) {
  os << "SelalibTask:\n"
     << "\t LevelVector = " << t.getLevelVector() << "\n"
     << "\t Path = " << t.getPath();

  return os;
}

inline void SelalibTask::setStepsTotal(size_t stepsTotal) { stepsTotal_ = stepsTotal; }

inline double* SelalibTask::getLocalDistribution() { return localDistribution_; }

inline void SelalibTask::setCombiStep(int ncombi) { combiStep_ = ncombi; }

} /* namespace combigrid */

#endif /* SELALIBTASK_HPP_ */

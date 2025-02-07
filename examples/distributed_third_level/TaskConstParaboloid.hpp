#ifndef TASKCONSTPARABOLOID_HPP_
#define TASKCONSTPARABOLOID_HPP_

#define BOOST_TEST_DYN_LINK

#include <boost/serialization/export.hpp>
#include "task/Task.hpp"

template <typename FG_ELEMENT>
class ParaboloidFn {
  public:
    FG_ELEMENT operator()(std::vector<double>& coords) {
      size_t dim = coords.size();
      FG_ELEMENT sign;
      (dim%2) ? sign = 1. : sign = -1.;
      FG_ELEMENT result(sign);
      for (size_t d = 0; d < dim; ++d) {
        result *= coords[d] * (coords[d] - 1.);
      }
      return result;
    }
};

/* simple task class to set all values on the grid to $levelVector_1 / levelVector_2$
 */
class TaskConstParaboloid : public combigrid::Task {
 public:
  TaskConstParaboloid(LevelVector& l, std::vector<bool>& boundary, real coeff,
                      std::unique_ptr<LoadModel>& loadModel)
      : Task(l, boundary, coeff, loadModel.get()) {
      assert(l.size() == 2);
  }

  void init(CommunicatorType lcomm, std::vector<IndexVector> decomposition) {
    // parallelization
    // assert(dfg_ == nullptr);
    auto nprocs = getCommSize(lcomm);
    std::vector<int> p = {nprocs,1};

    // decomposition = std::vector<IndexVector>(2);
    // size_t l1 = getLevelVector()[1];
    // size_t npoint_x1 = pow(2, l1) + 1;

    // decomposition[0].push_back(0);
    // decomposition[1].push_back(0);

    // // std::cout << "decomposition" << std::endl;
    // for (int r = 1; r < nprocs_; ++r) {
    //   decomposition[1].push_back(r * (npoint_x1 / nprocs_));

    //   // std::cout << decomposition[1].back() << std::endl;
    // }

    dfg_ = new DistributedFullGrid<CombiDataType>(getDim(), getLevelVector(), lcomm, getBoundary(),
                                                  p, false, decomposition);

    // set paraboloid function values
    ParaboloidFn<CombiDataType> f;
    for (IndexType li = 0; li < dfg_->getNrLocalElements(); ++li) {
      std::vector<double> coords(getDim());
      dfg_->getCoordsLocal(li, coords);
      dfg_->getData()[li] = f(coords);
    }
  }

  void run(CommunicatorType lcomm) {
    // constant run method
    setFinished(true);
    MPI_Barrier(lcomm);
  }

  void getFullGrid(FullGrid<CombiDataType>& fg, RankType r, CommunicatorType lcomm, int n = 0) {
    dfg_->gatherFullGrid(fg, r);
  }

  DistributedFullGrid<CombiDataType>& getDistributedFullGrid(int n = 0) { return *dfg_; }

  void setZero() {  }

  ~TaskConstParaboloid() {
    if (dfg_ != NULL) delete dfg_;
  }

 protected:
  TaskConstParaboloid() : dfg_(NULL) {}

 private:
  friend class boost::serialization::access;

  DistributedFullGrid<CombiDataType>* dfg_;

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& boost::serialization::base_object<Task>(*this);
    // ar& nprocs_;
  }
};

#endif // def TASKCONSTPARABOLOID_HPP_    BOOST_CHECK(true);

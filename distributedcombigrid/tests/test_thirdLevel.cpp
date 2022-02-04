#define BOOST_TEST_DYN_LINK
// to resolve https://github.com/open-mpi/ompi/issues/5157
#define OMPI_SKIP_MPICXX 1
#include <mpi.h>

#include <boost/serialization/export.hpp>
#include <boost/test/unit_test.hpp>
#include <cstdio>

#include "TaskConstParaboloid.hpp"
#include "TaskCount.hpp"
#include "sgpp/distributedcombigrid/combischeme/CombiMinMaxScheme.hpp"
#include "sgpp/distributedcombigrid/combischeme/CombiThirdLevelScheme.hpp"
#include "sgpp/distributedcombigrid/loadmodel/LearningLoadModel.hpp"
#include "sgpp/distributedcombigrid/loadmodel/LinearLoadModel.hpp"
#include "sgpp/distributedcombigrid/manager/CombiParameters.hpp"
#include "sgpp/distributedcombigrid/manager/ProcessGroupManager.hpp"
#include "sgpp/distributedcombigrid/manager/ProcessGroupWorker.hpp"
#include "sgpp/distributedcombigrid/manager/ProcessManager.hpp"
#include "sgpp/distributedcombigrid/sparsegrid/DistributedSparseGridUniform.hpp"
#include "sgpp/distributedcombigrid/task/Task.hpp"
#include "sgpp/distributedcombigrid/utils/Config.hpp"
#include "sgpp/distributedcombigrid/utils/MonteCarlo.hpp"
#include "sgpp/distributedcombigrid/utils/Types.hpp"
#include "stdlib.h"
#include "test_helper.hpp"

using namespace combigrid;

BOOST_CLASS_EXPORT(TaskConstParaboloid)
BOOST_CLASS_EXPORT(TaskCount)

class TestParams {
 public:
  DimType dim = 2;
  LevelVector lmin;
  LevelVector lmax;
  bool boundary;
  unsigned int ngroup = 1;
  unsigned int nprocs = 1;
  unsigned int ncombi = 1;
  unsigned int sysNum = 0;
  const CommunicatorType& comm;
  std::string host = "localhost";
  unsigned short port = 9999;

  TestParams(DimType dim, LevelVector& lmin, LevelVector& lmax, bool boundary, unsigned int ngroup,
             unsigned int nprocs, unsigned int ncombi, unsigned int sysNum,
             const CommunicatorType& comm, const std::string& host = "localhost",
             unsigned short dataPort = 9999)
      : dim(dim),
        lmin(lmin),
        lmax(lmax),
        boundary(boundary),
        ngroup(ngroup),
        nprocs(nprocs),
        ncombi(ncombi),
        sysNum(sysNum),
        comm(comm),
        host(host),
        port(dataPort) {}
};

/**
 * Checks if combination was successful.
 * Since the tasks don't evolve over time the expected result should match the
 * initial function values.
 */
bool checkReducedFullGrid(ProcessGroupWorker& worker, int nrun) {
  TaskContainer& tasks = worker.getTasks();
  int numGrids = (int)worker.getCombiParameters().getNumGrids();

  BOOST_CHECK(tasks.size() > 0);
  BOOST_CHECK(numGrids > 0);

  // to check if any data was actually compared
  bool any = false;

  for (Task* t : tasks) {
    for (int g = 0; g < numGrids; g++) {
      DistributedFullGrid<CombiDataType>& dfg = t->getDistributedFullGrid(g);
      // dfg.print(std::cout);
      // std::cout << std::endl;
      // TestFnCount<CombiDataType> initialFunction;
      ParaboloidFn<CombiDataType> initialFunction;
      for (IndexType li = 0; li < dfg.getNrLocalElements(); ++li) {
        std::vector<double> coords(dfg.getDimension());
        dfg.getCoordsLocal(li, coords);
        // CombiDataType expected = initialFunction(coords, nrun);
        CombiDataType expected = initialFunction(coords);
        CombiDataType occuring = dfg.getData()[li];
        BOOST_CHECK_CLOSE(expected, occuring, TestHelper::tolerance);
        // BOOST_REQUIRE_CLOSE(expected, occuring, TestHelper::tolerance); //TODO use this once
        // debugging is finished
        any = true;
      }
    }
  }
  BOOST_CHECK(any);
  return any;
}

void assignProcsToSystems(unsigned int ngroup, unsigned int nprocs, unsigned int numSystems,
                          unsigned int& sysNum, CommunicatorType& newcomm) {
  int rank, size, color, key;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  unsigned int procsPerSys = ngroup * nprocs + 1;

  BOOST_REQUIRE(TestHelper::checkNumMPIProcsAvailable(int(numSystems * procsPerSys) + 1));

  // assign procs to systems
  sysNum = unsigned(rank) / procsPerSys;
  color = int(sysNum);
  key = rank % (int)procsPerSys;

  MPI_Comm_split(MPI_COMM_WORLD, color, key, &newcomm);
}

/** Runs the third level manager in the background as a forked child process */
void runThirdLevelManager() {
  std::cout << "starting thirdLevelManager..." << std::endl;
  std::string command = "../../distributedcombigrid/third_level_manager/run.sh &";
  system(command.c_str());
}

/** Runs the tl manager*/
void startInfrastructure() {
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == 0) {
    runThirdLevelManager();
  }
  // give infrastructure some time to set up
  sleep(5);
}

void testCombineThirdLevel(TestParams& testParams) {
  BOOST_CHECK(testParams.comm != MPI_COMM_NULL);

  size_t procsPerSys = testParams.ngroup * testParams.nprocs + 1;

  combigrid::Stats::initialize();

  theMPISystem()->initWorldReusable(testParams.comm, testParams.ngroup, testParams.nprocs);

  WORLD_MANAGER_EXCLUSIVE_SECTION {
    ProcessGroupManagerContainer pgroups;
    for (size_t i = 0; i < testParams.ngroup; ++i) {
      int pgroupRootID((int)i);
      pgroups.emplace_back(std::make_shared<ProcessGroupManager>(pgroupRootID));
    }

    auto loadmodel = std::unique_ptr<LoadModel>(new LinearLoadModel());
    std::vector<bool> boundary(testParams.dim, testParams.boundary);

    // create third level specific scheme
    CombiMinMaxScheme combischeme(testParams.dim, testParams.lmin, testParams.lmax);
    combischeme.createClassicalCombischeme();
    // combischeme.createAdaptiveCombischeme();
    // get full scheme first
    std::vector<LevelVector> fullLevels = combischeme.getCombiSpaces();
    std::vector<combigrid::real> fullCoeffs = combischeme.getCoeffs();
    // split scheme and assign each half to a system
    std::vector<LevelVector> levels;
    std::vector<combigrid::real> coeffs;
    CombiThirdLevelScheme::createThirdLevelScheme(fullLevels, fullCoeffs, boundary,
                                                  testParams.sysNum, 2, levels, coeffs);

    BOOST_REQUIRE_EQUAL(levels.size(), coeffs.size());
    // std::cout << "Combischeme " << testParams.sysNum << ":" << std::endl;
    // for (size_t i =0; i < levels.size(); ++i)
    //  std::cout << toString(levels[i]) << " " << coeffs[i]<< std::endl;

    // create Tasks
    TaskContainer tasks;
    std::vector<size_t> taskIDs;
    for (size_t i = 0; i < levels.size(); i++) {
      Task* t = new TaskConstParaboloid(levels[i], boundary, coeffs[i], loadmodel.get());
      // Task* t = new TaskCount(2, levels[i], boundary, coeffs[i], loadmodel.get());

      tasks.push_back(t);
      taskIDs.push_back(t->getID());
    }

    // create combiparameters
    IndexVector parallelization = {static_cast<long>(testParams.nprocs), 1};
    CombiParameters combiParams(testParams.dim, testParams.lmin, testParams.lmax, boundary, levels,
                                coeffs, taskIDs, testParams.ncombi, 1, parallelization,
                                std::vector<IndexType>(testParams.dim, 0), std::vector<IndexType>(testParams.dim, 1),
                                true, testParams.host, testParams.port, 0);

    // create abstraction for Manager
    ProcessManager manager(pgroups, tasks, combiParams, std::move(loadmodel));

    // the combiparameters are sent to all process groups before the
    // computations start
    manager.updateCombiParameters();

    for (unsigned int i = 0; i < testParams.ncombi; i++) {
      if (i == 0) {
        Stats::startEvent("manager run");
        manager.runfirst();
        Stats::stopEvent("manager run");

        // exchange subspace sizes to unify the dsgs with the remote system
        Stats::startEvent("manager unify subspace sizes with remote");
        manager.unifySubspaceSizesThirdLevel(),
        Stats::stopEvent("manager unify subspace sizes with remote");
      } else {
        Stats::startEvent("manager run");
        manager.runnext();
        Stats::stopEvent("manager run");
      }
      // combine grids
      Stats::startEvent("manager combine third level");
      manager.combineThirdLevel();
      // manager.combine();
      Stats::stopEvent("manager combine third level");
    }

    // test Monte-Carlo interpolation
    std::vector<std::vector<real>> interpolationCoords;
    std::vector<real> values (1000);
    real l2ErrorSingle = 0.;
    // TestFnCount<CombiDataType> initialFunction;
    ParaboloidFn<CombiDataType> initialFunction;

    // compare to third-level monte carlo interpolation
    manager.monteCarloThirdLevel(1000, interpolationCoords, values);
    real l2ErrorTwoSystems = 0.;
    for (size_t i = 0; i < interpolationCoords.size(); ++i) {
      // l2ErrorTwoSystems += std::pow(initialFunction(interpolationCoords[i], testParams.ncombi) - values[i], 2);
      l2ErrorTwoSystems += std::pow(initialFunction(interpolationCoords[i]) - values[i], 2);
    }

    Stats::startEvent("manager interpolate");
    values = manager.interpolateValues(interpolationCoords);
    Stats::stopEvent("manager interpolate");

    for (size_t i = 0; i < interpolationCoords.size(); ++i) {
      // l2ErrorSingle += std::pow(initialFunction(interpolationCoords[i], testParams.ncombi) - values[i], 2);
      l2ErrorSingle += std::pow(initialFunction(interpolationCoords[i]) - values[i], 2);
    }

    std::cout << "Monte carlo errors are " << l2ErrorSingle << " on this system and " <<
      l2ErrorTwoSystems << " in total. boundary: " << boundary << std::endl;
    // only do check if no boundary, otherwise all components interpolate exactly on the hyperplane anyways
    // auto hasBoundary = std::all_of(boundary.begin(), boundary.end(), [] (bool b) {return std::forward<bool>(b);});
    // if (!hasBoundary) {
    BOOST_CHECK_LE(l2ErrorTwoSystems, l2ErrorSingle);
    // }

    std::string filename("thirdLevel_" + std::to_string(testParams.ncombi) + ".raw");
    Stats::startEvent("manager write solution");
    manager.parallelEval(testParams.lmax, filename, 0);
    Stats::stopEvent("manager write solution");

    manager.exit();

    // if output files are not needed, remove them right away
    remove(("thirdLevel_" + std::to_string(testParams.ncombi) + "_0.raw").c_str());
    remove(("thirdLevel_" + std::to_string(testParams.ncombi) + "_0.raw_header").c_str());
  }
  else {
    ProcessGroupWorker pgroup;
    SignalType signal = -1;
    signal = pgroup.wait();
    // omitting to count RUN_FIRST signal, as it is executed once for every task
    int nrun = 1;
    while (signal != EXIT) {
      signal = pgroup.wait();
      if (signal == RUN_NEXT) {
        ++nrun;
      }
      // std::cout << "Worker with rank " << theMPISystem()->getLocalRank() << " processed signal "
      //           << signal << std::endl;
      if (signal == COMBINE_THIRD_LEVEL) {
        // after combination check workers' grids
        BOOST_CHECK(
            !testParams.boundary ||
            checkReducedFullGrid(pgroup, nrun));  // TODO for no boundary, check inner values
      }
      // if(signal == WAIT_FOR_TL_SIZE_UPDATE)
      // if(signal == WAIT_FOR_TL_COMBI_RESULT)
      if (signal == REDUCE_SUBSPACE_SIZES_TL) {
        std::cout << "reduce ";
        for (auto& dsg : pgroup.getCombinedUniDSGVector()) {
          for (size_t size : dsg->getSubspaceDataSizes()) {
            std::cout << size << " ";
          }
        }
        std::cout << std::endl;
      }
      if(signal == INIT_DSGUS){
        std::cout << "INIT DSGUS ";
        for (auto& dsg : pgroup.getCombinedUniDSGVector()) {
          for (size_t size : dsg->getSubspaceDataSizes()) {
            std::cout << size << " ";
          }
        }
        std::cout << std::endl;
      }
    }
    for (const auto& b : pgroup.getCombiParameters().getBoundary())
      BOOST_CHECK_EQUAL(b, testParams.boundary);
    for (const auto& r : pgroup.getCombiParameters().getLMaxReductionVector())
      BOOST_CHECK_EQUAL(r, 1);
  }

  combigrid::Stats::finalize();
  combigrid::Stats::write("stats_thirdLevel_" + std::to_string(testParams.sysNum) + ".json");
  MPI_Barrier(testParams.comm);
  TestHelper::testStrayMessages(testParams.comm);
}


/**
 * @brief test for the static task assignment mechanism, both systems read their assignment from file `test_scheme.json`
 */
void testCombineThirdLevelStaticTaskAssignment(TestParams& testParams) {
  BOOST_CHECK(testParams.comm != MPI_COMM_NULL);

  combigrid::Stats::initialize();
  theMPISystem()->initWorldReusable(testParams.comm, testParams.ngroup, testParams.nprocs);

  auto loadmodel = std::unique_ptr<LoadModel>(new LinearLoadModel());
  std::vector<bool> boundary(testParams.dim, testParams.boundary);

  std::vector<LevelVector> levels;
  std::vector<combigrid::real> coeffs;
  std::vector<size_t> taskNumbers; // only used in case of static task assignment
  bool useStaticTaskAssignment = false;
  {
  // read in CT scheme
    std::unique_ptr<CombiMinMaxSchemeFromFile> scheme(
        new CombiMinMaxSchemeFromFile(testParams.dim, testParams.lmin, testParams.lmax, "test_scheme.json"));
    const auto& pgNumbers = scheme->getProcessGroupNumbers();
    if (pgNumbers.size() > 0) {
      useStaticTaskAssignment = true;
      const auto& allCoeffs = scheme->getCoeffs();
      const auto& allLevels = scheme->getCombiSpaces();
      const auto [itMin, itMax] = std::minmax_element(pgNumbers.begin(), pgNumbers.end());
      assert(*itMin == 0);  // make sure it starts with 0
      // filter out only those tasks that belong to "our" process group
      const auto& pgroupNumber = theMPISystem()->getProcessGroupNumber();
      for (size_t taskNo = 0; taskNo < pgNumbers.size(); ++taskNo) {
        if (pgNumbers[taskNo] == pgroupNumber) {
          taskNumbers.push_back(taskNo);
          coeffs.push_back(allCoeffs[taskNo]);
          levels.push_back(allLevels[taskNo]);
        }
      }
      MASTER_EXCLUSIVE_SECTION {
        std::cout << " Process group " << pgroupNumber << " will run " << levels.size() << " of "
                  << pgNumbers.size() << " tasks." << std::endl;
      }
    } else {
      // levels and coeffs are only used in manager
      WORLD_MANAGER_EXCLUSIVE_SECTION {
        coeffs = scheme->getCoeffs();
        levels = scheme->getCombiSpaces();
        std::cout << levels.size() << " tasks to distribute." << std::endl;
      }
    }
  }

  BOOST_REQUIRE(useStaticTaskAssignment);
  BOOST_REQUIRE_EQUAL(levels.size(), coeffs.size());

  WORLD_MANAGER_EXCLUSIVE_SECTION {
    ProcessGroupManagerContainer pgroups;
    for (size_t i = 0; i < testParams.ngroup; ++i) {
      int pgroupRootID((int)i);
      pgroups.emplace_back(std::make_shared<ProcessGroupManager>(pgroupRootID));
    }

    TaskContainer tasks;
    std::vector<size_t> taskIDs;
    for (size_t i = 0; i < levels.size(); i++) {
      Task* t = new TaskConstParaboloid(levels[i], boundary, coeffs[i], loadmodel.get());

      tasks.push_back(t);
      taskIDs.push_back(t->getID());
    }

    if (useStaticTaskAssignment) {
      // read in CT scheme -- again!
      std::unique_ptr<CombiMinMaxSchemeFromFile> scheme(new CombiMinMaxSchemeFromFile(
          testParams.dim, testParams.lmin, testParams.lmax, "test_scheme.json"));
      const auto& pgNumbers = scheme->getProcessGroupNumbers();
      for (size_t taskNo = 0; taskNo < tasks.size(); ++taskNo) {
        pgroups[pgNumbers[taskNo]]->storeTaskReference(tasks[taskNo]);
      }
    }

    // create combiparameters
    IndexVector parallelization = {static_cast<long>(testParams.nprocs), 1};
    CombiParameters combiParams(testParams.dim, testParams.lmin, testParams.lmax, boundary, levels,
                                coeffs, taskIDs, testParams.ncombi, 1, parallelization,
                                std::vector<IndexType>(testParams.dim, 0), std::vector<IndexType>(testParams.dim, 1),
                                true, testParams.host, testParams.port, 0);

    // create abstraction for Manager
    ProcessManager manager(pgroups, tasks, combiParams, std::move(loadmodel));

    // the combiparameters are sent to all process groups before the
    // computations start
    manager.updateCombiParameters();

    for (unsigned int i = 0; i < testParams.ncombi; i++) {
      if (i == 0) {
        Stats::startEvent("manager no run first");
        BOOST_TEST_CHECKPOINT("manager first runnext");
        manager.runnext();
        BOOST_TEST_CHECKPOINT("manager init dsgus");
        manager.initDsgus();
        Stats::stopEvent("manager no run first");

        // exchange subspace sizes to unify the dsgs with the remote system
        Stats::startEvent("manager unify subspace sizes with remote");
        BOOST_TEST_CHECKPOINT("manager unifySubspaceSizesThirdLevel");
        manager.unifySubspaceSizesThirdLevel(),
        Stats::stopEvent("manager unify subspace sizes with remote");
      } else {
        Stats::startEvent("manager run");
        BOOST_TEST_CHECKPOINT("manager runnext " + std::to_string(i));
        manager.runnext();
        Stats::stopEvent("manager run");
      }
      // combine grids
      Stats::startEvent("manager combine third level");
      BOOST_TEST_CHECKPOINT("manager combineThirdLevel " + std::to_string(i));
      manager.combineThirdLevel();
      Stats::stopEvent("manager combine third level");
    }

    BOOST_TEST_CHECKPOINT("manager exit");
    manager.exit();
  }
  else {
    ProcessGroupWorker pgroup;
    SignalType signal = -1;
    while (signal != EXIT) {
      BOOST_TEST_CHECKPOINT("Last Successful Worker Signal " + std::to_string(signal));
      signal = pgroup.wait();
      // if using static task assignment, we initialize all tasks after the combi parameters are
      // updated
      if (useStaticTaskAssignment) {
        if (signal == UPDATE_COMBI_PARAMETERS) {
          // initialize all "our" tasks
          for (size_t taskIndex = 0; taskIndex < taskNumbers.size(); ++taskIndex) {
            auto task = new TaskConstParaboloid(levels[taskIndex], boundary, coeffs[taskIndex], loadmodel.get());
            task->setID(taskNumbers[taskIndex]);
            pgroup.initializeTaskAndFaults(task);
          }
        }
        if (signal == RUN_FIRST) {
          BOOST_CHECK(false);
        }
      }
    }
  }
  combigrid::Stats::finalize();
  MPI_Barrier(testParams.comm);
  TestHelper::testStrayMessages(testParams.comm);
}

BOOST_FIXTURE_TEST_SUITE(thirdLevel, TestHelper::BarrierAtEnd, *boost::unit_test::timeout(60))

BOOST_AUTO_TEST_CASE(test_0, *boost::unit_test::tolerance(TestHelper::tolerance)) {
  unsigned int numSystems = 2;
  unsigned int ngroup = 1;
  unsigned int nprocs = 1;
  unsigned int ncombi = 3;
  DimType dim = 2;
  LevelVector lmin(dim, 1);
  LevelVector lmax(dim, 2);

  unsigned int sysNum;
  CommunicatorType newcomm;

  for (bool boundary : {false, true}) {
    assignProcsToSystems(ngroup, nprocs, numSystems, sysNum, newcomm);

    if (sysNum < numSystems) {  // remove unnecessary procs
      TestParams testParams(dim, lmin, lmax, boundary, ngroup, nprocs, ncombi, sysNum, newcomm);
      startInfrastructure();
      testCombineThirdLevel(testParams);
    }

    MPI_Barrier(MPI_COMM_WORLD);
  }
}

BOOST_AUTO_TEST_CASE(test_2, *boost::unit_test::tolerance(TestHelper::tolerance)) {
  unsigned int numSystems = 2;
  unsigned int ngroup = 1;
  unsigned int nprocs = 1;
  unsigned int ncombi = 10;
  DimType dim = 2;
  LevelVector lmin(dim, 2);
  LevelVector lmax(dim, 3);

  unsigned int sysNum;
  CommunicatorType newcomm;

  for (bool boundary : {true}) {
    assignProcsToSystems(ngroup, nprocs, numSystems, sysNum, newcomm);

    if (sysNum < numSystems) {  // remove unnecessary procs
      TestParams testParams(dim, lmin, lmax, boundary, ngroup, nprocs, ncombi, sysNum, newcomm);
      startInfrastructure();
      testCombineThirdLevel(testParams);
    }

    MPI_Barrier(MPI_COMM_WORLD);
  }
}

BOOST_AUTO_TEST_CASE(test_3, *boost::unit_test::tolerance(TestHelper::tolerance)) {
  unsigned int numSystems = 2;
  unsigned int ngroup = 1;
  unsigned int nprocs = 1;
  unsigned int ncombi = 10;
  DimType dim = 2;
  LevelVector lmin(dim, 4);
  LevelVector lmax(dim, 7);

  unsigned int sysNum;
  CommunicatorType newcomm;

  for (bool boundary : {false, true}) {
    assignProcsToSystems(ngroup, nprocs, numSystems, sysNum, newcomm);

    if (sysNum < numSystems) {  // remove unnecessary procs
      TestParams testParams(dim, lmin, lmax, boundary, ngroup, nprocs, ncombi, sysNum, newcomm);
      startInfrastructure();
      testCombineThirdLevel(testParams);
    }

    MPI_Barrier(MPI_COMM_WORLD);
  }
}

BOOST_AUTO_TEST_CASE(test_4, *boost::unit_test::tolerance(TestHelper::tolerance)) {
  unsigned int numSystems = 2;
  unsigned int ngroup = 2;
  unsigned int nprocs = 1;
  unsigned int ncombi = 10;
  DimType dim = 2;
  LevelVector lmin(dim, 4);
  LevelVector lmax(dim, 7);

  unsigned int sysNum;
  CommunicatorType newcomm;

  for (bool boundary : {false, true}) {
    assignProcsToSystems(ngroup, nprocs, numSystems, sysNum, newcomm);

    if (sysNum < numSystems) {  // remove unnecessary procs
      TestParams testParams(dim, lmin, lmax, boundary, ngroup, nprocs, ncombi, sysNum, newcomm);
      startInfrastructure();
      testCombineThirdLevel(testParams);
    }

    MPI_Barrier(MPI_COMM_WORLD);
  }
}

BOOST_AUTO_TEST_CASE(test_5, *boost::unit_test::tolerance(TestHelper::tolerance)) {
  unsigned int numSystems = 2;
  unsigned int ngroup = 1;
  unsigned int nprocs = 2;
  unsigned int ncombi = 10;
  DimType dim = 2;
  LevelVector lmin(dim, 4);
  LevelVector lmax(dim, 7);

  unsigned int sysNum;
  CommunicatorType newcomm;

  for (bool boundary : {false, true}) {
    assignProcsToSystems(ngroup, nprocs, numSystems, sysNum, newcomm);

    if (sysNum < numSystems) {  // remove unnecessary procs
      TestParams testParams(dim, lmin, lmax, boundary, ngroup, nprocs, ncombi, sysNum, newcomm);
      startInfrastructure();
      testCombineThirdLevel(testParams);
    }

    MPI_Barrier(MPI_COMM_WORLD);
  }
}

// like test_5, but with static group assignment
BOOST_AUTO_TEST_CASE(test_6, *boost::unit_test::tolerance(TestHelper::tolerance)) {
  unsigned int numSystems = 2;
  unsigned int ngroup = 3;
  unsigned int nprocs = 1;
  unsigned int ncombi = 10;
  DimType dim = 2;
  LevelVector lmin = {3,6};
  LevelVector lmax = {7,10};

  unsigned int sysNum;
  CommunicatorType newcomm;

  for (bool boundary : {false, true}) {
    assignProcsToSystems(ngroup, nprocs, numSystems, sysNum, newcomm);

    if (sysNum < numSystems) {  // remove unnecessary procs
      TestParams testParams(dim, lmin, lmax, boundary, ngroup, nprocs, ncombi, sysNum, newcomm);
      startInfrastructure();
      testCombineThirdLevelStaticTaskAssignment(testParams);
    }

    MPI_Barrier(MPI_COMM_WORLD);
  }
}

BOOST_AUTO_TEST_SUITE_END()

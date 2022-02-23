#ifndef __DYNAMIC_TRAJECTORY_HPP__
#define __DYNAMIC_TRAJECTORY_HPP__

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define __SCREEN_OUTPUT__

#include "dynamic_waypoint.hpp"
#include "mav_trajectory_generation/polynomial_optimization_linear.h"
#include "mav_trajectory_generation/polynomial_optimization_nonlinear.h"
#include "mav_trajectory_generation/trajectory.h"
#include "thread_safe_trajectory.hpp"
#include "utils/logging_utils.hpp"
#include "utils/traj_modifiers.hpp"

#define MAV_MAX_ACCEL (1 * 9.81f)
#define N_WAYPOINTS_TO_APPEND 1
#define TIME_STITCHING_SECURITY_COEF 0.9
#define TIME_CONSTANT 1.0
#define SECURITY_ZONE_MULTIPLIER 0.000 // TODO : This means no security zone

#define SECURITY_TIME_BEFORE_WAYPOINT 4.0
// #define SECURITY_TIME_BEFORE_WAYPOINT \
//   (SECURITY_ZONE_MULTIPLIER * computeSecurityTime(dynamic_waypoints_.size(), TIME_CONSTANT))

constexpr float AsyntoticComplexity(int n)
{
  float value = n * n; // TODO: CALCULATE THIS CORRECTLY
  return value;
}

namespace dynamic_traj_generator
{

  struct References
  {
    Eigen::Vector3d position;
    Eigen::Vector3d velocity;
    Eigen::Vector3d acceleration;

    Eigen::Vector3d &operator[](int index)
    {
      switch (index)
      {
      case 0:
        return position;
      case 1:
        return velocity;
      case 2:
        return acceleration;
      default:
        throw std::runtime_error("Invalid index");
      }
    }
  };

  class DynamicTrajectory
  {
  private:
    // MEMBER ATTRIBUTES
    struct NumericParameters
    {
      double algorithm_time_constant = TIME_CONSTANT;
      double last_local_time_evaluated = 0.0f;
      double t_offset = 0.0f;
      double last_global_time_evaluated = 0.0f;
      double speed = 0.0f;
      /* double compensation_time = 0.0f; */
      double global_time_last_trajectory_generated = 0.0f;
    } parameters_, new_parameters_;

    // const int derivative_to_optimize_ =
    // mav_trajectory_generation::derivative_order::JERK;
    const int derivative_to_optimize_ = mav_trajectory_generation::derivative_order::ACCELERATION;
    // const int derivative_to_optimize_ =
    // mav_trajectory_generation::derivative_order::VELOCITY;

    ThreadSafeTrajectory traj_;
    std::future<ThreadSafeTrajectory> future_traj_;

    const int dimension_ = 3;
    std::atomic_bool from_scratch_ = true;
    const double a_max_ = MAV_MAX_ACCEL;
    Eigen::Vector3d vehicle_position_;

    mutable std::mutex traj_mutex_;
    mutable std::mutex future_mutex_;
    mutable std::mutex dynamic_waypoints_mutex_;
    mutable std::mutex parameters_mutex_;
    mutable std::mutex todo_mutex;
    mutable std::mutex vehicle_position_mutex_;

    dynamic_traj_generator::DynamicWaypoint::Deque dynamic_waypoints_;
    dynamic_traj_generator::DynamicWaypoint::Deque next_trajectory_waypoint_;
    dynamic_traj_generator::DynamicWaypoint::Vector waypoints_to_be_added_;
    dynamic_traj_generator::DynamicWaypoint::Vector waypoints_to_be_set_;
    std::vector<std::pair<std::string, Eigen::Vector3d>> waypoints_to_be_modified_;

    std::atomic_bool generate_new_traj_ = false;
    std::atomic_bool computing_new_trajectory_ = false;
    std::atomic_bool stop_process_ = false;
    std::atomic_bool trajectory_regenerated_ = false;

    std::thread waitForGeneratingNewTraj_thread_;

  public:
    // PUBLIC FUNCTIONS

    DynamicTrajectory()
    {
      waitForGeneratingNewTraj_thread_ = std::thread(&DynamicTrajectory::todoThreadLoop, this);
    }
    ~DynamicTrajectory()
    {
      stop_process_ = true;
      waitForGeneratingNewTraj_thread_.join();
    }

    // principal functions
    void setWaypoints(const DynamicWaypoint::Vector &waypoints);
    void appendWaypoint(const DynamicWaypoint &waypoint);
    void modifyWaypoint(const std::string &name, const Eigen::Vector3d &position);
    bool evaluateTrajectory(const float &t, dynamic_traj_generator::References &refs,
                            bool only_positions = false, bool for_plotting = false);
    void generateTrajectory(const DynamicWaypoint::DynamicWaypoint::Deque &waypoints, bool force);

    // getters
    void setSpeed(double speed);
    double getMaxTime();
    double getMinTime();
    DynamicWaypoint::Deque getDynamicWaypoints();
    double getSpeed() const;
    double getTimeCompensation();
    bool getWasTrajectoryRegenerated();
    inline void updateVehiclePosition(const Eigen::Vector3d &position){
      std::lock_guard<std::mutex> lock(vehicle_position_mutex_);
      vehicle_position_ = position;
    };


  private:
    // PRIVATE FUNCTIONS

    inline Eigen::Vector3d getVehiclePosition() const
    {
      std::lock_guard<std::mutex> lock(vehicle_position_mutex_);
      return vehicle_position_ ;
    };

    double convertIntoGlobalTime(double t);
    double convertFromGlobalTime(double t);

    bool checkIfTrajectoryCanBeGenerated();
    bool checkStitchTrajectory();
    bool checkInSecurityZone();
    bool checkTrajectoryModifiers();

    bool checkTrajectoryGenerated();
    bool checkIfTrajectoryIsAlreadyGenerated() { return traj_ != nullptr; };
    void waitUntilTrajectoryIsGenerated() { checkTrajectoryGenerated(); };

    void swapTrajectory();
    void swapDynamicWaypoints();
    void appendDronePositionWaypoint(DynamicWaypoint::Deque& waypoints);

    void todoThreadLoop();
    double computeSecurityTime(int n, double TimeConstant)
    {
      return TimeConstant * AsyntoticComplexity(n);
    }

    bool applyWaypointModification(const std::string &name, const Eigen::Vector3d &position);
    ThreadSafeTrajectory computeTrajectory(const DynamicWaypoint::Deque &waypoints,
                                           const bool &lineal_optimization = false);

    Eigen::Vector3d evaluateModifiedTrajectory(const ThreadSafeTrajectory &traj, double global_time,
                                               double local_time, const int order = 0);

    References getReferences(const ThreadSafeTrajectory &traj, double global_time, double local_time,
                             const bool only_positions = false);

    void filterPassedWaypoints(DynamicWaypoint::Deque &waypoints);

    /**
     * @brief
     *
     * @param current_traj reference to the current trajectory
     * @param last_t_evaluated last T evaluated, will be the beggining point of
     * the new trajectory
     * @param waypoints new dynamic waypoints vector
     * @param TimeConstantAlgorithm Time constant (Ct) of computing trajectory:
     * The complexity of the algoritm is O(f(n)) this means that it will spend at
     * least t = Ct * f(n) sec
     */
    DynamicWaypoint::Deque stitchActualTrajectoryWithNewWaypoints(
        double last_t_evaluated, const DynamicWaypoint::Deque &waypoints);
    DynamicWaypoint::Deque generateWaypointsForTheNextTrajectory();
  };

} // namespace dynamic_traj_generator

#endif // __DYNAMIC_TRAJECTORY_HPP__

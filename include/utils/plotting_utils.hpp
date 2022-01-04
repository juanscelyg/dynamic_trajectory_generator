#ifndef __PLOTTING_UTILS_HPP__
#define __PLOTTING_UTILS_HPP__

#include <chrono>
#include <exception>
#include <iostream>
#include <map>
#include <random>
#include <thread>

#include "dynamic_trajectory.hpp"

namespace plt = matplotlibcpp;

#define STEP_SIZE 10ms

static char dist[] = {'r', 'g', 'b', 'c', 'm', 'y', 'k'};

class TrajectoryPlotter
{
private:
  long number_;
  long number2d_;

public:
  // enum with 3 printing modes : Line, Waypoint, UAV
  enum class PlotMode
  {
    LINE,
    WAYPOINT,
    UAV
  };

  TrajectoryPlotter(long number = 1)
      : number_(number * 2), number2d_(number * 2 + 1)
  {
    uav_pose_x_ = std::vector<double>(1);
    uav_pose_y_ = std::vector<double>(1);
    uav_pose_z_ = std::vector<double>(1);
    uav_time_ = std::vector<double>(1);
    static_plots_3d_.clear();
    plot_thread_ = std::thread(&TrajectoryPlotter::plot, this);
    ended_ = false;
  }
  ~TrajectoryPlotter()
  {
    ended_ = true;
    plot_thread_.join();
  }

  std::vector<double> uav_pose_x_;
  std::vector<double> uav_pose_y_;
  std::vector<double> uav_pose_z_;
  std::vector<double> uav_time_;

  std::vector<std::tuple<std::vector<double>, // x
                         std::vector<double>, // y
                         std::vector<double>, // z
                         std::string,         // color
                         PlotMode>            // plotMode
              >
      static_plots_3d_;

  std::vector<std::tuple<std::vector<double>, // time
                         std::vector<double>, // x
                         std::vector<double>, // y
                         std::vector<double>, // z
                         std::string,         // color
                         PlotMode             // plotMode>
                         >>
      static_plots_2d_;

  std::atomic_bool ended_;
  std::atomic_bool update_plot_;

  std::thread plot_thread_;

  std::mutex mutex_;

  void plotTraj(dynamic_traj_generator::DynamicTrajectory &traj)
  {
    double t_start = traj.getMinTime();
    double t_end = traj.getMaxTime();
    double dt = 0.1;
    int n_samples = (t_end - t_start) / dt;
    mutex_.lock();
    auto plot_x = std::vector<double>(n_samples);
    auto ploy_y = std::vector<double>(n_samples);
    auto plot_z = std::vector<double>(n_samples);
    auto plot_time = std::vector<double>(n_samples);

    dynamic_traj_generator::References refs;
    for (int i = 0; i < n_samples; i++)
    {
      double t_eval = t_start + i * dt;
      traj.evaluateTrajectory(t_eval, refs, true);
      plot_x[i] = refs.position(0);
      ploy_y[i] = refs.position(1);
      plot_z[i] = refs.position(2);
      plot_time[i] = t_eval;
    }

    srand(time(NULL));
    std::string color(1, dist[rand() % 7]);

    static_plots_3d_.emplace_back(plot_x, ploy_y, plot_z, color, PlotMode::LINE);
    static_plots_2d_.emplace_back(plot_time, plot_x, ploy_y, plot_z, color, PlotMode::LINE);

    auto waypoints = traj.getWaypoints();
    auto segments = traj.getSegments();

    auto waypoints_x = std::vector<double>(waypoints.size());
    auto waypoints_y = std::vector<double>(waypoints.size());
    auto waypoints_z = std::vector<double>(waypoints.size());
    auto segments_time = std::vector<double>(waypoints.size(), 0);

    // DYNAMIC_LOG(segments.size());
    // DYNAMIC_LOG(waypoints.size());

    for (int i = 0; i < waypoints.size(); i++)
    {
      dynamic_traj_generator::References ref;

      if (i < segments.size())
      {
        segments_time[i + 1] = segments_time[i] + segments[i].getTime();
      }
      traj.evaluateTrajectory(segments_time[i], ref, true);
      waypoints_x[i] = ref.position(0);
      waypoints_y[i] = ref.position(1);
      waypoints_z[i] = ref.position(2);
    }

    static_plots_3d_.emplace_back(waypoints_x, waypoints_y, waypoints_z, color, PlotMode::WAYPOINT);
    static_plots_2d_.emplace_back(segments_time, waypoints_x, waypoints_y, waypoints_z, color, PlotMode::WAYPOINT);

    mutex_.unlock();
    update_plot_ = true;
  };

  void setUAVposition(const dynamic_traj_generator::References &refs, const double &time)
  {
    mutex_.lock();
    uav_pose_x_[0] = refs.position(0);
    uav_pose_y_[0] = refs.position(1);
    uav_pose_z_[0] = refs.position(2);
    uav_time_[0] = time;
    mutex_.unlock();
    update_plot_ = true;
  };

  void clear2dGraph()
  {
    plt::figure(number2d_);
    plt::subplot(3, 1, 1);
    plt::cla();
    plt::subplot(3, 1, 2);
    plt::cla();
    plt::subplot(3, 1, 3);
    plt::cla();
  }
  void clear3dGraph()
  {
    plt::figure(number_);
    plt::cla();
  }

  void plot2dGraph(const std::vector<double> &time, const std::vector<double> &x, const std::vector<double> &y,
                   const std::vector<double> &z, const std::string &color, const PlotMode &plotMode)
  {
    std::string options;

    switch (plotMode)
    {
    case PlotMode::LINE:
      options = color + "-";
      break;
    case PlotMode::UAV:
      options = color + "o";
      break;
    case PlotMode::WAYPOINT:
      options = color + "x";
      break;
    default:
      throw std::runtime_error("Invalid plot mode");
    }

    plt::figure(number2d_);
    plt::subplot(3, 1, 1);
    plt::plot(time, x, options);
    plt::subplot(3, 1, 2);
    plt::plot(time, y, options);
    plt::subplot(3, 1, 3);
    plt::plot(time, z, options);
  };

  void plot3dGraph(const std::vector<double> &x, const std::vector<double> &y, const std::vector<double> &z,
                   const std::string &color, const PlotMode &plotMode)
  {
    std::map<std::string, std::string> style;
    style["color"] = color;

    switch (plotMode)
    {
    case PlotMode::LINE:
    {
    }
    break;
    case PlotMode::UAV:
    {
      style["marker"] = "o";
      style["markersize"] = "5";
      style["linestyle"] = "none";
    }

    break;
    case PlotMode::WAYPOINT:
    {
      style["marker"] = "x";
      style["markersize"] = "7";
      style["linestyle"] = "none";
    }
    break;
    default:
      throw std::runtime_error("Invalid plot mode");
    }

    plt::figure(number_);
    plt::plot3(x, y, z, style, number_);
  };

  void plot()
  {
    plt::figure(number_);
    plt::grid(true);

    while (!ended_)
    {
      if (!update_plot_)
      {
        plt::pause(0.01);
        continue;
      }

      clear3dGraph();
      clear2dGraph();
      mutex_.lock();

      for (auto &kwargs : static_plots_3d_)
      {
        plot3dGraph(std::get<0>(kwargs), std::get<1>(kwargs), std::get<2>(kwargs), std::get<3>(kwargs), std::get<4>(kwargs));
      }
      for (auto &kwargs : static_plots_2d_)
      {
        plot2dGraph(std::get<0>(kwargs), std::get<1>(kwargs), std::get<2>(kwargs), std::get<3>(kwargs), std::get<4>(kwargs),
                    std::get<5>(kwargs));
      };

      plot3dGraph(uav_pose_x_, uav_pose_y_, uav_pose_z_, "r", PlotMode::UAV);
      plot2dGraph(uav_time_, uav_pose_x_, uav_pose_y_, uav_pose_z_, "r", PlotMode::UAV);
      mutex_.unlock();

      plt::show(false);
      update_plot_ = false;
    }
    DYNAMIC_LOG("Close figure to continue");
    plt::show(true);
    plt::close();
  };
};

#endif // DYNAMIC_TRAJECTORY_PLOTTER_HPP

#pragma once

/* includes //{ */
#include <image_transport/image_transport.h>
#include <image_transport/subscriber_filter.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <message_filters/sync_policies/exact_time.h>

#include <pcl_tools/support.h>

#include <pcl/filters/crop_box.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/sample_consensus/ransac.h>
#include <pcl/sample_consensus/sac_model_perpendicular_plane.h>

#include <pcl_conversions/pcl_conversions.h>

#include <mrs_lib/transformer.h>
#include <mrs_lib/subscribe_handler.h>
#include <mrs_lib/scope_timer.h>

#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/Range.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/image_encodings.h>

#include <geometry_msgs/Point.h>
#include <geometry_msgs/PointStamped.h>
#include <geometry_msgs/Transform.h>

#include <visualization_msgs/MarkerArray.h>

#include <mrs_modules_msgs/PclToolsDiagnostics.h>

#include <boost/smart_ptr/make_shared_array.hpp>
#include <limits>

#include <tf2_eigen/tf2_eigen.h>

#include <pcl_tools/pcl_filtration_dynparamConfig.h>

#include <pcl_tools/remove_below_ground_filter.h>

//}

namespace pcl_tools
{
using vec3_t = Eigen::Vector3f;
using vec4_t = Eigen::Vector4f;
using quat_t = Eigen::Quaternionf;

namespace enc = sensor_msgs::image_encodings;
/* class SensorDepthCamera //{ */

/*//{ DepthTraits */

// Encapsulate differences between processing float and uint16_t depths in RGBD data
template <typename T>
struct DepthTraits
{};

template <>
struct DepthTraits<uint16_t>
{
  static inline bool valid(uint16_t depth) {
    return depth != 0;
  }
  static inline float toMeters(uint16_t depth) {
    return float(depth) * 0.001f;
  }  // originally mm
  static inline uint16_t fromMeters(float depth) {
    return (depth * 1000.0f) + 0.5f;
  }
};

template <>
struct DepthTraits<float>
{
  static inline bool valid(float depth) {
    return std::isfinite(depth);
  }
  static inline float toMeters(float depth) {
    return depth;
  }
  static inline float fromMeters(float depth) {
    return depth;
  }
};

/*//}*/

/*//{ IntensityTraits */

// Encapsulate differences between processing float, uint16_t and uint8_t intensities in RGBDI data
template <typename T>
struct IntensityTraits
{};

template <>
struct IntensityTraits<uint8_t>
{
  static inline bool valid(uint8_t intensity) {
    return intensity != 0;
  }
  static inline float toFloat(uint8_t intensity) {
    return float(intensity);
  }
  static inline uint8_t fromFloat(float intensity) {
    return intensity + 0.5f;
  }
};

template <>
struct IntensityTraits<uint16_t>
{
  static inline bool valid(uint16_t intensity) {
    return intensity != 0;
  }
  static inline float toFloat(uint16_t intensity) {
    return float(intensity);
  }
  static inline uint16_t fromFloat(float intensity) {
    return intensity + 0.5f;
  }
};

template <>
struct IntensityTraits<float>
{
  static inline bool valid(float intensity) {
    return std::isfinite(intensity);
  }
  static inline float toFloat(float intensity) {
    return intensity;
  }
  static inline float fromFloat(float intensity) {
    return intensity;
  }
};

/*//}*/

class SensorDepthCamera {
public:
  void initialize(const ros::NodeHandle& nh, const std::shared_ptr<CommonHandlers_t> common_handlers, const std::string& prefix, const std::string& name);

private:
  template <typename T>
  void convertDepthToCloud(const sensor_msgs::Image::ConstPtr& depth_msg, PC::Ptr& cloud_out, PC::Ptr& cloud_over_max_range_out,
                           const bool return_removed_close = false, const bool return_removed_far = false, const bool replace_nans = false,
                           const bool keep_ordered = false);

  template <typename T>
  void convertDepthToCloudUnordered(const sensor_msgs::Image::ConstPtr& depth_msg, PC::Ptr& out_pc, PC::Ptr& removed_pc,
                                    const bool return_removed_close = false, const bool return_removed_far = false, const bool replace_nans = false);

  template <typename T>
  void convertDepthToCloudOrdered(const sensor_msgs::Image::ConstPtr& depth_msg, PC::Ptr& out_pc, PC::Ptr& removed_pc, const bool return_removed_close = false,
                                  const bool return_removed_far = false, const bool replace_nans = false);

  void imagePointToCloudPoint(const int x, const int y, const float depth, pt_XYZ& point);


  template <typename T, typename U>
  void convertDepthToCloud(const sensor_msgs::Image::ConstPtr& depth_msg, const sensor_msgs::Image::ConstPtr& intensity_msg, PC_I::Ptr& cloud_out,
                           PC_I::Ptr& cloud_over_max_range_out, PC_I::Ptr& cloud_low_intensity, const bool return_removed_close = false, const bool return_removed_far = false,
                           const bool replace_nans = false, const bool keep_ordered = false);

  template <typename T, typename U>
  void convertDepthToCloudUnordered(const sensor_msgs::Image::ConstPtr& depth_msg, const sensor_msgs::Image::ConstPtr& intensity_msg, PC_I::Ptr& out_pc,
                                    PC_I::Ptr& removed_pc, PC_I::Ptr& cloud_low_intensity, const bool return_removed_close = false, const bool return_removed_far = false,
                                    const bool replace_nans = false);

  template <typename T, typename U>
  void convertDepthToCloudOrdered(const sensor_msgs::Image::ConstPtr& depth_msg, const sensor_msgs::Image::ConstPtr& intensity_msg, PC_I::Ptr& out_pc,
                                  PC_I::Ptr& removed_pc, PC_I::Ptr& cloud_low_intensity, const bool return_removed_close = false, const bool return_removed_far = false,
                                  const bool replace_nans = false);

  void imagePointToCloudPoint(const int x, const int y, const float depth, const float intensity, pt_XYZI& point);

  sensor_msgs::Image::Ptr applyMask(const sensor_msgs::Image::ConstPtr& depth_msg);

  void process_depth_msg(const sensor_msgs::Image::ConstPtr msg);
  void process_depth_intensity_msg(const sensor_msgs::Image::ConstPtr depth_msg, const sensor_msgs::Image::ConstPtr intensity_msg);
  void process_camera_info_msg(const sensor_msgs::CameraInfo::ConstPtr msg);
  void process_mask_msg(const sensor_msgs::Image::ConstPtr msg);

private:
  bool            initialized = false;
  ros::NodeHandle _nh;
  ros::Publisher  pub_points;
  ros::Publisher  pub_points_over_max_range;
  ros::Publisher  pub_points_low_intensity;
  ros::Publisher  pub_masked_depth;

  mrs_lib::SubscribeHandler<sensor_msgs::CameraInfo> sh_camera_info;
  mrs_lib::SubscribeHandler<sensor_msgs::Image>      sh_mask;
  mrs_lib::SubscribeHandler<sensor_msgs::Image>      sh_depth;

  std::shared_ptr<image_transport::ImageTransport>                                                intensity_it, depth_it;
  image_transport::SubscriberFilter                                                               sub_depth, sub_intensity;
  typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::Image, sensor_msgs::Image> ApproxSyncPolicy;
  typedef message_filters::sync_policies::ExactTime<sensor_msgs::Image, sensor_msgs::Image>       ExactSyncPolicy;
  typedef message_filters::Synchronizer<ApproxSyncPolicy>                                         ApproxSynchronizer;
  typedef message_filters::Synchronizer<ExactSyncPolicy>                                          ExactSynchronizer;
  std::shared_ptr<ApproxSynchronizer>                                                             approx_sync;
  std::shared_ptr<ExactSynchronizer>                                                              exact_sync;

  std::shared_ptr<CommonHandlers_t> _common_handlers;

  bool                         got_mask_msg = false;
  sensor_msgs::Image::ConstPtr mask_msg;

private:
  std::string depth_in, depth_camera_info_in, mask_in, points_out, points_over_max_range_out, masked_out;
  std::string sensor_name;

  float frequency;
  float vfov;

  bool has_camera_info = false;
  bool keep_ordered;
  bool publish_over_max_range;

  unsigned int image_width;
  unsigned int image_height;

  float replace_nan_depth;
  float image_center_x;
  float image_center_y;
  float focal_length;
  float focal_length_inverse;

  // Filters parameters
private:
  bool mask_use;

  bool        intensity_use;
  bool        intensity_sync_exact;
  std::string intensity_in_topic;
  bool        filter_low_intensity;
  double      filter_low_intensity_threshold;
  std::string points_low_intensity_out_topic;


  int downsample_step_col;
  int downsample_step_row;

  bool  range_clip_use;
  float range_clip_min;
  float range_clip_max;

  bool  voxel_grid_use;
  float voxel_grid_resolution;

  bool  radius_outlier_use;
  float radius_outlier_radius;
  int   radius_outlier_neighbors;

  bool  minimum_grid_use;
  float minimum_grid_resolution;

  bool  bilateral_use;
  float bilateral_sigma_S;
  float bilateral_sigma_R;
};

#include <pcl_tools/impl/sensors.hpp>

//}

class RemoveBelowGroundFilter;

/* class PCLFiltration //{ */
class PCLFiltration : public nodelet::Nodelet {

public:
  virtual void onInit();

private:
  bool is_initialized = false;

  mrs_lib::SubscribeHandler<sensor_msgs::PointCloud2> _sub_lidar3d;

  ros::Publisher _pub_lidar3d;
  ros::Publisher _pub_lidar3d_over_max_range;
  ros::Publisher _pub_lidar3d_below_ground;
  ros::Publisher _pub_fitted_plane;
  ros::Publisher _pub_ground_point;

  boost::recursive_mutex                               config_mutex_;
  typedef pcl_tools::pcl_filtration_dynparamConfig Config;
  typedef dynamic_reconfigure::Server<Config>          ReconfigureServer;
  boost::shared_ptr<ReconfigureServer>                 reconfigure_server_;
  /* pcl_tools::pcl_tools_dynparamConfig         last_drs_config; */

  RemoveBelowGroundFilter _filter_removeBelowGround;

  void callbackReconfigure(pcl_tools::pcl_filtration_dynparamConfig& config, uint32_t level);

  /* 3D LIDAR */
  void        lidar3dCallback(const sensor_msgs::PointCloud2::ConstPtr msg);
  std::string _lidar3d_name;
  float       _lidar3d_frequency;
  float       _lidar3d_vfov;
  bool        _lidar3d_keep_organized;
  bool        _lidar3d_republish;
  float       _lidar3d_invalid_value;
  bool        _lidar3d_dynamic_row_selection_enabled;
  bool        _lidar3d_downsample_use;

  bool     _lidar3d_rangeclip_use;
  float    _lidar3d_rangeclip_min_sq;
  float    _lidar3d_rangeclip_max_sq;
  uint32_t _lidar3d_rangeclip_min_mm;
  uint32_t _lidar3d_rangeclip_max_mm;

  bool   _lidar3d_inertclip_use;
  vec4_t _lidar3d_inertclip_min;
  vec4_t _lidar3d_inertclip_max;

  bool     _lidar3d_filter_intensity_use;
  float    _lidar3d_filter_intensity_range_sq;
  uint32_t _lidar3d_filter_intensity_range_mm;
  float    _lidar3d_filter_intensity_threshold;

  bool     _lidar3d_filter_reflectivity_use;
  float    _lidar3d_filter_reflectivity_range_sq;
  uint32_t _lidar3d_filter_reflectivity_range_mm;
  uint16_t _lidar3d_filter_reflectivity_threshold;

  int _lidar3d_row_step;
  int _lidar3d_col_step;

  bool         _lidar3d_cropbox_use;
  bool         _lidar3d_cropbox_cropinside;
  std::string  _lidar3d_cropbox_frame_id;
  vec3_t       _lidar3d_cropbox_min;
  vec3_t       _lidar3d_cropbox_max;
  unsigned int _lidar3d_dynamic_row_selection_offset = 0;

  std::shared_ptr<CommonHandlers_t> _common_handlers;

  /* Depth camera */
  std::vector<std::shared_ptr<SensorDepthCamera>> _sensors_depth_cameras;

  /* RPLidar */
  void  rplidarCallback(const sensor_msgs::LaserScan::ConstPtr msg);
  bool  _rplidar_republish;
  float _rplidar_voxel_resolution;

  /* Functions */
  template <typename PC>
  void process_msg(typename boost::shared_ptr<PC>& inout_pc);

  template <typename PC>
  void cropBoxPointCloud(boost::shared_ptr<PC>& inout_pc_ptr);

  template <typename PC>
  typename boost::shared_ptr<PC> removeCloseAndFar(typename boost::shared_ptr<PC>& inout_pc, const bool return_removed_close = false,
                                                   const bool return_removed_far = false);

  template <typename PC>
  typename boost::shared_ptr<PC> removeLowFields(typename boost::shared_ptr<PC>& inout_pc, const bool return_removed = false);

  template <typename PC>
  typename boost::shared_ptr<PC> removeCloseAndFarAndLowFields(typename boost::shared_ptr<PC>& inout_pc, const bool clip_return_removed_close = false,
                                                               const bool clip_return_removed_far = false, const bool intensity_return_removed = false);

  template <typename PC>
  void downsample(boost::shared_ptr<PC>& inout_pc_ptr, const size_t scale_row, const size_t scale_col, const size_t row_offset = 0);

  std::pair<PC::Ptr, PC::Ptr> removeCloseAndFarPointCloudXYZ(const sensor_msgs::PointCloud2::ConstPtr& msg, const bool& ret_cloud_over_max_range,
                                                             const float& min_range_sq, const float& max_range_sq);

  template <typename pt_t>
  void invalidatePoint(pt_t& point);

  template <typename PC>
  void invalidatePointsAtIndices(const pcl::IndicesConstPtr& indices, typename boost::shared_ptr<PC>& cloud);

  visualization_msgs::MarkerArray plane_visualization(const vec3_t& plane_normal, float plane_d, const std_msgs::Header& header);
};
//}

}  // namespace pcl_tools

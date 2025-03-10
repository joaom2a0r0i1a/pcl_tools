#include <PCLFiltration.h>

namespace pcl_tools
{

/* onInit() //{ */
void PCLFiltration::onInit() {

  ros::NodeHandle nh = nodelet::Nodelet::getMTPrivateNodeHandle();
  ros::Time::waitForValid();

  // Set PCL verbosity level to errors and higher
  /* pcl::console::setVerbosityLevel(pcl::console::L_ERROR); */

  /*//{ Common handlers */
  _common_handlers = std::make_shared<CommonHandlers_t>();

  // Param loader
  _common_handlers->param_loader = std::make_shared<mrs_lib::ParamLoader>(nh, "PCLFilter");

  // Transformer
  const auto uav_name           = _common_handlers->param_loader->loadParam2<std::string>("uav_name");
  _common_handlers->transformer = std::make_shared<mrs_lib::Transformer>("PCLFilter");
  _common_handlers->transformer->setDefaultPrefix(uav_name);
  _common_handlers->transformer->setLookupTimeout(ros::Duration(0.05));
  _common_handlers->transformer->retryLookupNewest(false);

  // Scope timer
  _common_handlers->param_loader->loadParam("scope_timer/enable", _common_handlers->scope_timer_enabled, false);
  const std::string time_logger_filepath = _common_handlers->param_loader->loadParam2("scope_timer/log_filename", std::string(""));
  _common_handlers->scope_timer_logger   = std::make_shared<mrs_lib::ScopeTimerLogger>(time_logger_filepath, _common_handlers->scope_timer_enabled);

  // Diagnostics message
  _common_handlers->diagnostics = std::make_shared<Diagnostics_t>(nh);

  /*//}*/

  /* 3D LIDAR */
  _common_handlers->param_loader->loadParam("lidar3d/name", _lidar3d_name, std::string("ouster"));
  _common_handlers->param_loader->loadParam("lidar3d/frequency", _lidar3d_frequency);
  _common_handlers->param_loader->loadParam("lidar3d/vfov", _lidar3d_vfov);

  _common_handlers->param_loader->loadParam("lidar3d/keep_organized", _lidar3d_keep_organized, true);
  _common_handlers->param_loader->loadParam("lidar3d/republish", _lidar3d_republish, false);
  _common_handlers->param_loader->loadParam("lidar3d/invalid_value", _lidar3d_invalid_value, std::numeric_limits<float>::quiet_NaN());
  _common_handlers->param_loader->loadParam("lidar3d/clip/range/use", _lidar3d_rangeclip_use, false);
  _common_handlers->param_loader->loadParam("lidar3d/clip/range/min", _lidar3d_rangeclip_min_sq, 0.4f);
  _common_handlers->param_loader->loadParam("lidar3d/clip/range/max", _lidar3d_rangeclip_max_sq, 100.0f);
  _lidar3d_rangeclip_min_mm = _lidar3d_rangeclip_min_sq * 1000;
  _lidar3d_rangeclip_max_mm = _lidar3d_rangeclip_max_sq * 1000;
  _lidar3d_rangeclip_min_sq *= _lidar3d_rangeclip_min_sq;
  _lidar3d_rangeclip_max_sq *= _lidar3d_rangeclip_max_sq;

  // load downsampling parameters
  _common_handlers->param_loader->loadParam("lidar3d/downsampling/dynamic_row_selection", _lidar3d_dynamic_row_selection_enabled, false);
  _common_handlers->param_loader->loadParam("lidar3d/downsampling/row_step", _lidar3d_row_step, 1);
  _common_handlers->param_loader->loadParam("lidar3d/downsampling/col_step", _lidar3d_col_step, 1);

  // load dynamic row selection
  if (_lidar3d_dynamic_row_selection_enabled && _lidar3d_row_step > 1 && _lidar3d_row_step % 2 != 0) {
    NODELET_ERROR("[PCLFiltration]: Dynamic selection of lidar rows is enabled, but `lidar_row_step` is not even and/or greater than 1. Ending nodelet.");
    ros::shutdown();
  }

  _lidar3d_downsample_use = _lidar3d_dynamic_row_selection_enabled || _lidar3d_row_step > 1 || _lidar3d_col_step > 1;
  if (_lidar3d_downsample_use) {
    NODELET_INFO("[PCLFiltration] Downsampling of input lidar data is enabled -> dynamically: %s, row step: %d, col step: %d",
                 _lidar3d_dynamic_row_selection_enabled ? "true" : "false", _lidar3d_row_step, _lidar3d_col_step);
  } else {
    NODELET_INFO("[PCLFiltration] Downsampling of input lidar data is disabled.");
  }

  // load ground removal parameters
  const bool use_ground_removal = _common_handlers->param_loader->loadParam2<bool>("lidar3d/ground_removal/use", false);
  if (use_ground_removal) {
    _common_handlers->param_loader->setPrefix("lidar3d/");
    _filter_removeBelowGround.initialize(nh, _common_handlers);
    _common_handlers->param_loader->setPrefix("");
  }

  // load cropbox parameters
  _common_handlers->param_loader->loadParam("lidar3d/cropbox/crop_inside", _lidar3d_cropbox_cropinside);
  _common_handlers->param_loader->loadParam("lidar3d/cropbox/frame_id", _lidar3d_cropbox_frame_id, {});
  _lidar3d_cropbox_frame_id = _common_handlers->transformer->resolveFrame(_lidar3d_cropbox_frame_id);
  _common_handlers->param_loader->loadMatrixStatic("lidar3d/cropbox/min", _lidar3d_cropbox_min, -std::numeric_limits<float>::infinity() * vec3_t::Ones());
  _common_handlers->param_loader->loadMatrixStatic("lidar3d/cropbox/max", _lidar3d_cropbox_max, std::numeric_limits<float>::infinity() * vec3_t::Ones());

  // by default, use the cropbox filter if any of the crop coordinates is finite
  const bool cbox_use_default = _lidar3d_cropbox_min.array().isFinite().any() || _lidar3d_cropbox_max.array().isFinite().any();
  // the user can override this behavior by setting the "lidar3d/cropbox/use" parameter
  _common_handlers->param_loader->loadParam("lidar3d/cropbox/use", _lidar3d_cropbox_use, cbox_use_default);

  _common_handlers->param_loader->loadParam("lidar3d/clip/intensity/use", _lidar3d_filter_intensity_use, false);
  _common_handlers->param_loader->loadParam("lidar3d/clip/intensity/threshold", _lidar3d_filter_intensity_threshold, std::numeric_limits<float>::max());
  _common_handlers->param_loader->loadParam("lidar3d/clip/intensity/range", _lidar3d_filter_intensity_range_sq, std::numeric_limits<float>::max());
  _lidar3d_filter_intensity_range_mm = _lidar3d_filter_intensity_range_sq * 1000;
  _lidar3d_filter_intensity_range_sq *= _lidar3d_filter_intensity_range_sq;

  _common_handlers->param_loader->loadParam("lidar3d/clip/reflectivity/use", _lidar3d_filter_reflectivity_use, false);
  _common_handlers->param_loader->loadParam("lidar3d/clip/reflectivity/range", _lidar3d_filter_reflectivity_range_sq, std::numeric_limits<float>::max());
  const int lidar3d_filter_reflectivity_threshold =
      _common_handlers->param_loader->loadParam2("lidar3d/clip/reflectivity/threshold", static_cast<int>(std::numeric_limits<uint16_t>::max()));
  _lidar3d_filter_reflectivity_threshold = static_cast<uint16_t>(lidar3d_filter_reflectivity_threshold);
  _lidar3d_filter_reflectivity_range_mm  = _lidar3d_filter_reflectivity_range_sq * 1000;
  _lidar3d_filter_reflectivity_range_sq *= _lidar3d_filter_reflectivity_range_sq;

  /* Depth cameras */
  const std::vector<std::string> depth_camera_names = _common_handlers->param_loader->loadParam2("depth/camera_names", std::vector<std::string>());
  for (const auto& name : depth_camera_names) {
    std::shared_ptr<SensorDepthCamera> cam = std::make_shared<SensorDepthCamera>();
    cam->initialize(nh, _common_handlers, uav_name, name);
    _sensors_depth_cameras.push_back(cam);
  }

  /* RPLidar */
  _common_handlers->param_loader->loadParam("rplidar/republish", _rplidar_republish, false);
  _common_handlers->param_loader->loadParam("rplidar/voxel_resolution", _rplidar_voxel_resolution, 0.0f);

  if (!_common_handlers->param_loader->loadedSuccessfully()) {
    NODELET_ERROR("[PCLFiltration]: Some compulsory parameters were not loaded successfully, ending the node");
    ros::shutdown();
  }

  if (_lidar3d_republish) {

    if (_lidar3d_row_step <= 0 || _lidar3d_col_step <= 0) {
      NODELET_ERROR("[PCLFiltration]: Downsampling row/col steps for 3D lidar must be >=1, ending nodelet.");
      ros::shutdown();
    }


    mrs_lib::SubscribeHandlerOptions shopts(nh);
    shopts.node_name            = "PCLFiltration";
    shopts.no_message_timeout   = ros::Duration(5.0);
    _sub_lidar3d                = mrs_lib::SubscribeHandler<sensor_msgs::PointCloud2>(shopts, "lidar3d_in", &PCLFiltration::lidar3dCallback, this);
    _pub_lidar3d                = nh.advertise<sensor_msgs::PointCloud2>("lidar3d_out", 1);
    _pub_lidar3d_over_max_range = nh.advertise<sensor_msgs::PointCloud2>("lidar3d_over_max_range_out", 1);
    if (_filter_removeBelowGround.used())
      _pub_lidar3d_below_ground = nh.advertise<sensor_msgs::PointCloud2>("lidar3d_below_ground_out", 1);
  }

  reconfigure_server_ = boost::make_shared<ReconfigureServer>(config_mutex_, nh);
  /* reconfigure_server_->updateConfig(last_drs_config); */
  ReconfigureServer::CallbackType f = boost::bind(&PCLFiltration::callbackReconfigure, this, _1, _2);
  reconfigure_server_->setCallback(f);

  NODELET_INFO_ONCE("[PCLFiltration] Nodelet initialized");

  is_initialized = true;
}
//}

/* callbackReconfigure() //{ */
void PCLFiltration::callbackReconfigure(Config& config, [[maybe_unused]] uint32_t level) {
  if (!is_initialized) {
    return;
  }
  NODELET_INFO("[PCLFiltration] Reconfigure callback.");

  _lidar3d_filter_intensity_use       = config.lidar3d_filter_intensity_use;
  _lidar3d_filter_intensity_threshold = config.lidar3d_filter_intensity_threshold;
  _lidar3d_filter_intensity_range_sq  = std::pow(config.lidar3d_filter_intensity_range, 2);
}
//}

/* lidar3dCallback() //{ */
void PCLFiltration::lidar3dCallback(const sensor_msgs::PointCloud2::ConstPtr msg) {

  if (!is_initialized || !_lidar3d_republish) {
    return;
  }

  if (msg->width % _lidar3d_col_step != 0 || msg->height % _lidar3d_row_step != 0) {
    NODELET_WARN(
        "[PCLFiltration] Step-based downsampling of 3D lidar data would create nondeterministic results. Data (w: %d, h: %d) with downsampling step (w: %d, "
        "h: %d) would leave some samples untouched. Skipping lidar frame.",
        msg->width, msg->height, _lidar3d_col_step, _lidar3d_row_step);
    return;
  }

  const mrs_modules_msgs::PclToolsDiagnostics::Ptr diag_msg = boost::make_shared<mrs_modules_msgs::PclToolsDiagnostics>();
  diag_msg->sensor_name                                     = _lidar3d_name;
  diag_msg->stamp                                           = msg->header.stamp;
  diag_msg->sensor_type                                     = mrs_modules_msgs::PclToolsDiagnostics::SENSOR_TYPE_LIDAR_3D;
  diag_msg->cols_before                                     = msg->width;
  diag_msg->rows_before                                     = msg->height;
  diag_msg->frequency                                       = _lidar3d_frequency;
  diag_msg->vfov                                            = _lidar3d_vfov;

  const mrs_lib::ScopeTimer timer =
      mrs_lib::ScopeTimer("PCLFiltration::lidar3dCallback", _common_handlers->scope_timer_logger, _common_handlers->scope_timer_enabled);

  const bool is_ouster_type = hasField("range", msg) && hasField("ring", msg) && hasField("t", msg);
  if (is_ouster_type) {

    NODELET_INFO_ONCE("[PCLFiltration] Received first 3D LIDAR message. Point type: ouster_ros::Point.");
    PC_OS::Ptr cloud = boost::make_shared<PC_OS>();
    pcl::fromROSMsg(*msg, *cloud);
    process_msg(cloud);
    diag_msg->cols_after = cloud->width;
    diag_msg->rows_after = cloud->height;

  } else {

    NODELET_INFO_ONCE("[PCLFiltration] Received first 3D LIDAR message. Point type: pcl::PointXYZI.");
    PC_I::Ptr cloud = boost::make_shared<PC_I>();
    pcl::fromROSMsg(*msg, *cloud);
    process_msg(cloud);
    diag_msg->cols_after = cloud->width;
    diag_msg->rows_after = cloud->height;
  }

  _common_handlers->diagnostics->publish(diag_msg);
}

template <typename PC>
void PCLFiltration::process_msg(typename boost::shared_ptr<PC>& inout_pc_ptr) {

  mrs_lib::ScopeTimer timer = mrs_lib::ScopeTimer("PCLFiltration::process_msg", _common_handlers->scope_timer_logger, _common_handlers->scope_timer_enabled);

  const size_t height_before = inout_pc_ptr->height;
  const size_t width_before  = inout_pc_ptr->width;
  const size_t points_before = inout_pc_ptr->size();

  if (_lidar3d_downsample_use) {
    // Try to keep the uppermost row (lower rows often see drone frame, so we want to prevent data loss)
    const size_t row_offset = _lidar3d_dynamic_row_selection_enabled ? _lidar3d_dynamic_row_selection_offset : _lidar3d_row_step - 1;
    downsample(inout_pc_ptr, _lidar3d_row_step, _lidar3d_col_step, row_offset);
  }

  if (_filter_removeBelowGround.used()) {
    const bool             publish_removed  = _pub_lidar3d_below_ground.getNumSubscribers() > 0;
    const typename PC::Ptr pcl_below_ground = _filter_removeBelowGround.applyInPlace(inout_pc_ptr, publish_removed);
    if (publish_removed)
      _pub_lidar3d_below_ground.publish(pcl_below_ground);
  }

  if (_lidar3d_rangeclip_use) {

    const bool publish_removed_far = _pub_lidar3d_over_max_range.getNumSubscribers() > 0;

    if (_lidar3d_filter_intensity_use || _lidar3d_filter_reflectivity_use) {
      const bool             publish_removed_fields = false;  // if anyone actually needs to publish the removed points, then implement the publisher etc.
      const typename PC::Ptr pcl_over_max_range     = removeCloseAndFarAndLowFields(inout_pc_ptr, false, publish_removed_far, publish_removed_fields);
      if (publish_removed_far) {
        _pub_lidar3d_over_max_range.publish(pcl_over_max_range);
      }
    } else {
      const typename PC::Ptr pcl_over_max_range = removeCloseAndFar(inout_pc_ptr, false, publish_removed_far);
      if (publish_removed_far) {
        _pub_lidar3d_over_max_range.publish(pcl_over_max_range);
      }
    }

  } else if (_lidar3d_filter_intensity_use || _lidar3d_filter_reflectivity_use) {

    const bool publish_removed = false;  // if anyone actually needs to publish the removed points, then implement the publisher etc.
    removeLowFields(inout_pc_ptr, publish_removed);
  }

  if (_lidar3d_cropbox_use) {
    cropBoxPointCloud(inout_pc_ptr);
  }

  // make sure that no infinite points remain in the pointcloud
  if (!_lidar3d_keep_organized) {
    const auto orig_pc   = inout_pc_ptr;
    inout_pc_ptr         = boost::make_shared<PC>();
    inout_pc_ptr->header = orig_pc->header;
    inout_pc_ptr->resize(orig_pc->size());
    size_t it = 0;
    for (const auto& pt : orig_pc->points) {
      if (pcl::isFinite(pt)) {
        inout_pc_ptr->at(it++) = pt;
      }
    }
    inout_pc_ptr->resize(it);
  }
  inout_pc_ptr->is_dense = !_lidar3d_keep_organized;

  _pub_lidar3d.publish(inout_pc_ptr);

  if (_lidar3d_dynamic_row_selection_enabled) {
    _lidar3d_dynamic_row_selection_offset++;
    _lidar3d_dynamic_row_selection_offset %= _lidar3d_row_step;
  }

  const size_t height_after = inout_pc_ptr->height;
  const size_t width_after  = inout_pc_ptr->width;
  size_t       points_after = 0;
  if (_lidar3d_keep_organized) {
    for (const auto& pt : *inout_pc_ptr)
      if (pcl::isFinite(pt))
        points_after++;
  } else
    points_after = inout_pc_ptr->size();
  NODELET_INFO_THROTTLE(
      5.0,
      "[PCLFiltration] Processed 3D LIDAR data (run time: %.1f ms; points before: %lu, after: %lu; dim before: (w: %lu, h: %lu), after: (w: %lu, h: %lu)).",
      timer.getLifetime(), points_before, points_after, width_before, height_before, width_after, height_after);
}
//}

/*//{ removeCloseAndFar() */
template <typename PC>
typename boost::shared_ptr<PC> PCLFiltration::removeCloseAndFar(typename boost::shared_ptr<PC>& inout_pc, const bool return_removed_close,
                                                                const bool return_removed_far) {
  using pt_t = typename PC::PointType;

  // Prepare pointcloud of removed points
  typename PC::Ptr removed_pc = boost::make_shared<PC>();
  removed_pc->header          = inout_pc->header;
  if (return_removed_close || return_removed_far)
    removed_pc->resize(inout_pc->size());
  size_t removed_it = 0;

  // Attempt to get the range field name's index
  const auto [range_exists, range_offset] = getFieldOffset<pt_t>("range");
  if (range_exists)
    ROS_INFO_ONCE("[PCLFiltration] Found field name \"range\" in point type, will be using range from points.");
  else
    ROS_WARN_ONCE("[PCLFiltration] Unable to find field name \"range\" in point type, will be using calculated range.");

  for (auto& point : inout_pc->points) {
    bool invalid_close = false;
    bool invalid_far   = false;

    // if the range field is available, use it
    if (range_exists) {
      // Get the range (in millimeters)
      const auto range = getFieldValue<uint32_t>(point, range_offset);
      invalid_close    = range < _lidar3d_rangeclip_min_mm;
      invalid_far      = range > _lidar3d_rangeclip_max_mm;
    }
    // otherwise, just calculate the range as the norm
    else {
      const vec3_t pt       = point.getArray3fMap();
      const float  range_sq = pt.squaredNorm();
      invalid_close         = range_sq < _lidar3d_rangeclip_min_sq;
      invalid_far           = range_sq > _lidar3d_rangeclip_max_sq;
    }

    if (invalid_close || invalid_far) {
      if ((return_removed_far && invalid_far) || (return_removed_close && invalid_close))
        removed_pc->at(removed_it++) = point;
      invalidatePoint(point);
    }
  }
  removed_pc->resize(removed_it);

  return removed_pc;
}
/*//}*/

/*//{ removeCloseAndFarAndLowFields() */
template <typename PC>
typename boost::shared_ptr<PC> PCLFiltration::removeCloseAndFarAndLowFields(typename boost::shared_ptr<PC>& inout_pc, const bool clip_return_removed_close,
                                                                            const bool clip_return_removed_far, const bool return_removed_fields) {
  using pt_t = typename PC::PointType;

  // Prepare pointcloud of removed points
  typename PC::Ptr removed_pc = boost::make_shared<PC>();
  removed_pc->header          = inout_pc->header;
  if (clip_return_removed_close || clip_return_removed_far || return_removed_fields)
    removed_pc->resize(inout_pc->size());
  size_t removed_it = 0;

  // Attempt to get the intensity field name's index
  auto [intensity_exists, intensity_offset] = getFieldOffset<pt_t>("intensity");

  if (!intensity_exists) {
    return removeCloseAndFar(inout_pc, clip_return_removed_close, clip_return_removed_far);
  }

  bool filter_intensity    = _lidar3d_filter_intensity_use;
  bool filter_reflectivity = _lidar3d_filter_reflectivity_use;

  std::size_t reflectivity_offset;

  // Attempt to get the fields' name indices
  if (filter_intensity) {
    ROS_INFO_ONCE("[PCLFiltration] Found field name \"intensity\" in point type, will be using intensity for filtering.");
    std::tie(filter_intensity, intensity_offset) = getFieldOffset<pt_t>("intensity");
  }
  if (filter_reflectivity) {
    ROS_INFO_ONCE("[PCLFiltration] Found field name \"reflectivity\" in point type, will be using reflectivity for filtering.");
    std::tie(filter_reflectivity, reflectivity_offset) = getFieldOffset<pt_t>("reflectivity");
  }

  // Attempt to get the range field name's index
  const auto [range_exists, range_offset] = getFieldOffset<pt_t>("range");
  if (range_exists) {
    ROS_INFO_ONCE("[PCLFiltration] Found field name \"range\" in point type, will be using range from points.");
  } else {
    ROS_WARN_ONCE("[PCLFiltration] Unable to find field name \"range\" in point type, will be using calculated range.");
  }

  for (auto& point : inout_pc->points) {

    bool invalid_range_close = false;
    bool invalid_range_far   = false;
    bool invalid_field       = false;

    // if the range field is available, use it
    if (range_exists)  // nevermind this condition inside a loop - the branch predictor will optimize this out easily
    {
      const auto range    = getFieldValue<uint32_t>(point, range_offset);
      invalid_range_close = range < _lidar3d_rangeclip_min_mm;
      invalid_range_far   = range > _lidar3d_rangeclip_max_mm;

      // Filter by field values
      if (filter_intensity) {
        const float intensity = getFieldValue<float>(point, intensity_offset);
        invalid_field         = intensity < _lidar3d_filter_intensity_threshold && range < _lidar3d_filter_intensity_range_mm;
      }
      if (!invalid_field && filter_reflectivity) {
        const uint16_t reflectivity = getFieldValue<uint16_t>(point, reflectivity_offset);
        invalid_field               = reflectivity < _lidar3d_filter_reflectivity_threshold && range < _lidar3d_filter_reflectivity_range_mm;
      }

    }
    // otherwise, just calculate the range as the norm
    else {
      const vec3_t pt       = point.getArray3fMap();
      const float  range_sq = pt.squaredNorm();
      invalid_range_close   = range_sq < _lidar3d_rangeclip_min_sq;
      invalid_range_far     = range_sq > _lidar3d_rangeclip_max_sq;

      // Filter by field values
      if (filter_intensity) {
        const float intensity = getFieldValue<float>(point, intensity_offset);
        invalid_field         = intensity < _lidar3d_filter_intensity_threshold && range_sq < _lidar3d_filter_intensity_range_sq;
      }
      if (!invalid_field && filter_reflectivity) {
        const uint16_t reflectivity = getFieldValue<uint16_t>(point, reflectivity_offset);
        invalid_field               = reflectivity < _lidar3d_filter_reflectivity_threshold && range_sq < _lidar3d_filter_reflectivity_range_sq;
      }
    }

    // check the invalidation condition
    if (invalid_range_close || invalid_range_far || invalid_field) {

      // check the removal condition
      if ((clip_return_removed_far && invalid_range_far) || (clip_return_removed_close && invalid_range_close) || (return_removed_fields && invalid_field)) {
        removed_pc->at(removed_it++) = point;
      }

      invalidatePoint(point);
    }
  }
  removed_pc->resize(removed_it);

  return removed_pc;
}
/*//}*/

/*//{ removeLowFields() */
template <typename PC>
typename boost::shared_ptr<PC> PCLFiltration::removeLowFields(typename boost::shared_ptr<PC>& inout_pc, const bool return_removed) {
  using pt_t = typename PC::PointType;

  // Prepare pointcloud of removed points
  typename PC::Ptr removed_pc = boost::make_shared<PC>();
  removed_pc->header          = inout_pc->header;
  if (return_removed) {
    removed_pc->resize(inout_pc->size());
  }
  size_t removed_it = 0;

  // Attempt to get the intensity field name's index
  auto [intensity_exists, intensity_offset] = getFieldOffset<pt_t>("intensity");

  if (!intensity_exists) {
    return removed_pc;
  }

  bool filter_intensity    = _lidar3d_filter_intensity_use;
  bool filter_reflectivity = _lidar3d_filter_reflectivity_use;

  std::size_t reflectivity_offset;

  // Attempt to get the fields' name indices
  if (filter_intensity) {
    ROS_INFO_ONCE("[PCLFiltration] Found field name \"intensity\" in point type, will be using intensity for filtering.");
    std::tie(filter_intensity, intensity_offset) = getFieldOffset<pt_t>("intensity");
  }
  if (filter_reflectivity) {
    ROS_INFO_ONCE("[PCLFiltration] Found field name \"reflectivity\" in point type, will be using reflectivity for filtering.");
    std::tie(filter_reflectivity, reflectivity_offset) = getFieldOffset<pt_t>("reflectivity");
  }

  // Attempt to get the range field name's index
  const auto [range_exists, range_offset] = getFieldOffset<pt_t>("range");
  if (range_exists) {
    ROS_INFO_ONCE("[PCLFiltration] Found field name \"range\" in point type, will be using range from points.");
  } else {
    ROS_WARN_ONCE("[PCLFiltration] Unable to find field name \"range\" in point type, will be using calculated range.");
  }

  for (auto& point : inout_pc->points) {

    bool invalid = false;

    // Filter by field values
    if (filter_intensity) {
      const float intensity = getFieldValue<float>(point, intensity_offset);
      invalid               = intensity < _lidar3d_filter_intensity_threshold;
    }
    if (!invalid && filter_reflectivity) {
      const uint16_t reflectivity = getFieldValue<uint16_t>(point, reflectivity_offset);
      invalid                     = reflectivity < _lidar3d_filter_reflectivity_threshold;
    }

    // check the removal condition
    if (invalid) {

      // if the range field is available, use it
      if (range_exists) {
        // Get the range (in millimeters)
        const auto range = getFieldValue<uint32_t>(point, range_offset);

        if (filter_intensity) {
          invalid = range < _lidar3d_filter_intensity_range_mm;
        }
        if (!invalid && filter_reflectivity) {
          invalid = range < _lidar3d_filter_reflectivity_range_mm;
        }
      }
      // otherwise, just calculate the range as the norm
      else {
        const vec3_t pt       = point.getArray3fMap();
        const float  range_sq = pt.squaredNorm();

        if (filter_intensity) {
          invalid = range_sq < _lidar3d_filter_intensity_range_sq;
        }
        if (!invalid && filter_reflectivity) {
          invalid = range_sq < _lidar3d_filter_reflectivity_range_sq;
        }
      }

      if (invalid) {

        if (return_removed) {
          removed_pc->at(removed_it++) = point;
        }

        invalidatePoint(point);
      }
    }
  }
  removed_pc->resize(removed_it);

  return removed_pc;
}

/*//}*/

/* cropBoxPointCloud() //{ */

template <typename PC>
void PCLFiltration::cropBoxPointCloud(boost::shared_ptr<PC>& inout_pc) {
  pcl::CropBox<typename PC::PointType> cb;

  if (!_lidar3d_cropbox_frame_id.empty()) {
    ros::Time stamp;
    pcl_conversions::fromPCL(inout_pc->header.stamp, stamp);
    const auto tf_opt = _common_handlers->transformer->getTransform(inout_pc->header.frame_id, _lidar3d_cropbox_frame_id, stamp);
    if (tf_opt.has_value()) {
      const Eigen::Affine3d tf = tf2::transformToEigen(tf_opt.value().transform);
      cb.setTransform(tf.cast<float>());
    } else {
      ROS_WARN_STREAM_THROTTLE(1.0, "[PCLFiltration]: Could not find pointcloud transformation (from \""
                                        << inout_pc->header.frame_id << "\" to \"" << _lidar3d_cropbox_frame_id << "\") ! Not applying CropBox filter.");
      return;
    }
  }

  vec4_t cb_min;
  cb_min.head<3>() = _lidar3d_cropbox_min;
  cb_min.w()       = -std::numeric_limits<float>::infinity();

  vec4_t cb_max;
  cb_max.head<3>() = _lidar3d_cropbox_max;
  cb_max.w()       = std::numeric_limits<float>::infinity();

  cb.setKeepOrganized(_lidar3d_keep_organized);
  cb.setNegative(_lidar3d_cropbox_cropinside);

  cb.setMin(cb_min);
  cb.setMax(cb_max);
  cb.setInputCloud(inout_pc);
  cb.filter(*inout_pc);
}

//}

/* downsample() //{ */

template <typename PC>
void PCLFiltration::downsample(boost::shared_ptr<PC>& inout_pc_ptr, const size_t scale_row, const size_t scale_col, const size_t row_offset) {
  using pt_t = typename PC::PointType;

  const auto [ring_exists, ring_offset] = getFieldOffset<pt_t>("ring");
  const uint8_t scale_row_uint8         = static_cast<const uint8_t>(scale_row);

  const size_t height_before = inout_pc_ptr->height;
  const size_t width_before  = inout_pc_ptr->width;
  const size_t height_after  = height_before / scale_row;
  const size_t width_after   = width_before / scale_col;

  const boost::shared_ptr<PC> pc_out = boost::make_shared<PC>(width_after, height_after);

  size_t r = 0;

  for (size_t j = row_offset; j < height_before; j += scale_row) {

    size_t c = 0;
    for (size_t i = 0; i < width_before; i += scale_col) {
      pt_t& point = inout_pc_ptr->at(i, j);

      if (ring_exists) {
        const uint8_t ring = getFieldValue<uint8_t>(point, ring_offset);
        pcl::setFieldValue<pt_t, uint8_t>(point, ring_offset, ring / scale_row_uint8);
      }

      pc_out->at(c++, r) = point;
    }

    r++;
  }

  pc_out->header   = inout_pc_ptr->header;
  pc_out->height   = height_after;
  pc_out->width    = width_after;
  pc_out->is_dense = inout_pc_ptr->is_dense;

  inout_pc_ptr = pc_out;
}

//}

/*//{ removeCloseAndFarPointCloudXYZ() */
std::pair<PC::Ptr, PC::Ptr> PCLFiltration::removeCloseAndFarPointCloudXYZ(const sensor_msgs::PointCloud2::ConstPtr& msg, const bool& ret_cloud_over_max_range,
                                                                          const float& min_range_sq, const float& max_range_sq) {

  // Convert to pcl object
  PC::Ptr cloud                = boost::make_shared<PC>();
  PC::Ptr cloud_over_max_range = boost::make_shared<PC>();
  pcl::fromROSMsg(*msg, *cloud);

  unsigned int j          = 0;
  unsigned int k          = 0;
  unsigned int cloud_size = cloud->points.size();

  if (ret_cloud_over_max_range) {
    cloud_over_max_range->header = cloud->header;
    cloud_over_max_range->points.resize(cloud_size);
  }

  for (unsigned int i = 0; i < cloud_size; i++) {

    float range_sq =
        cloud->points.at(i).x * cloud->points.at(i).x + cloud->points.at(i).y * cloud->points.at(i).y + cloud->points.at(i).z * cloud->points.at(i).z;

    if (range_sq < min_range_sq) {
      continue;

    } else if (range_sq <= max_range_sq) {

      cloud->points.at(j++) = cloud->points.at(i);

    } else if (ret_cloud_over_max_range && cloud->points.at(i).getArray3fMap().allFinite()) {

      cloud_over_max_range->points.at(k++) = cloud->points.at(i);
    }
  }

  // Resize both clouds
  if (j != cloud_size) {
    cloud->points.resize(j);
  }
  cloud->height   = 1;
  cloud->width    = static_cast<uint32_t>(j);
  cloud->is_dense = false;


  if (ret_cloud_over_max_range) {
    if (k != cloud_size) {
      cloud_over_max_range->points.resize(k);
    }
    cloud_over_max_range->header   = cloud->header;
    cloud_over_max_range->height   = 1;
    cloud_over_max_range->width    = static_cast<uint32_t>(k);
    cloud_over_max_range->is_dense = false;
  }

  return std::make_pair(cloud, cloud_over_max_range);
}
/*//}*/

/*//{ invalidatePoint() */
template <typename pt_t>
void PCLFiltration::invalidatePoint(pt_t& point) {
  point.x = _lidar3d_invalid_value;
  point.y = _lidar3d_invalid_value;
  point.z = _lidar3d_invalid_value;
}
/*//}*/

/*//{ invalidatePointsAtIndices() */
template <typename PC>
void PCLFiltration::invalidatePointsAtIndices(const pcl::IndicesConstPtr& indices, typename boost::shared_ptr<PC>& cloud) {
  for (auto it = indices->begin(); it != indices->end(); it++) {
    invalidatePoint(cloud->at(*it));
  }
}
/*//}*/

}  // namespace pcl_tools

PLUGINLIB_EXPORT_CLASS(pcl_tools::PCLFiltration, nodelet::Nodelet);

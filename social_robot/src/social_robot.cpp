#include <ros/ros.h>
#include <std_srvs/Empty.h>
#include <stereo_msgs/DisparityImage.h>
#include <social_robot/RegionOfInterests.h>
#include <omp.h>

#include "kinect_proxy.h"
#include "CvUtils.h"
#include "RosUtils.h"
#include "social_robot_constants.h"

#include "particle_filter/StateData.h"
#include "particle_filter/hist.h"
#include "particle_filter/filter.h"

using namespace std;
using namespace cv;
using namespace sensor_msgs;

namespace enc = image_encodings;

// for publications
RosUtils ros_utils;
social_robot::RegionOfInterests depth_pub_rois;
ros::Publisher depth_pub;

ros::ServiceClient rgb_update_client;
ros::ServiceClient depth_update_client;

std_srvs::Empty empty;

// for subscribtions
ros::Subscriber rgb_rois_sub;
ros::Subscriber depth_rois_sub;

double track_thr = 75;
double confidence_level_thr = 0.50;
double detection_confidence_thr = 0.80;
int num_particles = 300;
int rgb_framenum = 0;
int rgb_update_rate = 15;
int depth_framenum = 0;
int depth_update_rate = 15;
int high_frequency = 2;
int low_frequency = 12;
int external_update_rate = high_frequency;

Mat image_disparity;
Mat image_depth;
Mat image_rgb;

bool stand_alone = false;
bool use_colour = false;
bool use_depth = false;

vector<StateData> state_datas;
vector<vector <Rect> > results;

CvUtils cv_utils;

void check_update_rate_frequency ( void )
{
  if ( state_datas.empty() )
    {
      if ( external_update_rate == low_frequency )
        {
          external_update_rate = high_frequency;
          ros::NodeHandle nh;
          nh.setParam ( "/social_robot/depth/update_rate", external_update_rate );
          depth_update_client.call<std_srvs::Empty> ( empty );
          nh.setParam ( "/social_robot/rgb/update_rate", external_update_rate );
          rgb_update_client.call<std_srvs::Empty> ( empty );
        }
    }
  else if ( external_update_rate == high_frequency )
    {
      external_update_rate = low_frequency;
      ros::NodeHandle nh;
      nh.setParam ( "/social_robot/depth/update_rate", external_update_rate );
      depth_update_client.call<std_srvs::Empty> ( empty );
      nh.setParam ( "/social_robot/rgb/update_rate", external_update_rate );
      rgb_update_client.call<std_srvs::Empty> ( empty );
    }
}

void publish_data ( void )
{  
  vector<Rect> detected_faces ( state_datas.size() );
  for ( unsigned int i = 0; i < state_datas.size(); i++ )
    {
      detected_faces[i] = state_datas[i].get_target_position();
    }
  results.push_back(detected_faces);  
  vector<RegionOfInterest> rosrois = ros_utils.cvrects2rosrois ( detected_faces );
  depth_pub_rois.rois.swap ( rosrois );
  depth_pub.publish ( depth_pub_rois );
}

void do_tracking ( void )
{
  int i = 0;
  for ( vector<StateData>::iterator it = state_datas.begin(); it != state_datas.end(); )
    {
      it->set_image_depth ( image_disparity );
      it->image = image_rgb;
      it->tracking ( 0.02 );

      if ( it->filter->confidence() == 0.0 )
        {
          cout << "TOO BIG\n";
          it = state_datas.erase ( it );
        }
      else if ( it->detection_confidence < ( detection_confidence_thr / 2 ) )
        {
          if ( cv_utils.is_there_face_rgb ( image_rgb, it->get_target_position() ) )
            {
              it->detection_confidence = 1.0;
              it++;
            }
          else
            {
              cout << i << " ### conf " << it->filter->confidence() << " detec: " << it->detection_confidence << endl;
              it = state_datas.erase ( it );
            }
        }
      else if ( it->filter->confidence() > confidence_level_thr )
        {
          it++;
        }
      else
        {
          cout << i << " *** conf " << it->filter->confidence() << " detec: " << it->detection_confidence << endl;
          it = state_datas.erase ( it );
        }
      i++;
    }

  // if there was nothing to track we request more detection
  if ( !stand_alone )
    {
      check_update_rate_frequency();
    }  

  publish_data();
}

void data_association ( vector<Rect> &faces )
{
  for ( unsigned int i = 0; i < faces.size(); i++ )
    {
//       Point3f face_centre = cv_utils.get_rect_centre_3d ( faces[i], image_depth );
      Point face_centre = cv_utils.get_rect_centre ( faces[i] );
      bool associated = false;
      for ( unsigned int j = 0; j < state_datas.size(); j++ )
        {
//           if ( state_datas[j].is_associated )
//             {
//               continue;
//             }
//           Point3f track_centre = cv_utils.get_rect_centre_3d ( state_datas[j].get_target_position(), image_depth );
          Point track_centre = cv_utils.get_rect_centre ( state_datas[j].get_target_position() );
          double euc_dis = cv_utils.euclidean_distance ( face_centre, track_centre );

//           cout << "Euc " << euc_dis << ", " << j << endl;
          if ( euc_dis < track_thr )
            {
              associated = true;
              state_datas[j].detection_confidence = 1.0;
              state_datas[j].is_associated = true;
//               state_datas[j].update_target_histogram ( image_rgb, image_disparity, faces[i] );
              break;
            }
        }
      if ( !associated )
        {
          StateData state_data;
          state_data.initialise ( num_particles, image_rgb, faces[i], image_disparity, HIST_HS );
          state_datas.push_back ( state_data );
        }
    }
  faces.clear();
}

bool update_param_cb ( std_srvs::Empty::Request&, std_srvs::Empty::Response& )
{
  ROS_INFO ( "Updating parameter of social_robot" );
  cv_utils.write_results_to_file ( "arash", results );

  double confidence_level_thr_tmp = confidence_level_thr;
  double detection_confidence_thr_tmp = detection_confidence_thr;
  double track_thr_tmp = track_thr;

  ros::NodeHandle nh;

  nh.param ( "/social_robot/track/confidence_level_thr", confidence_level_thr_tmp, confidence_level_thr_tmp );
  nh.param ( "/social_robot/track/detection_confidence_thr", detection_confidence_thr_tmp, detection_confidence_thr_tmp );
  nh.param ( "/social_robot/track/track_thr", track_thr_tmp, track_thr_tmp );

  confidence_level_thr = confidence_level_thr_tmp;
  detection_confidence_thr = detection_confidence_thr_tmp;
  track_thr = track_thr_tmp;

  return true;
}

void depth_cb ( const ImageConstPtr& msg )
{
  try
    {
      image_depth = cv_bridge::toCvCopy ( msg )->image;
    }
  catch ( cv_bridge::Exception& e )
    {
      ROS_ERROR ( "cv_bridge exception: %s", e.what() );
      return;
    }
}

void rgb_cb ( const ImageConstPtr& msg )
{
  try
    {
      image_rgb = cv_bridge::toCvCopy ( msg, enc::BGR8 )->image;

      // if we want to perform detection ourselves
      if ( use_colour )
        {
          rgb_framenum++;
          if ( rgb_framenum >= rgb_update_rate )
            {
              rgb_framenum = 0;
              vector<Rect> rgb_faces = cv_utils.detect_face_rgb ( image_rgb );
              data_association ( rgb_faces );
            }
        }

      do_tracking();
    }
  catch ( cv_bridge::Exception& e )
    {
      ROS_ERROR ( "cv_bridge exception: %s", e.what() );
      return;
    }
}

void disparity_cb ( const stereo_msgs::DisparityImageConstPtr& msg )
{
  try
    {
      image_disparity = cv_bridge::toCvCopy ( msg->image )->image;
      image_disparity.convertTo ( image_disparity, CV_8UC1 );

      // if we want to perform detection ourselves
      if ( use_depth )
        {
          depth_framenum++;
          if ( image_depth.empty() )
            {
              return;
            }
          if ( depth_framenum >= depth_update_rate )
            {
              depth_framenum = 0;
              vector<Rect> depth_faces = cv_utils.detect_face_depth ( image_depth, image_disparity );
              data_association ( depth_faces );
            }
        }

    }
  catch ( cv_bridge::Exception& e )
    {
      ROS_ERROR ( "cv_bridge exception: %s", e.what() );
      return;
    }
}

void rgb_rois_cb ( const social_robot::RegionOfInterests &msg )
{
  vector<RegionOfInterest> rois = msg.rois;
  vector<Rect> rgb_faces = ros_utils.rosrois2cvrects ( rois );
  data_association ( rgb_faces );
}

void depth_rois_cb ( const social_robot::RegionOfInterests &msg )
{
  vector<RegionOfInterest> rois = msg.rois;
  vector<Rect> depth_faces = ros_utils.rosrois2cvrects ( rois );
  data_association ( depth_faces );
}

void parse_command_line ( int argc, char** argv )
{
  int c = -1;
  while ( ( c = getopt ( argc, argv, "cds" ) ) != -1 )
    {
      switch ( c )
        {
        case 'c':
          use_colour = true;
          break;
        case 'd':
          use_depth = true;
          break;
        case 's':
          stand_alone = true;
          break;
        default:
          cerr << "Usage: " << argv[0] << " [-c] [-d] [-s] " << endl << endl;
          cerr << "\t-c uses colour images in detection." << endl;
          cerr << "\t-d uses depth images in detection." << endl;
          cerr << "\t-s runs this application as stand alone, detection and tracking together." << endl;
          exit ( 1 );
        }
    }
}

int main ( int argc, char **argv )
{
  parse_command_line ( argc, argv );

  ros::init ( argc, argv, "social_robot_track" );
  ros::NodeHandle nh;
//   ros::MultiThreadedSpinner spinner ( 0 );

  // to register the depth
  nh.setParam ( "/camera/driver/depth_registration", true );

  nh.setParam ( "/social_robot/track/confidence_level_thr", confidence_level_thr );
  nh.setParam ( "/social_robot/track/detection_confidence_thr", detection_confidence_thr );
  nh.setParam ( "/social_robot/track/track_thr", track_thr );

  // subscribtions
  ros::ServiceServer update_srv = nh.advertiseService ( "/social_robot/track/update", update_param_cb );
  ros::Subscriber disparity_sub = nh.subscribe ( "/camera/depth/disparity", 1, disparity_cb );
  ros::Subscriber depth_sub = nh.subscribe ( "/camera/depth/image_raw", 1, depth_cb );
  ros::Subscriber rgb_sub = nh.subscribe ( "/camera/rgb/image_color", 1, rgb_cb );

  if ( !stand_alone )
    {
      rgb_rois_sub = nh.subscribe ( "/social_robot/rgb/rois", 1, rgb_rois_cb );
      depth_rois_sub = nh.subscribe ( "/social_robot/depth/rois", 1, depth_rois_cb );
      rgb_update_client = nh.serviceClient<std_srvs::Empty> ( "/social_robot/rgb/update" );
      depth_update_client = nh.serviceClient<std_srvs::Empty> ( "/social_robot/depth/update" );
    }

  // publications
  depth_pub = nh.advertise<social_robot::RegionOfInterests> ( "/social_robot/track/rois", 1 );

//   spinner.spin();
  ros::spin();

  return 0;
}
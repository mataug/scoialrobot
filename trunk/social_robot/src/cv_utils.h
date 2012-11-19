#ifndef CV_UTILS
#define CV_UTILS

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include <iostream>
#include <vector>
#include "particle_filter/StateData.h"

using namespace std;
using namespace cv;

const Scalar RED = Scalar ( 0, 0, 255 );
const Scalar BLUE = Scalar ( 255, 0, 0 );
const Scalar GREEN = Scalar ( 0, 255, 0 );
const Scalar CYAN = Scalar ( 255, 255, 0 );
const Scalar MAGENTA = Scalar ( 255, 0, 255 );
const Scalar YELLOW = Scalar ( 0, 255, 255 );
const Scalar WHITE = Scalar ( 255, 255, 255 );
const Scalar BLACK = Scalar ( 0, 0, 0 );

Mat rgb2bw ( Mat im_rgb );
Mat preprocessing ( Mat image );
void get_non_zeros ( Mat img, Mat prob, vector<Point3f> *points, Point pdiff = Point ( 0, 0 ), double scale = 1 );

void draw_rgb_faces ( Mat &img, vector<Rect> faces );
void draw_tracking_faces ( Mat &img, vector<StateData> state_datas );
void draw_depth_faces ( Mat &img, vector<Rect> faces );

#endif

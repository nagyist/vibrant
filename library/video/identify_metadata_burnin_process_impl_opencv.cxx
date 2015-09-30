/*ckwg +5
 * Copyright 2011 by Kitware, Inc. All Rights Reserved. Please refer to
 * KITWARE_LICENSE.TXT for licensing information, or contact General Counsel,
 * Kitware, Inc., 28 Corporate Drive, Clifton Park, NY 12065.
 */

#include "identify_metadata_burnin_process_impl_opencv.h"

#include <utilities/unchecked_return_value.h>
#include <utilities/log.h>

#include <vil/vil_convert.h>
#include <vil/vil_fill.h>

#include <vul/vul_sprintf.h>

#include <opencv/cxcore.h>
#include <opencv/cvaux.h>
#include <opencv/highgui.h>

#define HAVE_COLOR_IMAGE

namespace cv
{
static bool operator<( const cv::Point& a, const cv::Point& b );
}

namespace vidtk
{


bool
identify_metadata_burnin_process_impl_opencv
::set_params( config_block const& blk )
{
  const bool ret = identify_metadata_burnin_process_impl::set_params( blk );

  if ( !ret )
  {
    return ret;
  }

  vcl_vector< vcl_pair< vcl_string, vcl_string > >::const_iterator fnames;
  vcl_deque<cv::Rect> mem(text_memory_, cv::Rect());
  text_temporal_sum_ = vcl_vector<int>(templates_.size(), 0);

  for ( fnames = templates_.begin(); fnames != templates_.end(); ++fnames )
  {
    cv::Mat orig1;
    cv::Mat orig2;
    cv::Mat conv1;
    cv::Mat conv2;

    orig1 = cv::imread( fnames->first, 1 );
    orig1.convertTo( conv1, CV_8UC3 );

    orig2 = cv::imread( fnames->second, 0 );
    orig2.convertTo( conv2, CV_8UC1 );

    if ( orig1.size() != orig2.size() )
    {
      log_error( "Error: Images are not the same size." );
      continue;
    }

    template_imgs_.push_back( vcl_make_pair( conv1, conv2 ) );

    text_temporal_mem_.push_back(mem);
  }

  if ( !edge_template_.empty() )
  {
    cv::Mat orig;
    cv::Mat conv;

    orig = cv::imread( edge_template_, 1 );
    orig.convertTo( conv, CV_8UC3 );

    text_temporal_sum_.push_back( 0 );
    template_imgs_.push_back( vcl_make_pair( conv, cv::Mat() ) );
    text_temporal_mem_.push_back(mem);
  }

  return true;
}


bool
identify_metadata_burnin_process_impl_opencv
::initialize()
{
  invalid_image_ = true;
  return identify_metadata_burnin_process_impl::initialize();
}


// ----------------------------------------------------------------
/** Main processing step.
 *
 *
 */
bool
identify_metadata_burnin_process_impl_opencv
::step()
{
  if( invalid_image_ )
  {
    return false;
  }

  invalid_image_ = true;

  if( byte_mask_.size() != input_image_.size() )
  {
    byte_mask_ = vil_image_view<vxl_byte>( input_image_.ni(),
                                          input_image_.nj() );
  }
  vil_fill( byte_mask_, vxl_byte(0) );

  cv::Mat input( input_image_.nj(), input_image_.ni(),
                 ( input_image_.nplanes() == 1 ) ? CV_8UC1 : CV_8UC3,
                 input_image_.top_left_ptr() );
  cv::Mat end_mask( byte_mask_.nj(), byte_mask_.ni(),
                    CV_8UC1, byte_mask_.top_left_ptr() );

  const int w = input_image_.ni();
  const int h = input_image_.nj();
  const int nw = w * roi_ratio_;
  const int nh = roi_aspect_ ? ( nw * roi_aspect_ ) : ( h * roi_ratio_ );

  identify_metadata_burnin_process_impl_opencv::metadata md;
  cv::Rect roi = cv::Rect( (w - nw) / 2, (h - nh) / 2, nw, nh ) + cv::Point( off_center_x_, off_center_y_ );
  cv::Rect roi_buf = ( roi - cv::Point( line_width_, line_width_ ) ) + cv::Size( 2 * line_width_, 2 * line_width_ );
  cv::Mat edges = cv::Mat::zeros( h, w, CV_8UC1 );
  cv::Mat edge_view = edges( roi_buf );
  cv::Mat input_view = input( roi_buf );

  vcl_vector< cv::Mat > bands;
  cv::split( input_view, bands );

  for ( size_t i = 0; i < bands.size(); ++i )
  {
    cv::Mat tmp;
    cv::threshold(bands[i], tmp, 128, 255, cv::THRESH_BINARY);
    bitwise_or( edge_view, tmp, edge_view );
  }


  if ( 0.0 <= cross_threshold_ && cross_threshold_ <= 1.0)
  {
    detect_cross( edges, md.center );
  }
  if ( 0.0 <= bracket_threshold_ && bracket_threshold_ <= 1.0 )
  {
    detect_bracket( edges, roi, md.brackets );

    if ( 0.0 <= rectangle_threshold_ && rectangle_threshold_ <= 1.0 )
    {
      detect_rectangle( edges, md.brackets, md.rectangle );
    }
  }
  if ( 0.0 <= text_threshold_ && text_threshold_ <= 1.0 )
  {
    detect_text( input_view, edge_view, roi_buf, md.text );
  }

  draw_metadata_mask( end_mask, md );

  if ( verbose_ )
  {
    // write out the values of aspect ratios and jitters that were actually
    // used in the final detections.  This information can be used to better
    // set the parameters.
    log_detection_stats( md );
  }

  // force allocation of new memory for output
  mask_ = vil_image_view<bool>(NULL);
  vil_convert_cast< vxl_byte, bool >( byte_mask_, mask_ );

  return true;
}


void
identify_metadata_burnin_process_impl_opencv
::set_input_image( vil_image_view<vxl_byte> const& img )
{
  if( img.nplanes() != 1 && img.nplanes() != 3 )
  {
    log_error( "Input image does not have either 1 or 3 channels.\n" );
    invalid_image_ = true;
    return;
  }

  input_image_ = vil_image_view<vxl_byte>( img.ni(), img.nj(), 1, img.nplanes() );
  vil_copy_reformat( img, input_image_ );

  if( ( input_image_.nplanes() != 1 && input_image_.planestep() != 1 ) ||
      input_image_.istep() != input_image_.nplanes() ||
      input_image_.jstep() != ( input_image_.ni() * input_image_.nplanes() ) )
  {
    log_error( "Input image memory is not aligned for OpenCV.\n" );
    invalid_image_ = true;
    return;
  }

  invalid_image_ = false;
}


void
identify_metadata_burnin_process_impl_opencv
::detect_cross( const cv::Mat& edge_image, cv::Point& center )
{
  const int w = input_image_.ni();
  const int h = input_image_.nj();
  double score = 0;
  cv::Mat cross = cv::Mat::zeros( h, w, CV_8UC1 );
  cv::Mat buffer;
  cv::Point cntr = cv::Point( w / 2, h / 2 ) + cv::Point( off_center_x_, off_center_y_ );

  draw_cross( cross, cntr, 255, line_width_ );
  const int cross_count = cv::countNonZero( cross );
  draw_cross( cross, cntr, 0, line_width_ );

  for ( int i = -off_center_jitter_; i <= off_center_jitter_; ++i )
  {
    for ( int j = -off_center_jitter_; j <= off_center_jitter_; ++j )
    {
      const cv::Point ct = cntr + cv::Point( j, i );

      const cv::Rect bb( ct - cv::Point( cross_length_x_ + cross_gap_x_,
                                         cross_length_y_ + cross_gap_y_ ),
                         cv::Size( 2 * ( cross_length_x_ + cross_gap_x_ ),
                                   2 * ( cross_length_y_ + cross_gap_y_ ) ) );
      draw_cross( cross, ct, 255, line_width_ );
      cv::Mat edgev = edge_image(bb);
      cv::Mat crossv = cross(bb);
      cv::bitwise_and( edgev, crossv, buffer );
      draw_cross( cross, ct, 0, line_width_ );

      const int edge_count = cv::countNonZero( buffer );

      const double s = double(edge_count) / double(cross_count);

      if ( s > score )
      {
        center = ct;
        score = s;
      }
    }
  }

  if ( score < cross_threshold_ )
  {
    center = cv::Point( 0, 0 );
  }
}

typedef vcl_pair< double, cv::Rect > scored_rect;
static bool cmp_scored_rects( const scored_rect& a, const scored_rect& b );
static bool close_rects( const cv::Rect& base, const cv::Rect& cmp, double min_ratio, double max_ratio );
static bool is_non_zero( const cv::Mat& img, const cv::Point& pt );

void
identify_metadata_burnin_process_impl_opencv
::detect_bracket( const cv::Mat& edge_image, const cv::Rect& roi, vcl_vector< cv::Rect >& brackets )
{
  const int w = input_image_.ni();
  const int h = input_image_.nj();
  const double aspect = roi_aspect_ ? roi_aspect_ : ( double(h) / double(w) );

  vcl_vector< vcl_pair< double, cv::Rect > > scored_rects;
  vcl_map< cv::Point, vcl_pair< int, int > > count_tl;
  vcl_map< cv::Point, vcl_pair< int, int > > count_tr;
  vcl_map< cv::Point, vcl_pair< int, int > > count_bl;
  vcl_map< cv::Point, vcl_pair< int, int > > count_br;

  const int min_bracket_width = min_roi_ratio_ * w;
  for ( int nw = roi.width; nw >= min_bracket_width; )
  {
    double score = 0;
    cv::Rect rect;

    double best = 0;

    for ( int a = -bracket_aspect_jitter_; a <= bracket_aspect_jitter_; ++a )
    {
      const int nh = nw * aspect + a + 0.5;
      for ( int i = -off_center_jitter_; i <= off_center_jitter_; ++i )
      {
        for ( int j = -off_center_jitter_; j <= off_center_jitter_; ++j )
        {
          cv::Rect rt = cv::Rect( (w - nw + 1) / 2 + j, (h - nh + 1) / 2 + i, nw, nh ) + cv::Point( off_center_x_, off_center_y_ );

          int edge_count = 0;
          int bracket_count = 0;

          const cv::Point tl = rt.tl();
          const cv::Point tr = tl + cv::Point( rt.width, 0 );
          const cv::Point br = rt.br();
          const cv::Point bl = tl + cv::Point( 0, rt.height );

          if ( count_tl.find( tl ) == count_tl.end() )
          {
            vcl_vector< cv::Point > points;

            draw_bracket_tl( points, rt, line_width_ );

            const int hits = vcl_count_if( points.begin(), points.end(), boost::bind( is_non_zero, edge_image, _1 ) );
            count_tl[ tl ] = vcl_make_pair( hits, points.size() );

            edge_count += hits;
            bracket_count += points.size();
          }
          else
          {
            const vcl_pair< int, int >& pr = count_tl[ tl ];

            edge_count += pr.first;
            bracket_count += pr.second;
          }
          if ( count_tr.find( tr ) == count_tr.end() )
          {
            vcl_vector< cv::Point > points;

            draw_bracket_tr( points, rt, line_width_ );

            const int hits = vcl_count_if( points.begin(), points.end(), boost::bind( is_non_zero, edge_image, _1 ) );
            count_tr[ tr ] = vcl_make_pair( hits, points.size() );

            edge_count += hits;
            bracket_count += points.size();
          }
          else
          {
            const vcl_pair< int, int >& pr = count_tr[ tr ];

            edge_count += pr.first;
            bracket_count += pr.second;
          }
          if ( count_bl.find( bl ) == count_bl.end() )
          {
            vcl_vector< cv::Point > points;

            draw_bracket_bl( points, rt, line_width_ );

            const int hits = vcl_count_if( points.begin(), points.end(), boost::bind( is_non_zero, edge_image, _1 ) );
            count_bl[ bl ] = vcl_make_pair( hits, points.size() );

            edge_count += hits;
            bracket_count += points.size();
          }
          else
          {
            const vcl_pair< int, int >& pr = count_bl[ bl ];

            edge_count += pr.first;
            bracket_count += pr.second;
          }
          if ( count_br.find( br ) == count_br.end() )
          {
            vcl_vector< cv::Point > points;

            draw_bracket_br( points, rt, line_width_ );

            const int hits = vcl_count_if( points.begin(), points.end(), boost::bind( is_non_zero, edge_image, _1 ) );
            count_br[ br ] = vcl_make_pair( hits, points.size() );

            edge_count += hits;
            bracket_count += points.size();
          }
          else
          {
            const vcl_pair< int, int >& pr = count_br[ br ];

            edge_count += pr.first;
            bracket_count += pr.second;
          }

          const double s = double(edge_count) / double(bracket_count);

          if ( s > best )
          {
            best = s;
          }

          if ( s > score )
          {
            rect = rt;
            score = s;
          }
        }
      }
    }

    if ( best > bracket_threshold_ )
    {
      nw -= 2;
    }
    else
    {
      nw -= 2 * line_width_;
    }

    if ( score > bracket_threshold_ )
    {
      scored_rects.push_back( vcl_make_pair( score, rect ) );
    }
  }

  vcl_sort( scored_rects.begin(), scored_rects.end(), cmp_scored_rects );

  vcl_vector< vcl_pair< double, cv::Rect > >::const_reverse_iterator srit;
  for ( srit = scored_rects.rbegin(); srit != scored_rects.rend(); ++srit )
  {
    vcl_vector<cv::Rect>::const_iterator i = vcl_find_if( brackets.begin(), brackets.end(), boost::bind( close_rects, _1, srit->second, 0.8, 1.25 ) );

    if ( i == brackets.end() )
    {
      brackets.push_back( srit->second );
    }
  }
}

void
identify_metadata_burnin_process_impl_opencv
::detect_rectangle( const cv::Mat& edge_image, const vcl_vector< cv::Rect >& brackets, cv::Rect& rect )
{
  const int w = input_image_.ni();
  const int h = input_image_.nj();
  double score = 0;
  cv::Mat rectangle = cv::Mat::zeros( h, w, CV_8UC1 );
  cv::Mat buffer;

  for ( size_t i = 0; i < brackets.size(); ++i )
  {
    vcl_vector< cv::Point > points;

    draw_rectangle( points, brackets[i], line_width_ );

    const int rectangle_count = points.size();
    const int edge_count = vcl_count_if( points.begin(), points.end(), boost::bind( is_non_zero, edge_image, _1 ) );
    const double s = double(edge_count) / double(rectangle_count);

    if ( s > score )
    {
      rect = brackets[i];
      score = s;
    }
  }

  if ( score < rectangle_threshold_ )
  {
    rect = cv::Rect( 0, 0, 0, 0 );
  }
}


void
identify_metadata_burnin_process_impl_opencv
::detect_text( const cv::Mat & input_image, const cv::Mat& edge_image, const cv::Rect& main_roi, vcl_vector< cv::Rect >& text )
{
  static int frame = 0;

  cv::Mat result;
  cv::Rect froi = main_roi;
  froi.x = 0;
  froi.y = 0;

  for ( size_t i = 0; i < template_imgs_.size(); ++i )
  {
    double colorVal;
    cv::Point colorLoc;
#ifdef HAVE_EDGE_IMAGE
    double edgeVal;
    cv::Point edgeLoc;
#endif

    cv::Rect roi;
    bool seen_before = false;
    cv::Rect rect_previous;

    if ( text_temporal_sum_[i] < text_memory_ * text_consistency_threshold_ )
    {
      roi = froi;
    }
    else
    {
      seen_before = true;
      vcl_deque<cv::Rect>::const_reverse_iterator rit = text_temporal_mem_[i].rbegin();
      for ( ; rit != text_temporal_mem_[i].rend(); ++rit )
      {
        if ( rit->area() )
        {
          rect_previous = *rit;
          roi = cv::Rect( rit->x - text_range_, rit->y - text_range_,
                          rit->width + 2 * text_range_, rit->height + 2 * text_range_ );
          break;
        }
      }

      // Crop the roi to fit in the main roi.
      roi &= froi;
    }

    cv::Mat res;

    cv::matchTemplate( input_image( roi ), template_imgs_[i].first, res, CV_TM_CCORR_NORMED );

    cv::minMaxLoc( res, NULL, &colorVal, NULL, &colorLoc );

#ifdef HAVE_EDGE_IMAGE
    cv::Mat edgev = edge_image(roi);

    result = cv::Mat();
    cv::matchTemplate( edgev, template_imgs_[i].second, result, CV_TM_CCORR_NORMED );
    cv::minMaxLoc( result, NULL, &edgeVal, NULL, &edgeLoc );
#endif

    if ( verbose_ )
    {
      vcl_cerr << "Full ROI (" << frame << ") "
               << main_roi.size().width << "x" << main_roi.size().height
               << "+" << main_roi.tl().x << "+" << main_roi.tl().y << vcl_endl;
      if ( seen_before )
      {
        vcl_cerr << "Using previous results..."
                 << roi.size().width << "x" << roi.size().height
                 << "+" << roi.tl().x << "+" << roi.tl().y << vcl_endl;
      }
      vcl_cerr << "Score (" << frame << "): color: " << colorVal
#ifdef HAVE_EDGE_IMAGE
               << " edge: " << edgeVal
#endif
               << vcl_endl;
    }

    bool found = false;
    cv::Point loc;

#ifdef HAVE_EDGE_IMAGE
    if ( cv::norm( colorLoc - edgeLoc ) < max_text_dist_ )
#endif
    {
      double const score =
#ifdef HAVE_EDGE_IMAGE
        vcl_min( colorVal, edgeVal )
#else
        colorVal
#endif
        ;

      if ( seen_before )
      {
        if ( score > text_repeat_threshold_ )
        {
#ifdef HAVE_EDGE_IMAGE
          if ( cv::norm( rect_previous.tl() - colorLoc ) >
               cv::norm( rect_previous.tl() - edgeLoc ) )
          {
            loc = edgeLoc;
          }
          else
#endif
          {
            loc = colorLoc;
          }

          found = true;
        }
      }
      else
      {
        if ( score > text_threshold_ )
        {
#ifdef HAVE_EDGE_IMAGE
          if ( colorVal < edgeVal )
          {
            loc = edgeLoc;
          }
          else
#endif
          {
            loc = colorLoc;
          }

          found = true;
        }
      }
    }

    if ( write_intermediates_ )
    {
      cv::Rect const det = cv::Rect( loc + roi.tl(), template_imgs_[i].first.size() );

      vul_sprintf const color_fmt("md-opencv-color-det-%04d-%04d.png", i, frame);
      cv::imwrite( color_fmt, input_image( det ) );

      vul_sprintf const edge_fmt("md-opencv-edge-det-%04d-%04d.png", i, frame);
      cv::imwrite( edge_fmt, edge_image( det ) );
    }

    cv::Rect r;

    if ( found )
    {
      r = cv::Rect( loc.x + roi.x + main_roi.x, loc.y + roi.y + main_roi.y,
                    template_imgs_[i].first.rows, template_imgs_[i].first.cols );

      ++text_temporal_sum_[i];
    }

    text.push_back( r );
    if ( r.area() )
    {
      text_temporal_mem_[i].push_back( cv::Rect( loc.x + roi.x, loc.y + roi.y,
                                                 template_imgs_[i].first.rows, template_imgs_[i].first.cols ) );
    }
    else
    {
      text_temporal_mem_[i].push_back( r );
    }

    if ( text_temporal_mem_[i][0].area() )
    {
      --text_temporal_sum_[i];
    }
    text_temporal_mem_[i].pop_front();
  }

  ++frame;
}


void
identify_metadata_burnin_process_impl_opencv
::draw_cross( cv::Mat& img, const cv::Point& center, double clr, int width )
{
  const cv::Scalar color = cv::Scalar( clr );

  cv::line( img, center + cv::Point(  cross_gap_x_, 0 ), center + cv::Point(  cross_gap_x_ + cross_length_x_, 0 ), color, width );
  cv::line( img, center + cv::Point( -cross_gap_x_, 0 ), center + cv::Point( -cross_gap_x_ - cross_length_x_, 0 ), color, width );
  cv::line( img, center + cv::Point( 0,  cross_gap_y_ ), center + cv::Point( 0,  cross_gap_y_ + cross_length_y_ ), color, width );
  cv::line( img, center + cv::Point( 0, -cross_gap_y_ ), center + cv::Point( 0, -cross_gap_y_ - cross_length_y_ ), color, width );
}

static void draw_line( vcl_vector< cv::Point >& pts, const cv::Point& p1, const cv::Point& p2, int line_width );

void
identify_metadata_burnin_process_impl_opencv
::draw_bracket( cv::Mat& img, const cv::Rect& rect, vxl_uint_8 clr, int width )
{
  vcl_vector< cv::Point > points;

  draw_bracket_tl( points, rect, width );
  draw_bracket_tr( points, rect, width );
  draw_bracket_bl( points, rect, width );
  draw_bracket_br( points, rect, width );

  vcl_vector< cv::Point >::const_iterator pt = points.begin();
  vcl_vector< cv::Point >::const_iterator end = points.end();

  for ( ; pt != end; ++pt )
  {
    img.at<vxl_uint_8>( *pt ) = clr;
  }
}


void
identify_metadata_burnin_process_impl_opencv
::draw_bracket_tl( vcl_vector< cv::Point >& pts, const cv::Rect& rect, int width )
{
  const cv::Point dx = cv::Point( bracket_length_x_, 0 );
  const cv::Point dy = cv::Point( 0, bracket_length_y_ );

  const cv::Point tl = rect.tl();
  draw_line( pts, tl + dy, tl - cv::Point( 0, ( width - 1 ) / 2 ), width );
  draw_line( pts, tl + cv::Point( width / 2, 0 ), tl + dx, width );
}

void
identify_metadata_burnin_process_impl_opencv
::draw_bracket_tr( vcl_vector< cv::Point >& pts, const cv::Rect& rect, int width )
{
  const cv::Point dx = cv::Point( bracket_length_x_, 0 );
  const cv::Point dy = cv::Point( 0, bracket_length_y_ );

  const cv::Point tr = rect.tl() + cv::Point( rect.width, 0 );
  draw_line( pts, tr - dx, tr + cv::Point( width / 2, 0 ), width );
  draw_line( pts, tr + cv::Point( 0, width / 2 + 1 ), tr + dy, width );
}


void
identify_metadata_burnin_process_impl_opencv
::draw_bracket_bl( vcl_vector< cv::Point >& pts, const cv::Rect& rect, int width )
{
  const cv::Point dx = cv::Point( bracket_length_x_, 0 );
  const cv::Point dy = cv::Point( 0, bracket_length_y_ );

  const cv::Point bl = rect.tl() + cv::Point( 0, rect.height );
  draw_line( pts, bl + dx, bl - cv::Point( width / 2, 0 ), width );
  draw_line( pts, bl - cv::Point( 0, width / 2 + 1), bl - dy, width );
}


void
identify_metadata_burnin_process_impl_opencv
::draw_bracket_br( vcl_vector< cv::Point >& pts, const cv::Rect& rect, int width )
{
  const cv::Point dx = cv::Point( bracket_length_x_, 0 );
  const cv::Point dy = cv::Point( 0, bracket_length_y_ );

  const cv::Point br = rect.br();
  draw_line( pts, br - dy, br + cv::Point( 0, ( width - 1 ) / 2 ), width );
  draw_line( pts, br - cv::Point( width / 2, 0 ), br - dx, width );
}


void
identify_metadata_burnin_process_impl_opencv
::draw_rectangle( cv::Mat& img, const cv::Rect& rect, vxl_uint_8 clr, int width )
{
  vcl_vector< cv::Point > points;

  draw_rectangle( points, rect, width );

  vcl_vector< cv::Point >::const_iterator pt = points.begin();
  vcl_vector< cv::Point >::const_iterator end = points.end();

  for ( ; pt != end; ++pt )
  {
    img.at<vxl_uint_8>( *pt ) = clr;
  }
}


void
identify_metadata_burnin_process_impl_opencv
::draw_rectangle( vcl_vector< cv::Point >& pts, const cv::Rect& rect, int width )
{
  const cv::Point dx = cv::Point( rect.width, 0 );
  const cv::Point dy = cv::Point( 0, rect.height );

  draw_line( pts, rect.tl() + cv::Point( width / 2, 0 ), rect.tl() + dx, width );
  draw_line( pts, rect.tl() + dx + cv::Point( 0, width / 2 ), rect.br(), width );
  draw_line( pts, rect.br() - cv::Point( width / 2, 0 ), rect.tl() + dy, width );
  draw_line( pts, rect.tl() + dy - cv::Point( 0, width / 2 ), rect.tl(), width );
}


void
identify_metadata_burnin_process_impl_opencv
::draw_text( cv::Mat& img, const cv::Rect& rect, vxl_uint_8 clr )
{
  for ( int r = rect.tl().y; r <= rect.br().y; ++r )
  {
    for ( int c = rect.tl().x; c <= rect.br().x; ++c )
    {
      img.at<vxl_uint_8>( r, c ) = clr;
    }
  }

  // TODO: Actually draw the text
}


void
identify_metadata_burnin_process_impl_opencv
::draw_metadata_mask( cv::Mat& img, const metadata& md )
{
  if ( md.center.x != 0 && md.center.y != 0 )
  {
    draw_cross( img, md.center, 255, draw_line_width_ );
  }
  vcl_for_each( md.brackets.begin(), md.brackets.end(), boost::bind( &identify_metadata_burnin_process_impl_opencv::draw_bracket, this, img, _1, 255, draw_line_width_ ) );
  if ( md.rectangle.x != 0 && md.rectangle.y != 0 )
  {
    draw_rectangle( img, md.rectangle, 255, draw_line_width_ );
  }
  vcl_for_each( md.text.begin(), md.text.end(), boost::bind( &identify_metadata_burnin_process_impl_opencv::draw_text, this, img, _1, 255 ) );
}


void
identify_metadata_burnin_process_impl_opencv
::log_detection_stats( const metadata& md )
{
  const int w = input_image_.ni();
  const int h = input_image_.nj();
  const double roi_aspect = roi_aspect_ ? roi_aspect_ : ( double(h) / double(w) );
  cv::Point cntr = cv::Point( w / 2, h / 2 ) + cv::Point( off_center_x_, off_center_y_ );

  if ( md.center.x != 0 && md.center.y != 0 )
  {
    cv::Point jitter = cntr - md.center;
    log_info("Cross jitter: [" << jitter.x <<", "<< jitter.y<< "]\n" );
  }
  for (unsigned i=0; i<md.brackets.size(); ++i)
  {
    double aspect = double(md.brackets[i].height) / md.brackets[i].width;
    cv::Point bcntr = (md.brackets[i].tl() + md.brackets[i].br());
    double aspect_jitter = md.brackets[i].height - roi_aspect * md.brackets[i].width - 0.5;
    bcntr.x /= 2;
    bcntr.y /= 2;
    cv::Point pos_jitter = cntr - bcntr;
    log_info("Bracket aspect: " << aspect  << "\n" );
    log_info("Bracket aspect jitter: " << aspect_jitter << "\n" );
    log_info("Bracket position jitter: [" << pos_jitter.x <<", "<< pos_jitter.y<< "]\n" );
  }
}


bool cmp_scored_rects( const scored_rect& a, const scored_rect& b )
{
  return (a.first < b.first);
}


bool close_rects( const cv::Rect& base, const cv::Rect& cmp, double min_ratio, double max_ratio )
{
  const double ba = base.area();
  const double ca = cmp.area();

  return ((ba * min_ratio) < ca) && (ca < (ba * max_ratio));
}


bool is_non_zero( const cv::Mat& img, const cv::Point& pt )
{
  return ( img.at< vxl_uint_8 >( pt ) != 0 );
}


void draw_line( vcl_vector< cv::Point >& pts, const cv::Point& p1, const cv::Point& p2, int line_width )
{
  if ( !line_width )
  {
    return;
  }

  log_assert( p1.x == p2.x || p1.y == p2.y,
              "Algorithm for non-straight line not implemented." );

  if ( p1.x == p2.x )
  {
    const int dy = ( p1.y < p2.y ) ? 1 : -1;

    for ( int j = p1.y; j != p2.y + dy; j += dy )
    {
      for ( int i = ( p1.x - ( line_width - ( dy > 0 ) ) / 2 ); i <= ( p1.x + ( line_width - ( dy < 0 ) ) / 2 ); ++i )
      {
        pts.push_back( cv::Point( i, j ) );
      }
    }
  }
  else if ( p1.y == p2.y )
  {
    const int dx = ( p1.x < p2.x ) ? 1 : -1;

    for ( int i = p1.x; i != p2.x + dx; i += dx )
    {
      for ( int j = ( p1.y - ( line_width - ( dx > 0 ) ) / 2 ); j <= ( p1.y + ( line_width - ( dx < 0 ) ) / 2 ); ++j )
      {
        pts.push_back( cv::Point( i, j ) );
      }
    }
  }
}

} // end namespace vidtk

bool cv::operator<( const cv::Point& a, const cv::Point& b )
{
  return ( ( a.x <  b.x ) ||
         ( ( a.x == b.x ) && ( a.y <  b.y ) ) );
}

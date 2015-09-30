/*ckwg +5
 * Copyright 2010 by Kitware, Inc. All Rights Reserved. Please refer to
 * KITWARE_LICENSE.TXT for licensing information, or contact General Counsel,
 * Kitware, Inc., 28 Corporate Drive, Clifton Park, NY 12065.
 */

#include "image_difference_no_bkgnd_process.h"

#include <vcl_cmath.h>
#include <vcl_algorithm.h>
#include <utilities/log.h>
#include <utilities/unchecked_return_value.h>
#include <vil/algo/vil_threshold.h>
#include <vil/vil_math.h>
#include <vil/algo/vil_histogram.h>

namespace vidtk
{

template < class PixType >
image_difference_no_bkgnd_process<PixType>
::image_difference_no_bkgnd_process( vcl_string const& name )
  : fg_image_process<PixType>( name, "image_difference_no_bkgnd_process" ),
  diff_threshold_percentage_( 0 ),
  diff_threshold_( 0 ),
  dynamic_range_filename_(),
  dynamic_range_file_(),
  n_means_( 0 ),
  init_length_( 0 )
{
  config_.add_parameter( "diff_threshold_percentage", "0" ,
    "An intensity threshold specified in [0,Inf] interval." );
  config_.add_parameter( "dynamic_range:filename", "",
    "A two column file that contains min & max grayscale range in current frame."
    " Each frame has it's own value pair. Assuming 16-bit values." );
  config_.add_parameter( "n_means", "3","Multiplier of the mean value computed." );
  config_.add_parameter( "init_length", "60",
    "Frame number used to compute the mean (background) intensity from." );
}

template < class PixType >
image_difference_no_bkgnd_process<PixType>
::~image_difference_no_bkgnd_process() 
{
}

template < class PixType >
bool
image_difference_no_bkgnd_process<PixType>
::set_params( config_block const& blk )
{
  try
  {
    diff_threshold_percentage_ = blk.get<double>( "diff_threshold_percentage" );
    dynamic_range_filename_ = blk.get<vcl_string>( "dynamic_range:filename" );    
    n_means_ = blk.get<double>( "n_means" );    
    init_length_ = blk.get<unsigned>( "init_length" );
  }
  catch( unchecked_return_value& e )
  {
    log_error( this->name() << ": set_params failed: "
               << e.what() << "\n" );
    return false;
  }

  config_.update( blk );
  return true;
}

template < class PixType >
config_block
image_difference_no_bkgnd_process<PixType>
::params() const
{
  return config_;
}

template < class PixType >
bool
image_difference_no_bkgnd_process<PixType>
::initialize()
{
  if( ! dynamic_range_filename_.empty() )
  {
    dynamic_range_file_.open( dynamic_range_filename_.c_str() );
    if( ! dynamic_range_file_ )
    {
      log_error( "Couldn't open \"" << dynamic_range_filename_
                 << "\" for reading\n" );
      return false;
    }  

    //Using 'unsigned short' because we know that we are 
    //(for now) working with 16-bit data. 
    diff_threshold_ = static_cast<unsigned short>( 
      std::numeric_limits<unsigned short>::max() * diff_threshold_percentage_ );
  }
  else
  {
    diff_threshold_ = static_cast<unsigned short>( 
      std::numeric_limits<PixType>::max() * diff_threshold_percentage_ );
  }

  return true;
}

template < class PixType >
vil_image_view<bool> const&
image_difference_no_bkgnd_process<PixType>
::fg_image() const
{
  return diff_image_;
}

template < class PixType >
vil_image_view<PixType>
image_difference_no_bkgnd_process<PixType>
::bg_image() const
{
  return bg_img_;
}


template < class PixType >
void
image_difference_no_bkgnd_process<PixType>
::set_source_image( vil_image_view<PixType> const& src_image )
{
  src_img_ = &src_image;
}


template < class PixType >
bool
image_difference_no_bkgnd_process<PixType>
::step()
{
  static unsigned frame_number_ctr_ = 0;

  if( src_img_ == NULL )
  {
    vcl_cerr << this->name() << ": the source image is not set.\n";
    return false;
  }


  double mean, var;
  vil_math_mean_and_variance( mean, var, *src_img_, 0);
  PixType updated_threshold;

  unsigned short min_16bit, max_16bit;
  static unsigned short mean_16bit = 0;

  if( !dynamic_range_filename_.empty() )
  {
    // Recompute threshold at every frame based on the 16-bit dynamic 
    // range read in from the file.
    if( ! (dynamic_range_file_ >> min_16bit >> max_16bit) )
    {
      vcl_cerr<< "Failed to read from file: " 
              << dynamic_range_filename_<< "\n";
      return false;
    }

    if( frame_number_ctr_ == 0 )
    {
      mean_16bit = conv_8bit_2_16bit( PixType(mean), min_16bit, max_16bit );
    }
    else if( frame_number_ctr_ > 0 && frame_number_ctr_ < init_length_ )
    {
      mean_16bit += conv_8bit_2_16bit( PixType(mean), min_16bit, max_16bit );
      mean_16bit /= 2;
    }
    frame_number_ctr_++;

    unsigned short gated_threshold = diff_threshold_ + static_cast<unsigned short>(mean_16bit * n_means_);
    gated_threshold = vcl_max( gated_threshold, min_16bit );
    gated_threshold = vcl_min( gated_threshold, max_16bit );
    
    updated_threshold = conv_16bit_2_8bit( gated_threshold, min_16bit, max_16bit );
  }
  else
  {
    updated_threshold = static_cast<PixType>( diff_threshold_ );
  }  

  vil_threshold_above( *src_img_, diff_image_, updated_threshold );

#ifdef DEBUG_IMG_DIFF
  vcl_cout<< this->name() 
    << ":mean_16bit:"<<mean_16bit
    <<":min_16bit:"<<min_16bit
    <<":max_16bit:"<<max_16bit
    <<":mean_8bit:"<<unsigned(conv_16bit_2_8bit( mean_16bit, min_16bit, max_16bit ))
    <<vcl_endl;

  PixType median;
  vil_math_median(median, *src_img_, 0);
  vcl_cout<< this->name() 
          <<":mean0:"<<static_cast<unsigned int>(mean)
          << ":var0:"<<static_cast<unsigned int>(var)
          << ":stdev0:"<<static_cast<unsigned int>(sqrt(var))
          << ":median0:"<<static_cast<unsigned int>(median)
          <<"\n";

  vcl_cout<< this->name() <<":threshold 8bit="
          << unsigned(updated_threshold) 
          << " 16bit="<<conv_8bit_2_16bit( updated_threshold, min_16bit, max_16bit )<<vcl_endl;

  PixType min_8bit, max_8bit;
  vil_math_value_range(*src_img_, min_8bit, max_8bit);
  vcl_cout<< this->name()
          <<":min:"<<static_cast<unsigned int>(min_8bit)
          <<":max:"<<static_cast<unsigned int>(max_8bit)
          <<"\n";

  vcl_vector< double > histo;
  vil_histogram_byte(*src_img_, histo);
  vcl_cout<< this->name()
          <<":histo:";
  
  for(unsigned i=0;i<histo.size();i++)
  {
    vcl_cout<<histo[i]<<" ";
  }
  vcl_cout<<"\n";
#endif 

  // Mark the images as "used"
  src_img_ = NULL;

  return true;
}

} // namespace vidtk

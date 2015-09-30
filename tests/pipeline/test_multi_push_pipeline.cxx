/*ckwg +5
 * Copyright 2010 by Kitware, Inc. All Rights Reserved. Please refer to
 * KITWARE_LICENSE.TXT for licensing information, or contact General Counsel,
 * Kitware, Inc., 28 Corporate Drive, Clifton Park, NY 12065.
 */

#include "sample_nodes.h"

#include <pipeline/async_pipeline.h>
#include <testlib/testlib_test.h>

// Put everything in an anonymous namespace so that different tests
// won't conflict.
namespace {

using namespace vidtk;


void
test_simple_multi_push()
{
  async_pipeline p;
  process_smart_pointer< numbers > nums( new numbers( "numbers", 3 ) );
  process_smart_pointer< multi_push > mpush( new multi_push( "multi_push", 2 ) );
  process_smart_pointer< collect_value > collect( new collect_value( "collect" ) );

  p.add( nums );
  p.add( mpush );
  p.add( collect );

  p.connect( nums->value_port(),
             mpush->set_value_port() );
  p.connect( mpush->value_port(),
             collect->set_value_port() );

  TEST( "Pipeline initialize", p.initialize(), true );
  bool run_status = p.run();
  TEST( "Run", run_status, true);
  TEST( "Number of steps", collect->values_.size(), 9 );
  TEST( "Value of step 1", collect->values_[0], 0 );
  TEST( "Value of step 2", collect->values_[1], 1 );
  TEST( "Value of step 3", collect->values_[2], 2 );
  TEST( "Value of step 4", collect->values_[3], 1 );
  TEST( "Value of step 5", collect->values_[4], 2 );
  TEST( "Value of step 6", collect->values_[5], 3 );
  TEST( "Value of step 7", collect->values_[6], 2 );
  TEST( "Value of step 8", collect->values_[7], 3 );
  TEST( "Value of step 9", collect->values_[8], 4 );
  for( unsigned i=0; i<collect->values_.size(); ++i )
  {
    vcl_cout << collect->values_[i] << " ";
  }
  vcl_cout << vcl_endl;
}


void
test_nested_multi_push()
{
  typedef simple_sp< async_pipeline, multi_push > multi_push_sp;
  async_pipeline p;
  async_pipeline* nested_p = new async_pipeline(async_pipeline::ENABLE_STATUS_FOWARDING);
  process_smart_pointer< numbers > nums( new numbers( "numbers", 3 ) );
  process_smart_pointer< multi_push > mpush( new multi_push( "multi_push", 2 ) );
  process_smart_pointer< multi_push_sp >
      mpush_sp( new multi_push_sp("multi_push_sp", mpush, nested_p) );
  process_smart_pointer< collect_value > collect( new collect_value( "collect" ) );

  p.add( nums );
  p.add( mpush_sp );
  p.add( collect );

  p.connect( nums->value_port(),
             mpush_sp->set_value_port() );
  p.connect( mpush_sp->value_port(),
             collect->set_value_port() );

  TEST( "Pipeline initialize", p.initialize(), true );
  bool run_status = p.run();
  TEST( "Run", run_status, true);
  TEST( "Number of steps", collect->values_.size(), 9 );
  TEST( "Value of step 1", collect->values_[0], 0 );
  TEST( "Value of step 2", collect->values_[1], 1 );
  TEST( "Value of step 3", collect->values_[2], 2 );
  TEST( "Value of step 4", collect->values_[3], 1 );
  TEST( "Value of step 5", collect->values_[4], 2 );
  TEST( "Value of step 6", collect->values_[5], 3 );
  TEST( "Value of step 7", collect->values_[6], 2 );
  TEST( "Value of step 8", collect->values_[7], 3 );
  TEST( "Value of step 9", collect->values_[8], 4 );
  for( unsigned i=0; i<collect->values_.size(); ++i )
  {
    vcl_cout << collect->values_[i] << " ";
  }
  vcl_cout << vcl_endl;
}



} // end anonymous namespace

int test_multi_push_pipeline( int /*argc*/, char* /*argv*/[] )
{
  testlib_test_start( "test_multi_push_pipelines" );

  vcl_cout << "--------------------------------\n"
           << "Testing simple async multi-push\n"
           << "--------------------------------" << vcl_endl;
  test_simple_multi_push();

  vcl_cout << "-------------------------------\n"
           << "Testing nested async multi-push\n"
           << "-------------------------------" << vcl_endl;
  test_nested_multi_push();

  return testlib_test_summary();
}

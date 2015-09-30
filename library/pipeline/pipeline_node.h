/*ckwg +5
 * Copyright 2010 by Kitware, Inc. All Rights Reserved. Please refer to
 * KITWARE_LICENSE.TXT for licensing information, or contact General Counsel,
 * Kitware, Inc., 28 Corporate Drive, Clifton Park, NY 12065.
 */

#ifndef pipeline_node_h_
#define pipeline_node_h_

#include <vcl_string.h>
#include <vcl_vector.h>
#include <boost/function.hpp>

#include <pipeline/pipeline_edge.h>
#include <process_framework/process.h>
#include <utilities/config_block.h>


namespace RightTrack { class BoundedEvent; }

// TODO: Determine if this is the behavior we want in the long run
#define VIDTK_SKIP_IS_PROPOGATED_OVER_OPTIONAL_CONNECTIONS

namespace vidtk
{

class pipeline;

class pipeline_node
{
public:
  typedef boost::function< process::step_status () >  execute_function_type;
  typedef boost::function< bool () >  fail_recovery_function_type;
  typedef boost::function< bool () >  initialize_function_type;
  typedef boost::function< config_block () >  params_function_type;
  typedef boost::function< bool (config_block const&) >  set_params_function_type;


  pipeline_node( process* p );

  pipeline_node( process::pointer p );

  pipeline_node();

  virtual ~pipeline_node();


  pipeline_node& set_execute_func( execute_function_type const& f )
  {
    execute_ = f;
    return *this;
  }

  pipeline_node& set_recover_func( fail_recovery_function_type const& f )
  {
    recover_ = f;
    return *this;
  }


  pipeline_node& set_params_func( params_function_type const& f )
  {
    params_ = f;
    return *this;
  }


  pipeline_node& set_set_params_func( set_params_function_type const& f )
  {
    set_params_ = f;
    return *this;
  }


  pipeline_node& set_initialize_func( initialize_function_type const& f )
  {
    initialize_ = f;
    return *this;
  }


  pipeline_node& set_name( vcl_string const& s )
  {
    name_ = s;
    return *this;
  }

  vcl_string const& name() const
  {
    return name_;
  }

  /// @{

  /// The type of a node represents the functionality provided by the
  /// node.  For example, there could be multiple instances of a node
  /// with type "connected_components", each node with a different
  /// name (e.g. "raw_mod", "post_track_mod").
  ///
  /// The point of the type is to identify to the user what processing
  /// is done by the node.
  vcl_string const& type() const
  {
    return type_;
  }

  pipeline_node& set_type( vcl_string const& s )
  {
    type_ = s;
    return *this;
  }

  /// @}

  virtual void output_detailed_report( vcl_string const& indent = "" );

  void* id() const
  {
    return id_;
  }

  double steps_per_second() const;

  bool is_output_node() const;

  config_block get_params() const;

  process::pointer get_process();

protected:
  friend class pipeline;
  friend struct pipeline_edge;

  virtual process::step_status execute();

  /// log the execution status of this node.
  /// \param edge_status is the status of the incoming edges
  /// \param node_status is the status after calling \c execute()
  virtual void log_status(process::step_status edge_status,
                          process::step_status node_status);

  virtual bool initialize();

  /// Reset the pipeline node after a failure so that it can be restarted
  virtual bool reset();

  // node takes ownership of the edge
  void add_incoming_edge( pipeline_edge* e );
  // node does not take ownership of the edge
  void add_outgoing_edge( pipeline_edge* e );

  void set_last_execute_to_failed();
  void set_last_execute_to_skip();
  void set_last_execute_to_success();

  process::step_status last_execute_state() const;

  void set_print_detailed_report( bool print );

private:
  void append_params( config_block& all_params );

  bool set_params( config_block const& all_params );


protected:
  void* id_;
  process::pointer process_;
  vcl_string name_;
  vcl_string type_;
  execute_function_type execute_;
  fail_recovery_function_type recover_;
  params_function_type params_;
  set_params_function_type set_params_;
  initialize_function_type initialize_;
  vcl_vector<pipeline_edge*> incoming_edges_;
  vcl_vector<pipeline_edge*> outgoing_edges_;

  /// Records the success of the execution during this step and the
  /// last step.
  process::step_status last_execute_state_;

  /// Does this node have any outgoing edges?
  bool is_sink_node_;

  /// Has the "user" explicitly marked this node as an output node?
  enum ternary_value_type { UNKNOWN, YES, NO };
  ternary_value_type is_output_node_;

  double elapsed_ms_;
  unsigned long step_count_;

  static unsigned unnamed_count_;
  RightTrack::BoundedEvent * m_event;
};


} // end namespace vidtk


#endif // pipeline_node_h_

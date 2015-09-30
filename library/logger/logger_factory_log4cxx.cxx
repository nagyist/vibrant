/*ckwg +5
 * Copyright 2010 by Kitware, Inc. All Rights Reserved. Please refer to
 * KITWARE_LICENSE.TXT for licensing information, or contact General Counsel,
 * Kitware, Inc., 28 Corporate Drive, Clifton Park, NY 12065.
 */


#include <logger/logger_factory_log4cxx.h>

#include <logger/vidtk_logger_log4cxx.h>
#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/basicconfigurator.h>

#include <logger/logger.h>
#include <vul/vul_file.h>
#include <vcl_cstdlib.h>


namespace vidtk {
namespace logger_ns {


// ----------------------------------------------------------------
/**
 *
 *
 */
logger_factory_log4cxx
::logger_factory_log4cxx()
 : logger_factory("log4cxx")
{

}


logger_factory_log4cxx
::~logger_factory_log4cxx()
{

}



// ----------------------------------------------------------------
/** Initialize underlying logger as log4cxx.
 *
 * Need to configure log4cxx. Find the config file, read the config
 * file.
 *
 * @retval 0 - initialized o.k.
 * @retval 1 - error initializing
 */
int logger_factory_log4cxx
::initialize(vcl_string const & cfg)
{
  vcl_string config_file (cfg);

  // If no file, then look in environment
  if (config_file.empty())
  {
    // Try the environemnt variable if no config file yet
    const char * cfg_file_p = vcl_getenv("LOG4CXX_CONFIGURATION");
    if (0 != cfg_file_p)
    {
      config_file = cfg_file_p;
    }
  }

  // If still no file, look for usual suspects
  if (config_file.empty())
  {
    if (vul_file::exists ("log4cxx.properties") )
    {
      config_file = "log4cxx.properties";
    }
  }

  if ( ! config_file.empty())
  {
    ::log4cxx::PropertyConfigurator::configure(config_file);
  }
  else
  {
    ::log4cxx::BasicConfigurator::configure();
  }

  return (0);
}


// ----------------------------------------------------------------
/** Get logger object.
 *
 * The named logger is returned as generated by the underlying logger
 * implementation.
 */
vidtk_logger_sptr logger_factory_log4cxx
::get_logger( char const* name )
{
  return new vidtk_logger_log4cxx(name);
}


} // end namespace
} // end namespace
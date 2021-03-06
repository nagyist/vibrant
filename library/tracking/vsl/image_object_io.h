/*ckwg +5
 * Copyright 2010 by Kitware, Inc. All Rights Reserved. Please refer to
 * KITWARE_LICENSE.TXT for licensing information, or contact General Counsel,
 * Kitware, Inc., 28 Corporate Drive, Clifton Park, NY 12065.
 */

#ifndef vidtk_image_object_io_h_
#define vidtk_image_object_io_h_

/// \file
///
/// \brief Binary IO functions for vidtk::timestamp.

#include <vsl/vsl_fwd.h>
#include <tracking/image_object.h>

void vsl_b_write( vsl_b_ostream& s, vidtk::image_object const& o );

void vsl_b_read( vsl_b_istream& s, vidtk::image_object& o );

void vsl_b_write( vsl_b_ostream& os, vidtk::image_object const* p );

void vsl_b_read( vsl_b_istream& is, vidtk::image_object*& p );


#endif // vidtk_image_object_io_h_

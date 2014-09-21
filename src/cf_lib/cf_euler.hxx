/*
 *  Crossfeed Client Project
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL v2 (or later at your choice)
 *
 *   Revision 1.0.0  2012/10/17 00:00:00  geoff
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation; either version 2 of the
 *   License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, US
 *
 */

// Module: cf_euler.hxx
// Converion of linear velocity to speed, in knot
// and the position and orientation vectors to heading, pitch and roll
#ifndef _CF_EULER_HXX_
#define _CF_EULER_HXX_

#ifndef M_PI
#define M_PI       3.14159265358979323846
#endif

#ifndef RAD2DEG
#define RAD2DEG ( 180.0 / M_PI )
#endif
#ifndef DEG2RAD
#define DEG2RAD ( M_PI / 180.0 )
#endif


#ifndef USE_SIMGEAR
extern double cf_norm( Point3D &p3d ); // { return sqrt(cf_dot_prod(p3d, p3d)); }
extern void euler_get( double lat, double lon, double ox, double oy, double oz,
    double *phead, double *ppitch, double *proll );
extern char *get_point3d_stg2(Point3D *p);
#endif // #ifndef USE_SIMGEAR

#endif // _CF_EULER_HXX_
// eof - cf_euler.hxx

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

// Module: cf_euler.cxx
// Converion of linear velocity to speed, in knot
// and the position and orientation vectors to heading, pitch and roll
#ifdef _MSC_VER
#pragma warning ( disable : 4789 ) 
#endif
#include <stdlib.h> // abs() in unix
#include <stdio.h>
#include <string.h> // for memcpy(), ...
#include <math.h>
#include "fg_geometry.hxx"
#include "sprtf.hxx"
#include "cf_euler.hxx"

#ifdef _SPRTF_HXX_
#define SPRTF sprtf
#else // !_SPRTF_HXX_
#define SPRTF printf
#endif // _SPRTF_HXX_ y/n

#ifndef USE_SIMGEAR

#define fgs_rad2deg(a) (a * SG_RADIANS_TO_DEGREES)

#define ESPRTF SPRTF
// #define ESPRTF

//    dot(const SGVec3<T>& v1, const SGVec3<T>& v2)
// { return v1(0)*v2(0) + v1(1)*v2(1) + v1(2)*v2(2); }
inline double cf_dot_prod( Point3D &p1, Point3D &p2 ) 
{
    return p1.GetX()*p2.GetX() + p1.GetY()*p2.GetY() + p1.GetZ()*p2.GetZ();
}

inline double cf_norm( Point3D &p3d ) { return sqrt(cf_dot_prod(p3d, p3d)); }

/* ==============================================================
sub euler_get($$$$$$$$) {
    my ($lat, $lon, $ox, $oy, $oz, $rhead, $rpitch, $rroll) = @_;
    # FGMultiplayMgr::ProcessPosMsg
    my @angleAxis = ($ox,$oy,$oz);
    #push(@angleAxis, $ox);
    #push(@angleAxis, $oy);
    #push(@angleAxis, $oz);
    #print "angleAxis ";
    #show_vec3(\@angleAxis);
    my $recOrient = fromAngleAxis(\@angleAxis); # ecOrient = SGQuatf::fromAngleAxis(angleAxis);
    #print "ecOrient ";
    #show_quat($recOrient);
    # FGAIMultiplayer::update 
    my ($lat_rad, $lon_rad);
    $lat_rad = $lat * $SGD_DEGREES_TO_RADIANS;
    $lon_rad = $lon * $SGD_DEGREES_TO_RADIANS;
    my $qEc2Hl = fromLonLatRad($lon_rad, $lat_rad);
    #print "fromLonLatRad ";
    #show_quat($qEc2Hl);
    my $con = quat_conj($qEc2Hl);
    #print "conj ";
    #show_quat($con);
    my $rhlOr = mult_quats($con, $recOrient);
    #print "mult ";
    #show_quat($rhlOr);
    getEulerDeg($rhlOr, $rhead, $rpitch, $rroll );
}
  ============================================================== */
  /// Return a quaternion rotation from the earth centered to the
  /// simulation usual horizontal local frame from given
  /// longitude and latitude.
  /// The horizontal local frame used in simulations is the frame with x-axis
  /// pointing north, the y-axis pointing eastwards and the z axis
  /// pointing downwards.
/// Quaternion
//# x,y,z,w
//my $QX = 0;
//my $QY = 1;
//my $QZ = 2;
//my $QW = 3;
enum qoff { QX, QY, QZ, QW };

char *get_quat_stg(sgdQuat *rv4)
{
    char *tb = GetNxtBuf();
    double x = *rv4[QX];
    double y = *rv4[QY];
    double z = *rv4[QZ];
    double w = *rv4[QW];
    sprintf(tb,"x=%f, y=%f, z=%f, w=%f", x, y, z, w );
    return tb;
}

char *get_quat_stg2(sgdQuat *rv4)
{
    char *tb = GetNxtBuf();
    sgdQuat v;
    memcpy(&v,rv4,sizeof(sgdQuat));
    double x = v[QX];
    double y = v[QY];
    double z = v[QZ];
    double w = v[QW];
    sprintf(tb,"%lf,%lf,%lf,%lf", x, y, z, w );
    return tb;
}

char *get_point3d_stg(Point3D &p)
{
    char *tb = GetNxtBuf();
    double x = p.GetX();
    double y = p.GetY();
    double z = p.GetZ();
    sprintf(tb,"x=%lf, y=%lf, z=%lf", x, y, z );
    return tb;
}

char *get_point3d_stg2(Point3D *p)
{
    char *tb = GetNxtBuf();
    double x = p->GetX();
    double y = p->GetY();
    double z = p->GetZ();
    sprintf(tb,"%lf %lf %lf", x, y, z );
    return tb;
}


// # print out a quaternion - x,y,z,w
void show_quat(sgdQuat *rv4, char *msg)
{
    char *cp = get_quat_stg(rv4);
    SPRTF((char *)"sgdQuat: %s %s\n", cp, msg );

}

/// Return a quaternion rotation from the earth centered to the
/// simulation usual horizontal local frame from given
/// longitude and latitude.
/// The horizontal local frame used in simulations is the frame with x-axis
/// pointing north, the y-axis pointing eastwards and the z axis
/// pointing downwards.
//# static SGQuat fromLonLatRad(T lon, T lat)
//#    SGQuat q;
//#    T zd2 = T(0.5)*lon;
//#    T yd2 = T(-0.25)*SGMisc<T>::pi() - T(0.5)*lat;
//#    T Szd2 = sin(zd2);
//#    T Syd2 = sin(yd2);
//#    T Czd2 = cos(zd2);
//#    T Cyd2 = cos(yd2);
//#    q.w() = Czd2*Cyd2;
//#    q.x() = -Szd2*Syd2;
//#    q.y() = Czd2*Syd2;
//#    q.z() = Szd2*Cyd2;
//#    return q;  }
//sub fromLonLatRad($$) {
//    my ($lonr,$latr) = @_;
//    my @q = (0,0,0,0);
//    my $zd2 = 0.5 * $lonr; 
//    my $yd2 = -0.25 * $SGD_PI - (0.5 * $latr);
//    my $Szd2 = sin($zd2);
//    my $Syd2 = sin($yd2);
//    my $Czd2 = cos($zd2);
//    my $Cyd2 = cos($yd2);
//    $q[$QW] = $Czd2 * $Cyd2;
//    $q[$QX] = - $Szd2 * $Syd2;
//    $q[$QY] = $Czd2 * $Syd2;
//    $q[$QZ] = $Szd2 * $Cyd2;
//    return \@q;
//}
static sgdQuat *fromLonLatRad(double lon, double lat)
{
    static sgdQuat q;
    double zd2 = (0.5 * lon);
    double yd2 = -0.25 * SG_PI - (0.5 * lat);
    double Szd2 = sin(zd2);
    double Syd2 = sin(yd2);
    double Czd2 = cos(zd2);
    double Cyd2 = cos(yd2);
    q[QW] = Czd2 * Cyd2;
    q[QX] = -Szd2 * Syd2;
    q[QY] = Czd2 * Syd2;
    q[QZ] = Szd2 * Cyd2;
    return &q;
}

#if 0 // 000000000000000000000000000000000000000000000000000
// this FAILS!!! relaced below
static sgdQuat *mult_quats(sgdQuat *rv1, sgdQuat *rv2)
{
    static sgdQuat _s_mquat;
    sgdQuat *v = &_s_mquat;
    *v[QX] = *rv1[QW] * *rv2[QX] + *rv1[QX] * *rv2[QW] + *rv1[QY] * *rv2[QZ] - *rv1[QZ] * *rv2[QY];
    *v[QY] = *rv1[QW] * *rv2[QY] - *rv1[QX] * *rv2[QZ] + *rv1[QY] * *rv2[QW] + *rv1[QZ] * *rv2[QX];
    *v[QZ] = *rv1[QW] * *rv2[QZ] + *rv1[QX] * *rv2[QY] - *rv1[QY] * *rv2[QX] + *rv1[QZ] * *rv2[QW];
    *v[QW] = *rv1[QW] * *rv2[QW] - *rv1[QX] * *rv2[QX] - *rv1[QY] * *rv2[QY] - *rv1[QZ] * *rv2[QZ];
    return v;
}
#endif // 00000000000000000000000000000000000000000000000000

sgdQuat *m_mult2(const sgdQuat & v1, const sgdQuat & v2) 
{
    static sgdQuat v;
    v[QX] = v1[QW]*v2[QX] + v1[QX]*v2[QW] + v1[QY]*v2[QZ] - v1[QZ]*v2[QY];
    v[QY] = v1[QW]*v2[QY] - v1[QX]*v2[QZ] + v1[QY]*v2[QW] + v1[QZ]*v2[QX];
    v[QZ] = v1[QW]*v2[QZ] + v1[QX]*v2[QY] - v1[QY]*v2[QX] + v1[QZ]*v2[QW];
    v[QW] = v1[QW]*v2[QW] - v1[QX]*v2[QX] - v1[QY]*v2[QY] - v1[QZ]*v2[QZ];
    return &v;
}

// UGH - This FAILS
#if 0 // 0000000000000000000000000000000000000000000000000
/// Return a quaternion from real and imaginary part
//    q.w() = r;
//    q.x() = i.x();
//    q.y() = i.y();
//    q.z() = i.z();
static sgdQuat *fromRealImag(double r, Point3D *i)
{
    static sgdQuat _s_qfri;
    sgdQuat *pq = &_s_qfri;
    *pq[QX] = i->GetX();
    *pq[QY] = i->GetY();
    *pq[QZ] = i->GetZ();
    *pq[QW] = r;
    // ESPRTF("fromRealImag: r=%f Point3D %s, Quat %s\n", r, get_point3d_stg2(i), get_quat_stg2(pq));
    return pq;
}
#endif // 00000000000000000000000000000000000000000000000000

//sub scalar_mult_vector($$) {
//    my ($s,$rv) = @_;
//    my @v = (0,0,0);
//    $v[0] = ${$rv}[0] * $s;
//    $v[1] = ${$rv}[1] * $s;
//    $v[2] = ${$rv}[2] * $s;
//    return \@v;
//}
Point3D *scalar_mult_vector(double s, Point3D *rv )
{
    static Point3D _s_vsmv;
    Point3D *p = &_s_vsmv;
    double x = rv->GetX() * s;
    double y = rv->GetY() * s;
    double z = rv->GetZ() * s;
    p->Set( x, y, z );
    // ESPRTF("Mult Point3D %s, by %f, to get %s\n", get_point3d_stg2(rv), s, get_point3d_stg2(p));
    return p;
}


/// Create a quaternion from the angle axis representation where the angle
/// is stored in the axis' length
//sub fromAngleAxis($) {
//    my ($raxis) = @_;
//    my $nAxis = norm_vector_length($raxis);
//    if ($nAxis <= 0.0000001) {
//        my @arr = (0,0,0,0);
//        return \@arr; # SGQuat::unit();
//    }
//    my $angle2 = $nAxis * 0.5;
//    my $sang = sin($angle2) / $nAxis ;
//    my $cang = cos($angle2);
//    #print "nAxis = $nAxis, ange2 = $angle2, saxa = $sang\n";
//    my $rv = scalar_mult_vector($sang,$raxis);
//    #print "san ";
//    #show_vec3($rv);
//    #return fromRealImag(cos(angle2), T(sin(angle2)/nAxis)*axis);
//    return fromRealImag( $cang, $rv );
//}

#define SETQ(a,b) { for (int i = 0; i < 4; i++) a[i] = b[i]; }
static sgdQuat *fromAngleAxis( Point3D *axis )
{
    static sgdQuat quat;
    double nAxis = cf_norm(*axis);
    if (nAxis <= 0.0000001) {
        sgdQuat q = {0.0,0.0,0.0,0.0};
        SETQ(quat,q);
        return &quat;
    }
    double angle2 = (0.5 * nAxis);
    double sang = sin(angle2) / nAxis ;
    double cang = cos(angle2);
    // ESPRTF("fromAngleAxis: p3d %s, gave nAxis=%f, angle2=%f, sang=%f, cang=%f\n", get_point3d_stg2(axis), nAxis, angle2, sang, cang);
    // #print "nAxis = $nAxis, ange2 = $angle2, saxa = $sang\n";
    Point3D *rv = scalar_mult_vector(sang,axis);
//    #print "san ";
//    #show_vec3($rv);
//    #return fromRealImag(cos(angle2), T(sin(angle2)/nAxis)*axis);
//    return fromRealImag(cos(angle2), (sin(angle2)/nAxis)*axis);
//    return fromRealImag( cang, rv );
    quat[QW] = cang;
    quat[QX] = rv->GetX();
    quat[QY] = rv->GetY();
    quat[QZ] = rv->GetZ();
    return &quat;
}

/// The conjugate of the quaternion, this is also the
/// inverse for normalized quaternions
//#SGQuat<T> conj(const SGQuat<T>& v)
//#{ return SGQuat<T>(-v(0), -v(1), -v(2), v(3)); }
sgdQuat *quat_conj(sgdQuat & rq)
{
    static sgdQuat q;
    q[QX] = -rq[QX];
    q[QY] = -rq[QY];
    q[QZ] = -rq[QZ];
    q[QW] =  rq[QW];
    //# return [ -${$rq}[0], -${$rq}[1], -${$rq}[2], ${$rq}[3] ];
    return &q;
}

/// write the euler angles into the references
//#  void getEulerRad(T& zRad, T& yRad, T& xRad) const {
//#    T sqrQW = w()*w();
//#    T sqrQX = x()*x();
//#    T sqrQY = y()*y();
//#    T sqrQZ = z()*z();
//#    T num = 2*(y()*z() + w()*x());
//#    T den = sqrQW - sqrQX - sqrQY + sqrQZ;
//#    if (fabs(den) <= SGLimits<T>::min() &&
//#        fabs(num) <= SGLimits<T>::min())
//#      xRad = 0;
//#    else
//#      xRad = atan2(num, den);
//#    T tmp = 2*(x()*z() - w()*y());
//#    if (tmp <= -1)
//#      yRad = T(0.5)*SGMisc<T>::pi();
//#    else if (1 <= tmp)
//#      yRad = -T(0.5)*SGMisc<T>::pi();
//#    else
//#      yRad = -asin(tmp);
//#    num = 2*(x()*y() + w()*z()); 
//#    den = sqrQW + sqrQX - sqrQY - sqrQZ;
//#    if (fabs(den) <= SGLimits<T>::min() &&
//#        fabs(num) <= SGLimits<T>::min())
//#      zRad = 0;
//#    else {
//#      T psi = atan2(num, den);
//#      if (psi < 0)
//#        psi += 2*SGMisc<T>::pi();
//#      zRad = psi;
//#    }
//#  }

//                              *heading       *pitch        *roll
void getEulerRad(sgdQuat *q, double *rzRad, double *ryRad, double *rxRad)
{
    double xRad,yRad,zRad;
    sgdQuat rq;
    memcpy(&rq,q,sizeof(sgdQuat));
    double sqrQW = rq[QW] * rq[QW];
    double sqrQX = rq[QX] * rq[QX];
    double sqrQY = rq[QY] * rq[QY];
    double sqrQZ = rq[QZ] * rq[QZ];
    ESPRTF("gur: sgdQuat(%s) Squares XYZW %lf,%lf,%lf,%lf\n", get_quat_stg2(q), sqrQX, sqrQY, sqrQZ, sqrQW );
    //# y * z + w * x
    double num = 2 * ( rq[QY] * rq[QZ] + rq[QW] * rq[QX] );
    double den = sqrQW - sqrQX - sqrQY + sqrQZ;
    if ((abs(den) <= 0.0000001) &&
        (abs(num) <= 0.0000001) ) {
        xRad = 0;
    } else {
        xRad = atan2(num, den);
    }
    ESPRTF("gur: roll %g from atan2(%lf,%lf)\n", fgs_rad2deg(xRad), num, den );
 
    //# x * z - w * y
    double tmp = 2 * ( rq[QX] * rq[QZ] - rq[QW] * rq[QY] );
    if (tmp <= -1) {
        yRad = 0.5 * SG_PI;
    } else if (1 <= tmp) {
        yRad = - 0.5 * SG_PI;
    } else {
        yRad = -asin(tmp); // # needs Math::Trig
    }
    ESPRTF("gur: pitch %g from -asin(%lf)\n", fgs_rad2deg(yRad), tmp );

    //# x * y + w * z
    num = 2 * ( rq[QX] * rq[QY] + rq[QW] * rq[QZ] ); 
    den = sqrQW + sqrQX - sqrQY - sqrQZ;
    if ((abs(den) <= 0.0000001) &&
        (abs(num) <= 0.0000001) ) {
        zRad = 0;
    } else {
        double psi = atan2(num, den);
        if (psi < 0) {
            psi += 2 * SG_PI;
        }
        zRad = psi;
    }
    ESPRTF("gur: heading %d from atan2(%lf,%lf)\n", (int)(fgs_rad2deg(zRad) + 0.5), num, den );

    //# pass value back
    *rxRad = xRad;  // roll
    *ryRad = yRad;  // pitch
    *rzRad = zRad;  // heading

}

//# uses getEulerRad, and converts to degrees
//                              *heading       *pitch        *roll
void getEulerDeg(sgdQuat *rq, double *rzDeg, double *ryDeg, double *rxDeg)
{
    //my ($rq,$rzDeg,$ryDeg,$rxDeg) = @_;
    double xRad,yRad,zRad;
    getEulerRad(rq, &zRad, &yRad, &xRad);
    //# pass converted values back
    *rzDeg = fgs_rad2deg(zRad);
    *ryDeg = fgs_rad2deg(yRad);
    *rxDeg = fgs_rad2deg(xRad);
}

/* -------------------------------------------------
   motion encoding
    // The quaternion rotating from the earth centered frame to the
    // horizontal local frame
    SGQuatf qEc2Hl = SGQuatf::fromLonLatRad((float)lon, (float)lat);
    // The orientation wrt the horizontal local frame
    float heading = ifce.get_Psi();
    float pitch = ifce.get_Theta();
    float roll = ifce.get_Phi();
    SGQuatf hlOr = SGQuatf::fromYawPitchRoll(heading, pitch, roll);
    // The orientation of the vehicle wrt the earth centered frame
    motionInfo.orientation = qEc2Hl*hlOr;
    euler_get does the REVERSE of that
    Given the orientation, return heading, pitch and roll
    -------------------------------------------------- */
void euler_get( double lat, double lon, double ox, double oy, double oz, double *phead, double *ppitch, double *proll )
{
    Point3D v;
    v.Set( ox, oy, oz );
    sgdQuat *recOrient = fromAngleAxis(&v);
    ESPRTF("eg: From fromAngleAxis(%s), got recOrient %s\n", get_point3d_stg2(&v), get_quat_stg2(recOrient));
    double lat_rad, lon_rad;
    lat_rad = lat * SG_DEGREES_TO_RADIANS;
    lon_rad = lon * SG_DEGREES_TO_RADIANS;
    sgdQuat *qEc2Hl = fromLonLatRad(lon_rad, lat_rad);
    ESPRTF("eg: From lat/lon %lf,%lf, fromLonLatRad(%lf,%lf) got qEc2Hl %s\n",
        lat, lon, lat_rad, lon_rad, get_quat_stg2(qEc2Hl));
    sgdQuat *con = quat_conj(*qEc2Hl);
    //sgdQuat *rhlOr = mult_quats(con, recOrient);
    sgdQuat *rhlOr =m_mult2(*con, *recOrient);
    ESPRTF("eg: From quat_conj %s, from m_mult2 %s\n", get_quat_stg2(con), get_quat_stg2(rhlOr));
    getEulerDeg(rhlOr, phead, ppitch, proll );
    ESPRTF("eg: getEulerDeg returned h=%d, p=%g, r=%g\n", (int)(*phead + 0.5), *ppitch, *proll);
}

#endif // #ifndef USE_SIMGEAR

// eof - cf_euler.cxx

// geod.cxx

// Example of using the GeographicLib::Geocentric class
// 20170311 - Some example have been removed under COMPILE_EXAMPLE_CODE macro

#include <iostream>
#include <iomanip>
#include <exception>
#include <cmath>
#include <GeographicLib/Geocentric.hpp>
#include <GeographicLib/AlbersEqualArea.hpp>
#include <GeographicLib/Geodesic.hpp>
#include <GeographicLib/CassiniSoldner.hpp>
#include <GeographicLib/Constants.hpp>
#include <vector>
#include <GeographicLib/CircularEngine.hpp>
#include <GeographicLib/SphericalHarmonic.hpp>
#include <GeographicLib/DMS.hpp>
#include <GeographicLib/Ellipsoid.hpp>
#include <GeographicLib/Math.hpp>
#include <GeographicLib/EllipticFunction.hpp>
#include <GeographicLib/PolygonArea.hpp>
#include <GeographicLib/MagneticModel.hpp>
#include <GeographicLib/MagneticCircle.hpp>

#include "geod.hxx"
#include "sprtf.hxx"

static const char *mod_name = "geod.cxx";

using namespace std;
using namespace GeographicLib;

/* -----------------------------------------------------------

    Coordinate conversion
    from : http://osdir.com/ml/GoogleMapsAPI/2009-04/msg00629.html
    To get from a lat/long value to a pixel value requires a series of transformations:

    1. Geodetic CS -> World CS (projection) - GeographicalLib - hgl.bat hglm.bat
    2. World CS -> View CS (affine)
    3. View CS -> Document CS (affine)

    How do we do step #1
    --------------------

    Using GeographicLib, below is a simple service

    int geod_to_geoc( double *pX, double *pY, double *pZ,
                  double lat, double lon, double alt )
    This will convert the geod coordinates, lat, lon, alt,
    to geocentric coordinates X, Y, Z.

    How do we do step #2?
    ---------------------

    Its done like this:
 
    1. Translate the world CS center to 0,0
    2. Rotate the map if needed
    3. Scale the map to the view CS
    4. Translate from 0,0 in the view CS to the center of the view CS.

   ----------------------------------------------------------- */

int geod_to_geoc( double *pX, double *pY, double *pZ,
                  double lat, double lon, double alt )
{
    try {
        Geocentric earth(Constants::WGS84_a(), Constants::WGS84_f());
        // Alternatively: const Geocentric& earth = Geocentric::WGS84;
        double X, Y, Z;
        earth.Forward(lat, lon, alt, X, Y, Z);
        *pX = X;
        *pY = Y;
        *pZ = Z;
   } catch (const exception& e) {
       // cerr << "Caught exception: " << e.what() << "\n";
       sprtf("%s: geod_to_geoc: Caught exception: %s\n", mod_name, e.what());
       return 1;
   }
   return 0;
}

int geoc_to_geod( double *plat, double *plon, double *palt,
                  double X, double Y, double Z )
{
    try {
        Geocentric earth(Constants::WGS84_a(), Constants::WGS84_f());
        // Alternatively: const Geocentric& earth = Geocentric::WGS84;
        // Sample reverse calculation
        // double X = 302e3, Y = 5636e3, Z = 2980e3;
        double lat, lon, h;
        earth.Reverse(X, Y, Z, lat, lon, h);
        //cout << lat << " " << lon << " " << h << "\n";
        *plat = lat;
        *plon = lon;
        *palt = h;
    } catch (const exception& e) {
        //cerr << "Caught exception: " << e.what() << "\n";
       sprtf("%s: geoc_to_geod: Caught exception: %s\n", mod_name, e.what());
        return 1;
    }
    return 0;
}


int geod_to_albers( double *px, double *py,
                    double lat, double lon, double scale )
{
    try {
        const double
            a = Constants::WGS84_a<double>(),
            f = Constants::WGS84_f<double>(),
            lat1 = 40 + 58/60.0, lat2 = 39 + 56/60.0, // standard parallels
            k1 = scale,                               // scale
            lon0 = -77 - 45/60.0;                     // Central meridan
        // Set up basic projection
        const AlbersEqualArea albers(a, f, lat1, lat2, k1);
        // Sample conversion from geodetic to Albers Equal Area
        //     double lat = 39.95, lon = -75.17;    // Philadelphia
        double x, y;
        albers.Forward(lon0, lat, lon, x, y);
        // std::cout << x << " " << y << "\n";
        *px = x;
        *py = y;
    } catch (const exception& e) {
        //cerr << "Caught exception: " << e.what() << "\n";
        sprtf("%s: geod_to_albers: Caught exception: %s\n", mod_name, e.what());
        return 1;
    }
    return 0;
}

int albers_to_geod( double *plat, double *plon,
                    double x, double y, double scale )
{
    try {
        const double
            a = Constants::WGS84_a<double>(),
            f = Constants::WGS84_f<double>(),
            lat1 = 40 + 58/60.0, lat2 = 39 + 56/60.0, // standard parallels
            k1 = scale,                               // scale
            lon0 = -77 - 45/60.0;                     // Central meridan
        // Set up basic projection
        const AlbersEqualArea albers(a, f, lat1, lat2, k1);
        double lat, lon;
        albers.Reverse(lon0, x, y, lat, lon);
        // std::cout << lat << " " << lon << "\n";
        *plat = lat;
        *plon = lon;
    } catch (const exception& e) {
        //cerr << "Caught exception: " << e.what() << "\n";
        sprtf("%s: albers_to_geod: Caught exception: %s\n", mod_name, e.what());
        return 1;
    }
    return 0;
}

int geod_direct( double *plat, double *plon,
                 double lat, double lon, double dist, double hdg )
{
    try {
        Geodesic geod(Constants::WGS84_a(), Constants::WGS84_f());
        // Alternatively: const Geodesic& geod = Geodesic::WGS84;
        // Sample direct calculation, travelling about NE from JFK
        //double lat1 = 40.6, lon1 = -73.8, s12 = 5.5e6, azi1 = 51;
        double lat1 = lat, lon1 = lon, s12 = dist, azi1 = hdg;
        double lat2, lon2;
        geod.Direct(lat1, lon1, azi1, s12, lat2, lon2);
        //cout << lat2 << " " << lon2 << "\n";
        *plat = lat2;
        *plon = lon2;
    } catch (const exception& e) {
        // cerr << "Caught exception: " << e.what() << "\n";
        sprtf("%s: geod_direct: Caught exception: %s\n", mod_name, e.what());
        return 1;
    }
    return 0;
}

int geod_distance( double *pdist,
                   double lat1, double lon1, double lat2, double lon2 )
{
    try {
        Geodesic geod(Constants::WGS84_a(), Constants::WGS84_f());
        // Alternatively: const Geodesic& geod = Geodesic::WGS84;
        // Sample inverse calculation, JFK to LHR
        // double lat1 = 40.6, lon1 = -73.8, // JFK Airport
        //        lat2 = 51.6, lon2 = -0.5;  // LHR Airport
        double s12;
        geod.Inverse(lat1, lon1, lat2, lon2, s12);
        // cout << s12 << "\n";
        *pdist = s12;
    } catch (const exception& e) {
        //cerr << "Caught exception: " << e.what() << "\n";
        sprtf("%s: geod_distance: Caught exception: %s\n", mod_name, e.what());
        return 1;
    }
    return 0;
}

#ifdef COMPILE_EXAMPLE_CODE

int example_CassiniSoldner()
{
    cout << "example_CassiniSoldner()" << endl;
    try {
        Geodesic geod(Constants::WGS84_a(), Constants::WGS84_f());
        // Alternatively: const Geodesic& geod = Geodesic::WGS84;
        const double lat0 = 48 + 50/60.0, lon0 = 2 + 20/60.0; // Paris
        cout << "CassiniSoldner proj(" << lat0 << "," << lon0 << ",...) // Paris" << endl;
        CassiniSoldner proj(lat0, lon0, geod);
        {
            // Sample forward calculation
            double lat = 50.9, lon = 1.8; // Calais
            double x, y;
            proj.Forward(lat, lon, x, y);
            cout << "to " << lat << "," << lon << " Calais" << endl;
            cout << "x, y " << x << ", " << y << "\n";
        }
        {
            // Sample reverse calculation
            double x = -38e3, y = 230e3;
            double lat, lon;
            cout << "Reverse " << x << "," << y << endl;
            proj.Reverse(x, y, lat, lon);
            cout << lat << " " << lon << "\n";
        }
    } catch (const exception& e) {
        cerr << "Caught exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int example_CircularEngine()
{
    cout << "example_CircularEngine()" << endl;
    // This computes the same value as example-SphericalHarmonic.cpp using a
    // CircularEngine (which will be faster if many values on a circle of
    // latitude are to be found).
    try {
        int N = 3;                  // The maxium degree
        double ca[] = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1}; // cosine coefficients
        vector<double> C(ca, ca + (N + 1) * (N + 2) / 2);
        double sa[] = {6, 5, 4, 3, 2, 1}; // sine coefficients
        vector<double> S(sa, sa + N * (N + 1) / 2);
        double a = 1;
        SphericalHarmonic h(C, S, N, a);
        double x = 2, y = 3, z = 1, p = Math::hypot(x, y);
        CircularEngine circ = h.Circle(p, z, true);
        double v, vx, vy, vz;
        v = circ(x/p, y/p, vx, vy, vz);
        cout << v << " " << vx << " " << vy << " " << vz << "\n";
    } catch (const exception& e) {
        cerr << "Caught exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int example_DMS()
{
    cout << "example_DMS()" << endl;
    try {
        {
            string dms = "30d14'45.6\"S";
            DMS::flag type;
            double ang = DMS::Decode(dms, type);
            cout << "convert " << dms << ", type " << type << " " << ang << "\n";
        }
        {
            double ang = -30.245715;
            string dms = DMS::Encode(ang, 6, DMS::LATITUDE);
            cout << "convert " << ang << " to dms " << dms << "\n";
        }
    } catch (const exception& e) {
        cerr << "Caught exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int example_Ellipsoid()
{
    cout << "example_Ellipsoid()" << endl;
    try {
          Ellipsoid wgs84(Constants::WGS84_a(), Constants::WGS84_f());
          // Alternatively: const Ellipsoid& wgs84 = Ellipsoid::WGS84;
          cout << "The latitude half way between the equator and the pole is "
               << wgs84.InverseRectifyingLatitude(45) << "\n";
          cout << "Half the area of the ellipsoid lies between latitudes +/- "
               << wgs84.InverseAuthalicLatitude(30) << "\n";
          cout << "The northernmost edge of a square Mercator map at latitude "
              << wgs84.InverseIsometricLatitude(180) << "\n";
    } 
    catch (const exception& e) {
        cerr << "Caught exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

#if 0 // ===================================================================
int example_EllipticFunction()
{
    cout << "example_EllipticFunction()" << endl;
  try {
    EllipticFunction ell(0.1);  // parameter m = 0.1
    cout << "See Abramowitz and Stegun, table 17.1\n";
    cout << ell.K() << " " << ell.E() << "\n";
    double phi = 20 * Math::degree();
    cout << "See Abramowitz and Stegun, table 17.6 with " << endl;
    cout << "alpha = asin(sqrt(m)) = 18.43 deg and phi = 20 deg" << endl;
    cout << ell.E(phi) << " "
         << ell.E(sin(phi), cos(phi), sqrt(1 - ell.k2() * Math::sq(sin(phi))))
         << "\n";
    cout << "See Carlson 1995, Sec 3." << endl;
    cout << fixed << setprecision(16)
         << "RF(1,2,0)      = " << EllipticFunction::RF(1,2)      << "\n"
         << "RF(2,3,4)      = " << EllipticFunction::RF(2,3,4)    << "\n"
         << "RC(0,1/4)      = " << EllipticFunction::RC(0,0.25)   << "\n"
         << "RC(9/4,2)      = " << EllipticFunction::RC(2.25,2)   << "\n"
         << "RC(1/4,-2)     = " << EllipticFunction::RC(0.25,-2)  << "\n"
         << "RJ(0,1,2,3)    = " << EllipticFunction::RJ(0,1,2,3)  << "\n"
         << "RJ(2,3,4,5)    = " << EllipticFunction::RJ(2,3,4,5)  << "\n"
         << "RD(0,2,1)      = " << EllipticFunction::RD(0,2,1)    << "\n"
         << "RD(2,3,4)      = " << EllipticFunction::RD(2,3,4)    << "\n"
         << "RG(0,16,16)    = " << EllipticFunction::RG(16,16)    << "\n"
         << "RG(2,3,4)      = " << EllipticFunction::RG(2,3,4)    << "\n"
         << "RG(0,0.0796,4) = " << EllipticFunction::RG(0.0796,4) << "\n";
  }
  catch (const GeographicErr& e) {
    cout << "Caught exception: " << e.what() << "\n";
  }
  return 0;
}
#endif // 0 ===================================================================== */

int example_Geodesic_small()
{
    cout << "example_Geodesic_small()" << endl;
  const Geodesic& geod = Geodesic::WGS84;
  cout << "Distance from JFK to LHR ";
  double
    lat1 = 40.6, lon1 = -73.8, // JFK Airport
    lat2 = 51.6, lon2 = -0.5;  // LHR Airport
  double s12;
  geod.Inverse(lat1, lon1, lat2, lon2, s12);
  cout << s12 / 1000 << " km\n";
  return 0;
}

int example_PolygonArea()
{
    int res, km;
    cout << "example_PolygonArea()" << endl;
  try {

    Geodesic geod(Constants::WGS84_a(), Constants::WGS84_f());
    // Alternatively: const Geodesic& geod = Geodesic::WGS84;
    PolygonArea poly(geod);
    double lat, lon, lat2, lon2, dist, tdist;
    tdist = 0.0;
    lat = 52;
    lon = 0;
    cout << "AddPoint " << lat << "," << lon << " London" << endl;
    poly.AddPoint( lat,  lon);     // London

    lat2 = 41;
    lon2 = -74;
    res = geod_distance( &dist, lat, lon, lat2, lon2 );
    if (res)
        cout << "AddPoint " << lat2 << "," << lon2 << " New York!" << endl;
    else {
        km = (int)((dist / 1000) + 0.5);
        cout << "AddPoint " << lat2 << "," << lon2 << " New York - dist_km " << km << endl;
        tdist += dist;
    }
    poly.AddPoint( lat2, lon2);     // New York

    lat = -23;
    lon = -43;
    res = geod_distance( &dist, lat, lon, lat2, lon2 );
    if (res)
        cout << "AddPoint " << lat << "," << lon << " Rio de Janerio!" << endl;
    else {
        km = (int)((dist / 1000) + 0.5);
        cout << "AddPoint " << lat << "," << lon << " Rio de Janerio - dist_km " << km << endl;
        tdist += dist;
    }
    poly.AddPoint(lat,lon);     // Rio de Janeiro

    lat2 = -26;
    lon2 = 28;
    res = geod_distance( &dist, lat, lon, lat2, lon2 );
    if (res)
        cout << "AddPoint " << lat2 << "," << lon2 << " Johannesburg!" << endl;
    else {
        km = (int)((dist / 1000) + 0.5);
        cout << "AddPoint " << lat2 << "," << lon2 << " Johannesburg - dist_km " << km << endl;
        tdist += dist;
    }
    poly.AddPoint(lat2, lon2);     // Johannesburg

    lat = 52;   // london again
    lon = 0;
    res = geod_distance( &dist, lat, lon, lat2, lon2 );
    if (res == 0) {
        km = (int)((dist / 1000) + 0.5);
        tdist += dist;
        cout << "Back to London - dist_km " << km;
        km = (int)((tdist / 1000) + 0.5);
        cout << " Total distance " << km << " km" << endl;
    }

    double perimeter, area;
    unsigned n = poly.Compute(false, true, perimeter, area);
    km = (int)((perimeter / 1000) + 0.5);
    cout << "Hops " << n << ", with permimeter " << km << " km, area " << area << "\n";
  }
  catch (const exception& e) {
    cerr << "Caught exception: " << e.what() << "\n";
    return 1;
  }
  return 0;
}

int example_MagneticModel()
{
    cout << "example_MagneticModel()" << endl;
    //int res;
  try {
      // WHERE is this file - assume Magentic Model
      // DOWNLOAD: from : http://geographiclib.sourceforge.net/html/magnetic.html
      // expects installation in 
      // C:/Documents and Settings/All Users/Application Data/GeographicLib/magnetic/wmm2010.wmm

    MagneticModel mag("wmm2010","C:\\Users\\user\\AppData\\Local\\GeographicLib\\magnetic");
    double lat = 27.99, lon0 = 86.93, h = 8820, t = 2012; // Mt Everest
    {
        cout << "Slow method of evaluating the values at several points on a circle of latitude." << endl;
        cout << "lat " << lat << ", lon " << lon0 << ", alt " << h << 
            ", t " << t << ", name Mt Everest" << endl;
      for (int i = -5; i <= 5; ++i) {
        double lon = lon0 + i * 0.2;
        double Bx, By, Bz;
        mag(t, lat, lon, h, Bx, By, Bz);
        //double lat1, lon1, alt1;
        //res = geoc_to_geod( &lat1, &lon1, &alt1,  Bx, By, Bz );
        //if (res) {
            cout << lon << " Bx " << Bx << " By " << By << " Bz " << Bz << "\n";
        //} else {
        //    cout << lon << " " << Bx << " " << By << " " << Bz << 
        //        " " << lat1 << " " << lon1 << " " << alt1 << "\n";
        //}
      }
    }
    {
        cout << "Fast method of evaluating the values at several points on a circle of latitude using MagneticCircle.\n";
        MagneticCircle circ = mag.Circle(t, lat, h);
        for (int i = -5; i <= 5; ++i) {
            double lon = lon0 + i * 0.2;
            double Bx, By, Bz;
            circ(lon, Bx, By, Bz);
            cout << lon << " Bx " << Bx << " By " << By << " Bz " << Bz << "\n";
      }
    }
  }
  catch (const exception& e) {
    cerr << "Caught exception: " << e.what() << "\n";
    return 1;
  }
  return 0;
}

#endif // #ifdef COMPILE_EXAMPLE_CODE

// eof - geod.cxx

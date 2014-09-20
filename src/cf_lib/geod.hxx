// shp_geod.hxx
#ifndef _SHP_GEOD_HXX_
#define _SHP_GEOD_HXX_

extern int geod_to_geoc( double *pX, double *pY, double *pZ,
                  double lat, double lon, double alt = 0.0 );
extern int geoc_to_geod( double *plat, double *plon, double *palt,
                  double X, double Y, double Z );

extern int geod_to_albers( double *px, double *py,
                    double lat, double lon, double scale = 1.0 );
extern int albers_to_geod( double *plat, double *plon,
                    double x, double y, double scale = 1.0 );

extern int geod_direct( double *plat, double *plon,
                 double lat, double lon, double dist, double hdg );
extern int geod_distance( double *pdist,
                   double lat1, double lon1, double lat2, double lon2 );


#endif // #ifndef _SHP_GEOD_HXX_
// eof - shp_geod.hxx




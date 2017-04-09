
// tiny_xdr.hxx
#ifndef USE_SIMGEAR
//////////////////////////////////////////////////////////////////////////////////////////////
#ifndef _TINY_XDR_HXX_
#define _TINY_XDR_HXX_
//////////////////////////////////////////////////////////////////////
//
//      Tiny XDR implementation for flightgear
//      written by Oliver Schroeder
//      released to the puiblic domain
//
//      This implementation is not complete, but implements
//      everything we need.
//
//      For further reading on XDR read RFC 1832.
//
//////////////////////////////////////////////////////////////////////

/**
 * xdr encode 8, 16 and 32 Bit values
 */
template<typename TYPE>
xdr_data_t XDR_encode ( TYPE Val )
{
        union
        {
                xdr_data_t      encoded;
                TYPE            raw;
        } tmp;

        tmp.raw = Val;
        tmp.encoded = SWAP32(tmp.encoded);
        return (tmp.encoded);
}

/**
 * xdr decode 8, 16 and 32 Bit values
 */
template<typename TYPE>
TYPE XDR_decode ( xdr_data_t Val )
{
        union
        {
                xdr_data_t      encoded;
                TYPE            raw;
        } tmp;

        tmp.encoded = SWAP32(Val);
        return (tmp.raw);
}

/**
 * xdr encode 64 Bit values
 */
template<typename TYPE>
xdr_data2_t XDR_encode64 ( TYPE Val )
{
        union
        {
                xdr_data2_t     encoded;
                TYPE            raw;
        } tmp;

        tmp.raw = Val;
        tmp.encoded = SWAP64(tmp.encoded);
        return (tmp.encoded);
}

/**
 * xdr decode 64 Bit values
 */
template<typename TYPE>
TYPE XDR_decode64 ( xdr_data2_t Val )
{
        union {
            xdr_data2_t     encoded;
            TYPE            raw;
        } tmp;

        tmp.encoded = SWAP64 (Val);
        return (tmp.raw);
}


//////////////////////////////////////////////////////////////////////
//
//      encode to network byte order
//
/////////////////////////////////////////////////////////////////////

/**
 * encode 8-Bit values to network byte order
 * (actually encodes nothing, just to satisfy the templates)
 */
template<typename TYPE>
uint8_t
NET_encode8 ( TYPE Val )
{
        union
        {
                uint8_t netbyte;
                TYPE    raw;
        } tmp;

        tmp.raw = Val;
        return (tmp.netbyte);
}

/**
 * decode 8-Bit values from network byte order
 * (actually decodes nothing, just to satisfy the templates)
 */
template<typename TYPE>
TYPE
NET_decode8 ( uint8_t Val )
{
        union
        {
                uint8_t netbyte;
                TYPE    raw;
        } tmp;

        tmp.netbyte = Val;
        return (tmp.raw);
}

/**
 * encode 16-Bit values to network byte order
 */
template<typename TYPE>
uint16_t
NET_encode16 ( TYPE Val )
{
        union
        {
                uint16_t        netbyte;
                TYPE            raw;
        } tmp;

        tmp.raw = Val;
        tmp.netbyte = SWAP16(tmp.netbyte);
        return (tmp.netbyte);
}

/**
 * decode 16-Bit values from network byte order
 */
template<typename TYPE>
TYPE
NET_decode16 ( uint16_t Val )
{
        union
        {
                uint16_t        netbyte;
                TYPE            raw;
        } tmp;

        tmp.netbyte = SWAP16(Val);
        return (tmp.raw);
}

/**
 * encode 32-Bit values to network byte order
 */
template<typename TYPE>
uint32_t
NET_encode32 ( TYPE Val )
{
        union
        {
                uint32_t        netbyte;
                TYPE            raw;
        } tmp;

        tmp.raw = Val;
        tmp.netbyte = SWAP32(tmp.netbyte);
        return (tmp.netbyte);
}

/**
 * decode 32-Bit values from network byte order
 */
template<typename TYPE>
TYPE
NET_decode32 ( uint32_t Val )
{
        union
        {
                uint32_t        netbyte;
                TYPE            raw;
        } tmp;

        tmp.netbyte = SWAP32(Val);
        return (tmp.raw);
}

/**
 * encode 64-Bit values to network byte order
 */
template<typename TYPE>
uint64_t
NET_encode64 ( TYPE Val )
{
        union
        {
                uint64_t        netbyte;
                TYPE            raw;
        } tmp;

        tmp.raw = Val;
        tmp.netbyte = SWAP64(tmp.netbyte);
        return (tmp.netbyte);
}

/**
 * decode 64-Bit values from network byte order
 */
template<typename TYPE>
TYPE
NET_decode64 ( uint64_t Val )
{
        union
        {
                uint64_t        netbyte;
                TYPE            raw;
        } tmp;
        tmp.netbyte = SWAP64(Val);
        return (tmp.raw);
}


#endif // #ifndef _TINY_XDR_HXX_
//////////////////////////////////////////////////////////////////////////////////////////////
#endif // #ifndef USE_SIMGEAR

// eof - tiny_xdr.hxx



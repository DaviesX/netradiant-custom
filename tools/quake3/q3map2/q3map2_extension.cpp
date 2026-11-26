/* -------------------------------------------------------------------------------

   The q3map2 SH extension (the SH side file); see q3map2_extension.h.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   ------------------------------------------------------------------------------- */

#include "q3map2_extension.h"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <system_error>

namespace q3map2_extension_internal
{

uint16_t FloatToHalf( float value ){
	if ( value > 65504.0f ) {
		value = 65504.0f;
	}
	else if ( value < -65504.0f ) {
		value = -65504.0f;
	}
	const uint32_t bits = std::bit_cast<uint32_t>( value );
	const uint16_t sign = static_cast<uint16_t>( ( bits >> 16 ) & 0x8000 );
	const uint32_t magnitude = bits & 0x7FFFFFFF;
	if ( magnitude > 0x7F800000 ) { // NaN; the writer refuses it, so any quiet NaN does
		return sign | 0x7E00;
	}
	if ( magnitude >= 0x38800000 ) { // at least 2^-14: a normal half
		// Rebias the exponent from 127 to 15, then round the 23-bit mantissa to
		// 10 bits, to nearest even; a mantissa carry correctly bumps the exponent.
		uint32_t rebiased = magnitude - 0x38000000;
		rebiased += 0x0FFF + ( ( rebiased >> 13 ) & 1 );
		return sign | static_cast<uint16_t>( rebiased >> 13 );
	}
	if ( magnitude <= 0x33000000 ) { // at most 2^-25, the tie below the smallest subnormal
		return sign;
	}
	// A subnormal half counts units of 2^-24. The float is significand * 2^(e - 150)
	// with a 24-bit significand, so the count is significand >> (126 - e).
	const uint32_t exponent = magnitude >> 23;
	const uint32_t significand = ( magnitude & 0x007FFFFF ) | 0x00800000;
	const uint32_t shift = 126 - exponent; // 14 to 24
	uint32_t units = significand >> shift;
	const uint32_t rest = significand & ( ( 1u << shift ) - 1 );
	const uint32_t half_unit = 1u << ( shift - 1 );
	if ( rest > half_unit || ( rest == half_unit && ( units & 1 ) ) ) {
		++units; // 1024 becomes the smallest normal, as it should
	}
	return sign | static_cast<uint16_t>( units );
}

float HalfToFloat( uint16_t bits ){
	const uint32_t sign = static_cast<uint32_t>( bits & 0x8000 ) << 16;
	const uint32_t exponent = ( bits >> 10 ) & 0x1F;
	const uint32_t mantissa = bits & 0x3FF;
	if ( exponent == 0 ) {
		const float magnitude = std::ldexp( static_cast<float>( mantissa ), -24 );
		return std::bit_cast<float>( std::bit_cast<uint32_t>( magnitude ) | sign );
	}
	if ( exponent == 31 ) {
		return std::bit_cast<float>( sign | 0x7F800000 | ( mantissa << 13 ) );
	}
	return std::bit_cast<float>( sign | ( ( exponent + 112 ) << 23 ) | ( mantissa << 13 ) );
}

} // namespace q3map2_extension_internal

namespace
{

using q3map2_extension_internal::FloatToHalf;
using q3map2_extension_internal::HalfToFloat;

constexpr char c_magic[4] = { 'S', 'H', 'S', 'F' };
constexpr uint32_t c_version = 3;
constexpr uint64_t c_headerBytes = 24;
constexpr uint64_t c_pageHeaderBytes = 8;
constexpr int c_valuesPerLuxel = 27;
constexpr uint64_t c_bytesPerLuxel = ( c_valuesPerLuxel + 1 ) * 2;
constexpr uint64_t c_maxPageSide = std::numeric_limits<int32_t>::max();
constexpr uint64_t c_gridHeaderBytes = 36;
constexpr uint64_t c_bytesPerProbe = c_valuesPerLuxel * 4 + 4 + 1; // coefficients, sky visibility, flag

void SetError( std::string* error, const std::string& message ){
	if ( error != nullptr ) {
		*error = message;
	}
}

std::string Num( double value ){
	char buffer[32];
	std::snprintf( buffer, sizeof( buffer ), "%g", value );
	return buffer;
}

std::string Hex16( uint16_t bits ){
	char buffer[8];
	std::snprintf( buffer, sizeof( buffer ), "0x%04x", bits );
	return buffer;
}

std::string Str( uint64_t value ){
	return std::to_string( value );
}

// --- Writer checks --------------------------------------------------------

// Returns an empty string when the page's sizes and counts are right.
std::string PageShapeProblem( const SHQ3Map2ExtensionPage& page, size_t p ){
	const std::string where = "pages[" + Str( p ) + "]";
	if ( page.width < 1 ) {
		return where + ".width: " + std::to_string( page.width );
	}
	if ( page.height < 1 ) {
		return where + ".height: " + std::to_string( page.height );
	}
	const uint64_t luxels = uint64_t( page.width ) * uint64_t( page.height );
	if ( page.coeffs.size() != luxels * c_valuesPerLuxel ) {
		std::string have = Str( page.coeffs.size() ) + " values";
		if ( page.coeffs.size() % c_valuesPerLuxel == 0 ) {
			have += " (" + Str( page.coeffs.size() / c_valuesPerLuxel ) + " luxels)";
		}
		return where + ".coeffs: " + have + ", expected " + Str( luxels * c_valuesPerLuxel ) + " (" + Str( luxels ) + " luxels of 27, " +
		       std::to_string( page.width ) + " x " + std::to_string( page.height ) + ")";
	}
	if ( page.visibility.size() != luxels ) {
		return where + ".visibility: " + Str( page.visibility.size() ) + " values, expected " + Str( luxels ) + " (" +
		       std::to_string( page.width ) + " x " + std::to_string( page.height ) + ")";
	}
	return {};
}

std::string PageValueProblem( const SHQ3Map2ExtensionPage& page, size_t p ){
	const std::string where = "pages[" + Str( p ) + "]";
	for ( size_t j = 0; j < page.visibility.size(); ++j ) {
		// Visibility first, so the bake's not-baked marker is reported as such.
		const float vis = page.visibility[j];
		if ( !std::isfinite( vis ) ) {
			return where + ".visibility[" + Str( j ) + "]: non-finite value " + Num( vis );
		}
		if ( !( vis >= 0.0f && vis <= 1.0f ) ) {
			return where + ".visibility[" + Str( j ) + "]: " + Num( vis ) + " outside [0, 1] (a luxel the bake skipped must be filled)";
		}
		const float* luxel = &page.coeffs[j * c_valuesPerLuxel];
		for ( int i = 0; i < c_valuesPerLuxel; ++i ) {
			if ( !std::isfinite( luxel[i] ) ) {
				return where + ".coeffs: luxel " + Str( j ) + " coefficient " + Str( i / 3 ) + " channel " + Str( i % 3 ) + ": non-finite value " +
				       Num( luxel[i] );
			}
		}
		for ( int c = 0; c < 3; ++c ) {
			if ( luxel[c] < 0.0f ) {
				return where + ".coeffs: luxel " + Str( j ) + " coefficient 0 channel " + Str( c ) + ": negative L0 component " + Num( luxel[c] );
			}
		}
	}
	return {};
}

std::string CountProblem( size_t count, const char* field ){
	if ( count > std::numeric_limits<uint32_t>::max() ) {
		return std::string( field ) + ": count " + Str( count ) + " does not fit in a uint32";
	}
	return {};
}

std::string HullMeshProblem( const SHHullMesh& mesh ){
	for ( const auto& [count, field] : { std::pair{ mesh.positions.size(), "hull_mesh.positions" },
	                                     std::pair{ mesh.indices.size(), "hull_mesh.indices" },
	                                     std::pair{ mesh.hulls.size(), "hull_mesh.hulls" } } ) {
		if ( std::string problem = CountProblem( count, field ); !problem.empty() ) {
			return problem;
		}
	}
	for ( size_t v = 0; v < mesh.positions.size(); ++v ) {
		const Vector3& pos = mesh.positions[v];
		if ( !std::isfinite( pos.x() ) || !std::isfinite( pos.y() ) || !std::isfinite( pos.z() ) ) {
			return "hull_mesh.positions[" + Str( v ) + "]: non-finite position (" + Num( pos.x() ) + ", " + Num( pos.y() ) + ", " +
			       Num( pos.z() ) + ")";
		}
	}
	uint64_t expected_first = 0;
	for ( size_t h = 0; h < mesh.hulls.size(); ++h ) {
		const SHHullRange& range = mesh.hulls[h];
		if ( range.index_count == 0 || range.index_count % 3 != 0 ) {
			return "hull_mesh.hulls[" + Str( h ) + "].index_count: " + Str( range.index_count ) + " is not a positive multiple of 3";
		}
		if ( range.first_index != expected_first ) {
			return "hull_mesh.hulls[" + Str( h ) + "].first_index: " + Str( range.first_index ) + ", expected " + Str( expected_first );
		}
		expected_first += range.index_count;
	}
	if ( expected_first != mesh.indices.size() ) {
		return "hull_mesh.hulls: ranges cover " + Str( expected_first ) + " of " + Str( mesh.indices.size() ) + " indices";
	}
	for ( size_t i = 0; i < mesh.indices.size(); ++i ) {
		if ( mesh.indices[i] >= mesh.positions.size() ) {
			return "hull_mesh.indices[" + Str( i ) + "]: index " + Str( mesh.indices[i] ) + " not below vertex count " + Str( mesh.positions.size() );
		}
	}
	return {};
}

std::string VectorText( const Vector3& v ){
	return "(" + Num( v.x() ) + ", " + Num( v.y() ) + ", " + Num( v.z() ) + ")";
}

bool VectorFinite( const Vector3& v ){
	return std::isfinite( v.x() ) && std::isfinite( v.y() ) && std::isfinite( v.z() );
}

std::string DimensionsText( const uint32_t dims[3] ){
	return Str( dims[0] ) + " x " + Str( dims[1] ) + " x " + Str( dims[2] );
}

// Checks one probe's values, `coeffs` its 27 and `visibility` its sky
// visibility; empty when they are right. The message is the part after
// "probe i".
std::string ProbeValueProblem( const float* coeffs, float visibility, unsigned valid ) {
	if ( valid > 1 ) {
		return ": valid flag " + Str( valid ) + " is neither 0 nor 1";
	}
	if ( !std::isfinite( visibility ) ) {
		return " sky visibility: non-finite value " + Num( visibility );
	}
	if ( !( visibility >= 0.0f && visibility <= 1.0f ) ) {
		return " sky visibility: " + Num( visibility ) + " outside [0, 1]";
	}
	if ( valid == 0 && visibility != 0.0f ) {
		return " sky visibility: " + Num( visibility ) + " in an invalid probe (valid flag 0), expected 0";
	}
	for ( int i = 0; i < c_valuesPerLuxel; ++i ) {
		const std::string where = " coefficient " + Str( i / 3 ) + " channel " + Str( i % 3 );
		if ( !std::isfinite( coeffs[i] ) ) {
			return where + ": non-finite value " + Num( coeffs[i] );
		}
		if ( valid == 0 && coeffs[i] != 0.0f ) {
			return where + ": " + Num( coeffs[i] ) + " in an invalid probe (valid flag 0), expected 0";
		}
		if ( valid == 1 && i < 3 && coeffs[i] < 0.0f ) {
			return where + ": negative L0 component " + Num( coeffs[i] );
		}
	}
	return {};
}

std::string GridProblem( const SHGrid& grid ){
	if ( !VectorFinite( grid.origin ) ) {
		return "grid.origin: non-finite value " + VectorText( grid.origin );
	}
	if ( !VectorFinite( grid.spacing ) ) {
		return "grid.spacing: non-finite value " + VectorText( grid.spacing );
	}
	const uint32_t* dims = grid.dimensions;
	const int zeros = ( dims[0] == 0 ) + ( dims[1] == 0 ) + ( dims[2] == 0 );
	if ( zeros != 0 && zeros != 3 ) {
		return "grid.dimensions: " + DimensionsText( dims ) + " has some but not all components 0";
	}
	uint64_t probes = 0;
	if ( zeros == 0 ) {
		// Each factor is below 2^32, so x * y fits 64 bits; compare before
		// multiplying by z.
		const uint64_t xy = uint64_t( dims[0] ) * dims[1];
		if ( xy > std::numeric_limits<uint32_t>::max() / dims[2] ) {
			return "grid: probe count " + DimensionsText( dims ) + " does not fit in a uint32";
		}
		probes = xy * dims[2];
		for ( int i = 0; i < 3; ++i ) {
			if ( !( grid.spacing[i] > 0.0f ) ) {
				return "grid.spacing: " + VectorText( grid.spacing ) + " has a component not above 0";
			}
		}
	}
	if ( grid.coeffs.size() != probes * c_valuesPerLuxel ) {
		std::string have = Str( grid.coeffs.size() ) + " values";
		if ( grid.coeffs.size() % c_valuesPerLuxel == 0 ) {
			have += " (" + Str( grid.coeffs.size() / c_valuesPerLuxel ) + " probes)";
		}
		return "grid.coeffs: " + have + ", expected " + Str( probes * c_valuesPerLuxel ) + " (" + Str( probes ) + " probes of 27, " +
		       DimensionsText( dims ) + ")";
	}
	if ( grid.visibility.size() != probes ) {
		return "grid.visibility: " + Str( grid.visibility.size() ) + " values, expected " + Str( probes ) + " (" + DimensionsText( dims ) + ")";
	}
	if ( grid.valid.size() != probes ) {
		return "grid.valid: " + Str( grid.valid.size() ) + " flags, expected " + Str( probes ) + " (" + DimensionsText( dims ) + ")";
	}
	for ( size_t i = 0; i < probes; ++i ) {
		if ( std::string problem = ProbeValueProblem( &grid.coeffs[i * c_valuesPerLuxel], grid.visibility[i], grid.valid[i] ); !problem.empty() ) {
			return "grid probe " + Str( i ) + problem;
		}
	}
	return {};
}

std::string ExtensionProblem( const SHQ3Map2Extension& ext ){
	if ( std::string problem = CountProblem( ext.pages.size(), "pages" ); !problem.empty() ) {
		return problem;
	}
	for ( size_t p = 0; p < ext.pages.size(); ++p ) {
		if ( std::string problem = PageShapeProblem( ext.pages[p], p ); !problem.empty() ) {
			return problem;
		}
		if ( std::string problem = PageValueProblem( ext.pages[p], p ); !problem.empty() ) {
			return problem;
		}
	}
	if ( std::string problem = HullMeshProblem( ext.hull_mesh ); !problem.empty() ) {
		return problem;
	}
	return GridProblem( ext.grid );
}

// --- Serialization --------------------------------------------------------

void PutU16( uint16_t value, std::vector<uint8_t>& out ){
	out.push_back( static_cast<uint8_t>( value & 0xFF ) );
	out.push_back( static_cast<uint8_t>( value >> 8 ) );
}

void PutU32( uint32_t value, std::vector<uint8_t>& out ){
	for ( int shift = 0; shift < 32; shift += 8 ) {
		out.push_back( static_cast<uint8_t>( ( value >> shift ) & 0xFF ) );
	}
}

std::vector<uint8_t> Serialize( const SHQ3Map2Extension& ext ){
	const SHHullMesh& mesh = ext.hull_mesh;
	uint64_t total = c_headerBytes;
	for ( const SHQ3Map2ExtensionPage& page : ext.pages ) {
		total += c_pageHeaderBytes + c_bytesPerLuxel * page.visibility.size();
	}
	total += 8 * mesh.hulls.size() + 12 * mesh.positions.size() + 4 * mesh.indices.size();
	total += c_gridHeaderBytes + c_bytesPerProbe * ext.grid.valid.size();

	std::vector<uint8_t> out;
	out.reserve( total );
	out.insert( out.end(), c_magic, c_magic + 4 );
	PutU32( c_version, out );
	PutU32( static_cast<uint32_t>( ext.pages.size() ), out );
	PutU32( static_cast<uint32_t>( mesh.hulls.size() ), out );
	PutU32( static_cast<uint32_t>( mesh.positions.size() ), out );
	PutU32( static_cast<uint32_t>( mesh.indices.size() ), out );
	for ( const SHQ3Map2ExtensionPage& page : ext.pages ) {
		PutU32( static_cast<uint32_t>( page.width ), out );
		PutU32( static_cast<uint32_t>( page.height ), out );
		const size_t luxels = page.visibility.size();
		for ( int k = 0; k < 9; ++k ) {
			for ( size_t j = 0; j < luxels; ++j ) {
				for ( int c = 0; c < 3; ++c ) {
					PutU16( FloatToHalf( page.coeffs[( j * 9 + k ) * 3 + c] ), out );
				}
			}
		}
		for ( float vis : page.visibility ) {
			PutU16( FloatToHalf( vis ), out );
		}
	}
	for ( const SHHullRange& range : mesh.hulls ) {
		PutU32( range.first_index, out );
		PutU32( range.index_count, out );
	}
	for ( const Vector3& pos : mesh.positions ) {
		for ( int i = 0; i < 3; ++i ) {
			PutU32( std::bit_cast<uint32_t>( pos[i] ), out );
		}
	}
	for ( uint32_t index : mesh.indices ) {
		PutU32( index, out );
	}
	const SHGrid& grid = ext.grid;
	for ( const Vector3* v : { &grid.origin, &grid.spacing } ) {
		for ( int i = 0; i < 3; ++i ) {
			PutU32( std::bit_cast<uint32_t>( ( *v )[i] ), out );
		}
	}
	for ( uint32_t dim : grid.dimensions ) {
		PutU32( dim, out );
	}
	for ( float value : grid.coeffs ) {
		PutU32( std::bit_cast<uint32_t>( value ), out );
	}
	for ( float value : grid.visibility ) {
		PutU32( std::bit_cast<uint32_t>( value ), out );
	}
	out.insert( out.end(), grid.valid.begin(), grid.valid.end() );
	return out;
}

void RemoveQuietly( const std::filesystem::path& path ){
	std::error_code ec;
	std::filesystem::remove( path, ec );
}

// --- Reader ---------------------------------------------------------------

// Reads little-endian values; callers have checked that the bytes exist.
class ByteReader
{
	const std::vector<uint8_t>& m_bytes;
	size_t m_pos = 0;

public:
	explicit ByteReader( const std::vector<uint8_t>& bytes ) : m_bytes( bytes ){
	}
	void seek( size_t pos ){
		m_pos = pos;
	}
	uint16_t u16(){
		const uint16_t value = static_cast<uint16_t>( m_bytes[m_pos] | ( m_bytes[m_pos + 1] << 8 ) );
		m_pos += 2;
		return value;
	}
	uint32_t u32(){
		uint32_t value = 0;
		for ( int i = 0; i < 4; ++i ) {
			value |= static_cast<uint32_t>( m_bytes[m_pos + i] ) << ( 8 * i );
		}
		m_pos += 4;
		return value;
	}
	float f32(){
		return std::bit_cast<float>( u32() );
	}
};

bool HalfFinite( uint16_t bits ){
	return ( bits & 0x7C00 ) != 0x7C00;
}

bool ReadWholeFile( const char* path, std::vector<uint8_t>& bytes, std::string* error ){
	std::FILE* file = std::fopen( path, "rb" );
	if ( file == nullptr ) {
		SetError( error, std::string( "cannot open " ) + path + ": " + std::strerror( errno ) );
		return false;
	}
	std::error_code ec;
	const uintmax_t size = std::filesystem::file_size( path, ec );
	bool ok = !ec;
	if ( ok ) {
		bytes.resize( static_cast<size_t>( size ) );
		ok = bytes.empty() || std::fread( bytes.data(), 1, bytes.size(), file ) == bytes.size();
	}
	std::fclose( file );
	if ( !ok ) {
		SetError( error, std::string( "cannot read " ) + path + ( ec ? ": " + ec.message() : std::string() ) );
	}
	return ok;
}

bool NameStartsWithLm( const char* path ){
	const char* name = path;
	for ( const char* c = path; *c != '\0'; ++c ) {
		if ( *c == '/' || *c == '\\' ) {
			name = c + 1;
		}
	}
	return ( name[0] == 'l' || name[0] == 'L' ) && ( name[1] == 'm' || name[1] == 'M' ) && name[2] == '_';
}

// Encodes one pixel as Radiance RGBE (Ward's encoding).
void PutRgbe( float r, float g, float b, std::vector<uint8_t>& out ){
	const float largest = std::max( { r, g, b } );
	if ( largest < 1e-32f ) {
		out.insert( out.end(), { 0, 0, 0, 0 } );
		return;
	}
	int exponent;
	const float scale = std::frexp( largest, &exponent ) * 256.0f / largest;
	out.push_back( static_cast<uint8_t>( r * scale ) );
	out.push_back( static_cast<uint8_t>( g * scale ) );
	out.push_back( static_cast<uint8_t>( b * scale ) );
	out.push_back( static_cast<uint8_t>( exponent + 128 ) );
}

} // namespace

bool SaveSHQ3Map2Extension( const SHQ3Map2Extension& ext, const char* path, std::string* error ){
	if ( std::string problem = ExtensionProblem( ext ); !problem.empty() ) {
		SetError( error, "invalid input: " + problem );
		return false;
	}
	const std::vector<uint8_t> bytes = Serialize( ext );
	const std::filesystem::path target( path );
	std::filesystem::path tmp = target;
	tmp += ".tmp";

	std::FILE* file = std::fopen( tmp.string().c_str(), "wb" );
	if ( file == nullptr ) {
		SetError( error, "cannot create " + tmp.string() + " for " + path + ": " + std::strerror( errno ) );
		return false;
	}
	const bool written = std::fwrite( bytes.data(), 1, bytes.size(), file ) == bytes.size();
	const bool closed = std::fclose( file ) == 0;
	if ( !written || !closed ) {
		SetError( error, "cannot write " + tmp.string() + " for " + path + ": " + std::strerror( errno ) );
		RemoveQuietly( tmp );
		return false;
	}
	std::error_code ec;
	std::filesystem::rename( tmp, target, ec );
	if ( ec ) {
		SetError( error, "cannot rename " + tmp.string() + " to " + path + ": " + ec.message() );
		RemoveQuietly( tmp );
		return false;
	}
	return true;
}

std::optional<SHQ3Map2Extension> LoadSHQ3Map2Extension( const char* path, std::string* error ){
	std::vector<uint8_t> bytes;
	if ( !ReadWholeFile( path, bytes, error ) ) {
		return std::nullopt;
	}
	const uint64_t size = bytes.size();
	const auto fail = [&]( const std::string& reason ) -> std::optional<SHQ3Map2Extension> {
		SetError( error, std::string( path ) + ": " + reason );
		return std::nullopt;
	};
	const auto shorter = [&]( uint64_t expected ){
		return fail( "file is shorter than expected: needs at least " + Str( expected ) + " bytes, has " + Str( size ) );
	};

	if ( size < c_headerBytes ) {
		return shorter( c_headerBytes );
	}
	if ( std::memcmp( bytes.data(), c_magic, 4 ) != 0 ) {
		return fail( "bad magic" );
	}
	ByteReader in( bytes );
	in.seek( 4 );
	const uint32_t version = in.u32();
	if ( version != c_version ) {
		return fail( "unknown version " + Str( version ) + " (only version 3 is supported)" );
	}
	const uint64_t page_count = in.u32();
	const uint64_t hull_count = in.u32();
	const uint64_t vertex_count = in.u32();
	const uint64_t index_count = in.u32();

	// Sizes first, without allocating: every count is below 2^32, so none of
	// these sums can overflow 64 bits. `tail` is the hulls and the smallest
	// grid; the pages must end by `pages_end`.
	const uint64_t hull_bytes = 8 * hull_count + 12 * vertex_count + 4 * index_count;
	const uint64_t tail = hull_bytes + c_gridHeaderBytes;
	const uint64_t minimum = c_headerBytes + c_pageHeaderBytes * page_count + tail;
	if ( minimum > size ) {
		return shorter( minimum );
	}
	const uint64_t pages_end = size - tail;
	uint64_t pos = c_headerBytes;
	for ( uint64_t p = 0; p < page_count; ++p ) {
		if ( pages_end - pos < c_pageHeaderBytes ) {
			return shorter( pos + c_pageHeaderBytes + tail );
		}
		in.seek( pos );
		const uint64_t width = in.u32();
		const uint64_t height = in.u32();
		if ( width == 0 || height == 0 || width > c_maxPageSide || height > c_maxPageSide ) {
			return fail( "page " + Str( p ) + " has size " + Str( width ) + " x " + Str( height ) );
		}
		pos += c_pageHeaderBytes;
		// width * height < 2^62; compare before multiplying by the luxel size.
		const uint64_t luxels = width * height;
		if ( luxels > ( pages_end - pos ) / c_bytesPerLuxel ) {
			return fail( "file is shorter than expected: page " + Str( p ) + " (" + Str( width ) + " x " + Str( height ) + ") does not fit in " +
			             Str( size ) + " bytes" );
		}
		pos += luxels * c_bytesPerLuxel;
	}
	// The grid header is in the file, since pos <= pages_end.
	const uint64_t grid_pos = pos + hull_bytes;
	in.seek( grid_pos + 24 );
	uint32_t dims[3];
	for ( uint32_t& dim : dims ) {
		dim = in.u32();
	}
	const int zeros = ( dims[0] == 0 ) + ( dims[1] == 0 ) + ( dims[2] == 0 );
	if ( zeros != 0 && zeros != 3 ) {
		return fail( "grid dimensions " + DimensionsText( dims ) + " have some but not all components 0" );
	}
	uint64_t probe_count = 0;
	if ( zeros == 0 ) {
		// Bound each factor by what the rest of the file can hold before
		// multiplying, so nothing overflows.
		const uint64_t room = ( size - grid_pos - c_gridHeaderBytes ) / c_bytesPerProbe;
		const uint64_t xy = uint64_t( dims[0] ) * dims[1];
		if ( xy > room || dims[2] > room / xy ) {
			return fail( "file is shorter than expected: grid (" + DimensionsText( dims ) + ") does not fit in " + Str( size ) + " bytes" );
		}
		probe_count = xy * dims[2];
		if ( probe_count > std::numeric_limits<uint32_t>::max() ) {
			return fail( "grid probe count " + DimensionsText( dims ) + " does not fit in a uint32" );
		}
	}
	const uint64_t implied = grid_pos + c_gridHeaderBytes + c_bytesPerProbe * probe_count;
	if ( implied != size ) {
		return fail( "file is longer than expected: " + Str( implied ) + " bytes implied, has " + Str( size ) );
	}

	// The sizes match the file; decode, applying the writer's value rules.
	SHQ3Map2Extension ext;
	ext.pages.resize( page_count );
	in.seek( c_headerBytes );
	for ( uint64_t p = 0; p < page_count; ++p ) {
		SHQ3Map2ExtensionPage& page = ext.pages[p];
		page.width = static_cast<int>( in.u32() );
		page.height = static_cast<int>( in.u32() );
		const size_t luxels = size_t( page.width ) * size_t( page.height );
		page.coeffs.resize( luxels * c_valuesPerLuxel );
		page.visibility.resize( luxels );
		for ( int k = 0; k < 9; ++k ) {
			for ( size_t j = 0; j < luxels; ++j ) {
				for ( int c = 0; c < 3; ++c ) {
					const uint16_t bits = in.u16();
					const float value = HalfToFloat( bits );
					if ( !HalfFinite( bits ) || ( k == 0 && value < 0.0f ) ) {
						const std::string where =
						    "page " + Str( p ) + " luxel " + Str( j ) + " coefficient " + Str( k ) + " channel " + Str( c );
						if ( !HalfFinite( bits ) ) {
							return fail( "non-finite binary16 " + Hex16( bits ) + " at " + where );
						}
						return fail( "negative L0 component " + Num( value ) + " at " + where );
					}
					page.coeffs[( j * 9 + k ) * 3 + c] = value;
				}
			}
		}
		for ( size_t j = 0; j < luxels; ++j ) {
			const uint16_t bits = in.u16();
			const float value = HalfToFloat( bits );
			if ( !HalfFinite( bits ) || value < 0.0f || value > 1.0f ) {
				return fail( "visibility " + Hex16( bits ) + " (" + Num( value ) + ") outside [0, 1] at page " + Str( p ) + " luxel " + Str( j ) );
			}
			page.visibility[j] = value;
		}
	}

	SHHullMesh& mesh = ext.hull_mesh;
	mesh.hulls.resize( hull_count );
	uint64_t expected_first = 0;
	for ( uint64_t h = 0; h < hull_count; ++h ) {
		SHHullRange& range = mesh.hulls[h];
		range.first_index = in.u32();
		range.index_count = in.u32();
		if ( range.index_count == 0 || range.index_count % 3 != 0 ) {
			return fail( "hull " + Str( h ) + " has index count " + Str( range.index_count ) + ", not a positive multiple of 3" );
		}
		if ( range.first_index != expected_first ) {
			return fail( "hull " + Str( h ) + " starts at index " + Str( range.first_index ) + ", expected " + Str( expected_first ) );
		}
		expected_first += range.index_count;
	}
	if ( expected_first != index_count ) {
		return fail( "hull ranges cover " + Str( expected_first ) + " of " + Str( index_count ) + " indices" );
	}
	mesh.positions.resize( vertex_count );
	for ( uint64_t v = 0; v < vertex_count; ++v ) {
		Vector3& pos3 = mesh.positions[v];
		for ( int i = 0; i < 3; ++i ) {
			pos3[i] = in.f32();
		}
		if ( !std::isfinite( pos3.x() ) || !std::isfinite( pos3.y() ) || !std::isfinite( pos3.z() ) ) {
			return fail( "non-finite hull position (" + Num( pos3.x() ) + ", " + Num( pos3.y() ) + ", " + Num( pos3.z() ) + ") at vertex " + Str( v ) );
		}
	}
	mesh.indices.resize( index_count );
	for ( uint64_t i = 0; i < index_count; ++i ) {
		const uint32_t index = in.u32();
		if ( index >= vertex_count ) {
			return fail( "hull index " + Str( index ) + " at " + Str( i ) + " not below vertex count " + Str( vertex_count ) );
		}
		mesh.indices[i] = index;
	}

	SHGrid& grid = ext.grid;
	for ( Vector3* v : { &grid.origin, &grid.spacing } ) {
		for ( int i = 0; i < 3; ++i ) {
			( *v )[i] = in.f32();
		}
	}
	if ( !VectorFinite( grid.origin ) ) {
		return fail( "non-finite grid origin " + VectorText( grid.origin ) );
	}
	if ( !VectorFinite( grid.spacing ) ) {
		return fail( "non-finite grid spacing " + VectorText( grid.spacing ) );
	}
	if ( probe_count != 0 && !( grid.spacing.x() > 0.0f && grid.spacing.y() > 0.0f && grid.spacing.z() > 0.0f ) ) {
		return fail( "grid spacing " + VectorText( grid.spacing ) + " has a component not above 0" );
	}
	for ( int i = 0; i < 3; ++i ) {
		grid.dimensions[i] = dims[i];
	}
	in.seek( grid_pos + c_gridHeaderBytes );
	grid.coeffs.resize( probe_count * c_valuesPerLuxel );
	for ( float& value : grid.coeffs ) {
		value = in.f32();
	}
	grid.visibility.resize( probe_count );
	for ( float& value : grid.visibility ) {
		value = in.f32();
	}
	grid.valid.assign( bytes.end() - probe_count, bytes.end() );
	for ( uint64_t i = 0; i < probe_count; ++i ) {
		if ( std::string problem = ProbeValueProblem( &grid.coeffs[i * c_valuesPerLuxel], grid.visibility[i], grid.valid[i] ); !problem.empty() ) {
			return fail( "grid probe " + Str( i ) + problem );
		}
	}
	return ext;
}

bool SaveSHQ3Map2ExtensionPageL0Hdr( const SHQ3Map2ExtensionPage& page, const char* path, std::string* error ){
	if ( std::string problem = PageShapeProblem( page, 0 ); !problem.empty() ) {
		SetError( error, "invalid input: " + problem );
		return false;
	}
	if ( NameStartsWithLm( path ) ) {
		SetError( error, std::string( "refusing " ) + path + ": rend2 auto-loads lm_*.hdr files" );
		return false;
	}
	std::vector<uint8_t> out;
	const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " + std::to_string( page.height ) + " +X " + std::to_string( page.width ) + "\n";
	out.insert( out.end(), header.begin(), header.end() );
	for ( size_t j = 0; j < page.visibility.size(); ++j ) {
		const float* l0 = &page.coeffs[j * c_valuesPerLuxel];
		float rgb[3];
		for ( int c = 0; c < 3; ++c ) {
			if ( !std::isfinite( l0[c] ) ) {
				SetError( error, "invalid input: pages[0].coeffs: luxel " + Str( j ) + " coefficient 0 channel " + Str( c ) + ": non-finite value " +
				                 Num( l0[c] ) );
				return false;
			}
			// The range the extension itself stores; also keeps the RGBE exponent in a byte.
			rgb[c] = std::clamp( l0[c], 0.0f, 65504.0f );
		}
		PutRgbe( rgb[0], rgb[1], rgb[2], out );
	}
	std::FILE* file = std::fopen( path, "wb" );
	if ( file == nullptr ) {
		SetError( error, std::string( "cannot write " ) + path + ": " + std::strerror( errno ) );
		return false;
	}
	const bool written = std::fwrite( out.data(), 1, out.size(), file ) == out.size();
	const bool closed = std::fclose( file ) == 0;
	if ( !written || !closed ) {
		SetError( error, std::string( "cannot write " ) + path + ": " + std::strerror( errno ) );
		return false;
	}
	return true;
}

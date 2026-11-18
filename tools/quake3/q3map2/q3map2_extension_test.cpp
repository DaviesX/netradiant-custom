/* -------------------------------------------------------------------------------

   Tests for the q3map2 SH extension (q3map2_extension.h). Built and run by
   `make test-q3map2-extension`; exits 0 when every check passes.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   ------------------------------------------------------------------------------- */

#include "q3map2_extension.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using q3map2_extension_internal::FloatToHalf;
using q3map2_extension_internal::HalfToFloat;

namespace
{

// --- Harness --------------------------------------------------------------

int g_failures = 0;
int g_checks = 0;
const char* g_test = "";

void Report( bool ok, const char* file, int line, const std::string& what ){
	++g_checks;
	if ( !ok ) {
		++g_failures;
		std::fprintf( stderr, "%s:%d: [%s] check failed: %s\n", file, line, g_test, what.c_str() );
	}
}

#define CHECK( cond ) Report( ( cond ), __FILE__, __LINE__, #cond )
#define CHECK_MSG( cond, msg ) Report( ( cond ), __FILE__, __LINE__, std::string( #cond ) + " -- " + ( msg ) )
// Checks that `text` contains `part`, printing both if not.
#define CHECK_CONTAINS( text, part ) \
	Report( std::string( text ).find( part ) != std::string::npos, __FILE__, __LINE__, \
	        std::string( "\"" ) + ( text ) + "\" contains \"" + ( part ) + "\"" )

uint32_t Bits( float value ){
	return std::bit_cast<uint32_t>( value );
}

std::string Hex( uint32_t value ){
	char buffer[16];
	std::snprintf( buffer, sizeof( buffer ), "0x%x", value );
	return buffer;
}

// A fresh directory per test.
fs::path g_dir;

fs::path TestDir( const char* name ){
	std::error_code ec;
	fs::path dir = fs::temp_directory_path( ec ) / ( std::string( "q3map2_extension_test_" ) + name + "_" + std::to_string( getpid() ) );
	fs::remove_all( dir, ec );
	fs::create_directories( dir, ec );
	return dir;
}

std::string P( const fs::path& path ){
	return path.string();
}

std::vector<uint8_t> ReadBytes( const fs::path& path ){
	std::vector<uint8_t> bytes;
	if ( std::FILE* file = std::fopen( P( path ).c_str(), "rb" ) ) {
		int c;
		while ( ( c = std::fgetc( file ) ) != EOF ) {
			bytes.push_back( static_cast<uint8_t>( c ) );
		}
		std::fclose( file );
	}
	return bytes;
}

void WriteBytes( const fs::path& path, const std::vector<uint8_t>& bytes ){
	if ( std::FILE* file = std::fopen( P( path ).c_str(), "wb" ) ) {
		std::fwrite( bytes.data(), 1, bytes.size(), file );
		std::fclose( file );
	}
}

void PutU16At( std::vector<uint8_t>& bytes, size_t offset, uint16_t value ){
	bytes[offset] = value & 0xFF;
	bytes[offset + 1] = value >> 8;
}

void PutU32At( std::vector<uint8_t>& bytes, size_t offset, uint32_t value ){
	for ( int i = 0; i < 4; ++i ) {
		bytes[offset + i] = ( value >> ( 8 * i ) ) & 0xFF;
	}
}

size_t CountEntries( const fs::path& dir ){
	size_t entries = 0;
	std::error_code ec;
	for ( auto it = fs::directory_iterator( dir, ec ); it != fs::directory_iterator(); it.increment( ec ) ) {
		++entries;
	}
	return entries;
}

float Halfed( float value ){
	return HalfToFloat( FloatToHalf( value ) );
}

float& Coeff( SHQ3Map2ExtensionPage& page, size_t luxel, int k, int c ){
	return page.coeffs[( luxel * 9 + k ) * 3 + c];
}

// A valid page whose values vary by luxel, coefficient and channel.
SHQ3Map2ExtensionPage MakePage( int width, int height, float seed ){
	SHQ3Map2ExtensionPage page;
	page.width = width;
	page.height = height;
	const size_t luxels = size_t( width ) * size_t( height );
	page.coeffs.resize( luxels * 27 );
	page.visibility.resize( luxels );
	for ( size_t j = 0; j < luxels; ++j ) {
		for ( int k = 0; k < 9; ++k ) {
			for ( int c = 0; c < 3; ++c ) {
				const float v = seed + 0.37f * j + 0.11f * k + 0.05f * c;
				// L0 stays non-negative; higher bands take both signs.
				Coeff( page, j, k, c ) = ( k == 0 || ( j + k + c ) % 2 == 0 ) ? v : -v;
			}
		}
		page.visibility[j] = std::fmod( 0.13f * j + seed, 1.0f );
	}
	return page;
}

SHHullMesh TwoHullMesh(){
	SHHullMesh mesh;
	mesh.positions = { Vector3( 0, 0, 0 ), Vector3( 1, 0, 0 ), Vector3( 0, 1, 0 ), Vector3( 0, 0, 1 ), Vector3( 5, 5, 5 ) };
	mesh.indices = { 0, 1, 2, 0, 2, 3, 1, 3, 4 };
	mesh.hulls = { { 0, 6 }, { 6, 3 } };
	return mesh;
}

SHHullMesh TriangleMesh( std::vector<uint32_t> indices, std::vector<SHHullRange> hulls ){
	SHHullMesh mesh;
	mesh.positions = { Vector3( 0, 0, 0 ), Vector3( 1, 0, 0 ), Vector3( 0, 1, 0 ) };
	mesh.indices = std::move( indices );
	mesh.hulls = std::move( hulls );
	return mesh;
}

// The extension of the "Golden bytes" scenario.
SHQ3Map2Extension GoldenExtension(){
	SHQ3Map2Extension ext;
	SHQ3Map2ExtensionPage page;
	page.width = 1;
	page.height = 1;
	page.coeffs.resize( 27 );
	for ( int k = 0; k < 9; ++k ) {
		Coeff( page, 0, k, 0 ) = float( k );
		Coeff( page, 0, k, 1 ) = k + 0.5f;
		Coeff( page, 0, k, 2 ) = -float( k );
	}
	page.visibility = { 0.25f };
	ext.pages.push_back( page );
	ext.hull_mesh = TriangleMesh( { 0, 1, 2 }, { { 0, 3 } } );
	return ext;
}

// Byte offsets into the golden file.
constexpr size_t c_goldenWidth = 24;
constexpr size_t c_goldenCoeffs = 32; // coefficient k channel c: +6k + 2c
constexpr size_t c_goldenVisibility = 86;
constexpr size_t c_goldenVertices = 96;
constexpr size_t c_goldenIndices = 132;

void CheckSameValues( const SHQ3Map2Extension& written, const SHQ3Map2Extension& read ){
	CHECK( read.pages.size() == written.pages.size() );
	for ( size_t p = 0; p < written.pages.size() && p < read.pages.size(); ++p ) {
		const SHQ3Map2ExtensionPage& w = written.pages[p];
		const SHQ3Map2ExtensionPage& r = read.pages[p];
		CHECK( r.width == w.width && r.height == w.height );
		CHECK( r.coeffs.size() == w.coeffs.size() && r.visibility.size() == w.visibility.size() );
		if ( r.coeffs.size() != w.coeffs.size() || r.visibility.size() != w.visibility.size() ) {
			continue;
		}
		bool same = true;
		for ( size_t i = 0; i < w.coeffs.size(); ++i ) {
			same = same && Bits( r.coeffs[i] ) == Bits( Halfed( w.coeffs[i] ) );
		}
		for ( size_t j = 0; j < w.visibility.size(); ++j ) {
			same = same && Bits( r.visibility[j] ) == Bits( Halfed( w.visibility[j] ) );
		}
		CHECK_MSG( same, "page " + std::to_string( p ) + " values" );
	}
	const SHHullMesh& w = written.hull_mesh;
	const SHHullMesh& r = read.hull_mesh;
	CHECK( r.positions.size() == w.positions.size() );
	for ( size_t v = 0; v < w.positions.size() && v < r.positions.size(); ++v ) {
		CHECK( r.positions[v] == w.positions[v] );
	}
	CHECK( r.indices == w.indices );
	CHECK( r.hulls.size() == w.hulls.size() );
	for ( size_t h = 0; h < w.hulls.size() && h < r.hulls.size(); ++h ) {
		CHECK( r.hulls[h].first_index == w.hulls[h].first_index && r.hulls[h].index_count == w.hulls[h].index_count );
	}
}

// --- Half conversion ------------------------------------------------------

void TestEveryHalfRoundTrips(){
	int finite = 0, bad = 0;
	for ( uint32_t bits = 0; bits <= 0xFFFF; ++bits ) {
		if ( ( bits & 0x7C00 ) == 0x7C00 ) {
			continue;
		}
		++finite;
		if ( FloatToHalf( HalfToFloat( static_cast<uint16_t>( bits ) ) ) != bits ) {
			if ( ++bad <= 5 ) {
				CHECK_MSG( false, "half " + Hex( bits ) + " does not round-trip" );
			}
		}
	}
	CHECK( finite == 63488 );
	CHECK( bad == 0 );
}

void TestExactHalfValues(){
	const float smallest = std::ldexp( 1.0f, -24 );
	const float largest_subnormal = 1023 * smallest;
	for ( float v : { 0.0f, -0.0f, -0.5f, 1.0f, 2048.0f, 65504.0f, smallest, largest_subnormal } ) {
		CHECK_MSG( Bits( Halfed( v ) ) == Bits( v ), std::to_string( v ) );
	}
	CHECK( FloatToHalf( -0.0f ) == 0x8000 );
	CHECK( FloatToHalf( smallest ) == 0x0001 );
	CHECK( FloatToHalf( largest_subnormal ) == 0x03FF );
	CHECK( FloatToHalf( 65504.0f ) == 0x7BFF );
}

void TestHalfRoundingAndClamp(){
	const float tie = 1.0f + std::ldexp( 1.0f, -11 );
	const float tie_up = 1.0f + 3.0f * std::ldexp( 1.0f, -11 );
	const float above_tie = 1.0f + std::ldexp( 1.0f, -11 ) + std::ldexp( 1.0f, -20 );
	const float sub_tie = std::ldexp( 1.0f, -25 );
	const float sub_tie_up = 3.0f * std::ldexp( 1.0f, -25 );
	CHECK( Halfed( tie ) == 1.0f );
	CHECK( Halfed( tie_up ) == 1.0f + std::ldexp( 1.0f, -9 ) );
	CHECK( Halfed( above_tie ) == 1.0f + std::ldexp( 1.0f, -10 ) );
	CHECK( Bits( Halfed( sub_tie ) ) == Bits( 0.0f ) );
	CHECK( Bits( Halfed( -sub_tie ) ) == Bits( -0.0f ) );
	CHECK( Halfed( sub_tie_up ) == std::ldexp( 1.0f, -23 ) );
	CHECK( Halfed( 1e6f ) == 65504.0f );
	CHECK( Halfed( -1e6f ) == -65504.0f );
	CHECK( Halfed( 65519.0f ) == 65504.0f );
	CHECK( Halfed( std::numeric_limits<float>::max() ) == 65504.0f );
	// Rounding up out of the subnormal range gives the smallest normal.
	CHECK( FloatToHalf( std::ldexp( 1.0f, -14 ) * ( 1.0f - std::ldexp( 1.0f, -12 ) ) ) == 0x0400 );
}

// --- Writer ---------------------------------------------------------------

void TestGoldenBytes(){
	// Built by hand from the layout in q3map2_extension.h.
	const std::vector<uint8_t> expected = {
		// Header: magic, version 1, 1 page, 1 hull, 3 vertices, 3 indices.
		'S', 'H', 'S', 'F', 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0,
		// Page: width 1, height 1.
		1, 0, 0, 0, 1, 0, 0, 0,
		// Coefficient planes, (k, k + 0.5, -k) as binary16 R, G, B.
		0x00, 0x00, 0x00, 0x38, 0x00, 0x80, // 0: 0, 0.5, -0
		0x00, 0x3C, 0x00, 0x3E, 0x00, 0xBC, // 1: 1, 1.5, -1
		0x00, 0x40, 0x00, 0x41, 0x00, 0xC0, // 2: 2, 2.5, -2
		0x00, 0x42, 0x00, 0x43, 0x00, 0xC2, // 3: 3, 3.5, -3
		0x00, 0x44, 0x80, 0x44, 0x00, 0xC4, // 4: 4, 4.5, -4
		0x00, 0x45, 0x80, 0x45, 0x00, 0xC5, // 5: 5, 5.5, -5
		0x00, 0x46, 0x80, 0x46, 0x00, 0xC6, // 6: 6, 6.5, -6
		0x00, 0x47, 0x80, 0x47, 0x00, 0xC7, // 7: 7, 7.5, -7
		0x00, 0x48, 0x40, 0x48, 0x00, 0xC8, // 8: 8, 8.5, -8
		// Visibility plane: 0.25.
		0x00, 0x34,
		// Hull range: first index 0, count 3.
		0, 0, 0, 0, 3, 0, 0, 0,
		// Vertices (0,0,0), (1,0,0), (0,1,0) as float32; 1.0f is 0x3F800000.
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //
		0, 0, 0x80, 0x3F, 0, 0, 0, 0, 0, 0, 0, 0, //
		0, 0, 0, 0, 0, 0, 0x80, 0x3F, 0, 0, 0, 0, //
		// Indices 0, 1, 2.
		0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0 };
	CHECK( expected.size() == 24u + 64u + 8u + 36u + 12u );
	CHECK( expected.size() == 144u );
	const fs::path path = g_dir / "golden.shsf";
	std::string error;
	CHECK_MSG( SaveSHQ3Map2Extension( GoldenExtension(), P( path ).c_str(), &error ), error );
	CHECK( ReadBytes( path ) == expected );
}

// Expects the write of `ext` to fail with `message` and create nothing.
void CheckRefused( const SHQ3Map2Extension& ext, const std::string& message ){
	const fs::path path = g_dir / "refused.shsf";
	std::string error;
	CHECK( !SaveSHQ3Map2Extension( ext, P( path ).c_str(), &error ) );
	CHECK_CONTAINS( error, "invalid input: " );
	CHECK_CONTAINS( error, message );
	CHECK( !fs::exists( path ) );
	CHECK( CountEntries( g_dir ) == 0 );
}

SHQ3Map2Extension OnePage( int width, int height ){
	SHQ3Map2Extension ext;
	ext.pages.push_back( MakePage( width, height, 0.1f ) );
	return ext;
}

void TestWriterRefusals(){
	{
		SHQ3Map2Extension ext;
		ext.pages.push_back( MakePage( 1, 1, 0.1f ) );
		ext.pages.push_back( MakePage( 4, 2, 0.1f ) );
		ext.pages[1].coeffs.resize( 7 * 27 );
		CheckRefused( ext, "pages[1].coeffs: 189 values (7 luxels), expected 216 (8 luxels of 27, 4 x 2)" );
	}
	{
		SHQ3Map2Extension ext = OnePage( 4, 2 );
		ext.pages[0].visibility.resize( 9 );
		CheckRefused( ext, "pages[0].visibility: 9 values, expected 8 (4 x 2)" );
	}
	{
		SHQ3Map2Extension ext;
		SHQ3Map2ExtensionPage page;
		page.width = 0;
		page.height = 2;
		ext.pages.push_back( page );
		CheckRefused( ext, "pages[0].width: 0" );
	}
	{
		SHQ3Map2Extension ext = OnePage( 2, 1 );
		Coeff( ext.pages[0], 1, 2, 1 ) = std::numeric_limits<float>::quiet_NaN();
		CheckRefused( ext, "pages[0].coeffs: luxel 1 coefficient 2 channel 1: non-finite value nan" );
	}
	{
		SHQ3Map2Extension ext = OnePage( 2, 1 );
		ext.pages[0].visibility[1] = std::numeric_limits<float>::infinity();
		CheckRefused( ext, "pages[0].visibility[1]: non-finite value inf" );
	}
	{
		SHQ3Map2Extension ext = GoldenExtension();
		ext.hull_mesh.positions[1].y() = std::numeric_limits<float>::infinity();
		CheckRefused( ext, "hull_mesh.positions[1]: non-finite position (1, inf, 0)" );
	}
	{ // The bake's not-baked marker.
		SHQ3Map2Extension ext = OnePage( 2, 2 );
		for ( int i = 0; i < 27; ++i ) {
			ext.pages[0].coeffs[3 * 27 + i] = -1.0f;
		}
		ext.pages[0].visibility[3] = -1.0f;
		CheckRefused( ext, "pages[0].visibility[3]: -1 outside [0, 1]" );
	}
	{
		SHQ3Map2Extension ext = OnePage( 1, 1 );
		ext.pages[0].visibility[0] = 1.5f;
		CheckRefused( ext, "pages[0].visibility[0]: 1.5 outside [0, 1]" );
	}
	{
		SHQ3Map2Extension ext = OnePage( 1, 1 );
		Coeff( ext.pages[0], 0, 0, 2 ) = -0.25f;
		CheckRefused( ext, "pages[0].coeffs: luxel 0 coefficient 0 channel 2: negative L0 component -0.25" );
	}
	{
		SHQ3Map2Extension ext;
		ext.hull_mesh = TriangleMesh( { 0, 1, 2, 0 }, { { 0, 4 } } );
		CheckRefused( ext, "hull_mesh.hulls[0].index_count: 4 is not a positive multiple of 3" );
	}
	{
		SHQ3Map2Extension ext;
		ext.hull_mesh = TriangleMesh( { 0, 1, 2 }, { { 0, 0 }, { 0, 3 } } );
		CheckRefused( ext, "hull_mesh.hulls[0].index_count: 0 is not a positive multiple of 3" );
	}
	{
		SHQ3Map2Extension ext;
		ext.hull_mesh = TriangleMesh( { 0, 1, 2, 0, 1, 2, 0, 1, 2 }, { { 0, 3 }, { 6, 3 } } );
		CheckRefused( ext, "hull_mesh.hulls[1].first_index: 6, expected 3" );
	}
	{
		SHQ3Map2Extension ext;
		ext.hull_mesh = TriangleMesh( { 0, 1, 2, 0, 1, 2 }, { { 0, 3 }, { 0, 3 } } );
		CheckRefused( ext, "hull_mesh.hulls[1].first_index: 0, expected 3" );
	}
	{
		SHQ3Map2Extension ext;
		ext.hull_mesh = TriangleMesh( { 0, 1, 2, 0, 1, 2 }, { { 0, 3 } } );
		CheckRefused( ext, "hull_mesh.hulls: ranges cover 3 of 6 indices" );
	}
	{
		SHQ3Map2Extension ext = GoldenExtension();
		ext.hull_mesh.indices[2] = 3;
		CheckRefused( ext, "hull_mesh.indices[2]: index 3 not below vertex count 3" );
	}
}

void TestMissingDirectory(){
	const fs::path path = g_dir / "no_such_dir" / "town.shsf";
	std::string error;
	CHECK( !SaveSHQ3Map2Extension( GoldenExtension(), P( path ).c_str(), &error ) );
	CHECK_CONTAINS( error, "town.shsf" );
	CHECK( !fs::exists( g_dir / "no_such_dir" ) );
	CHECK( CountEntries( g_dir ) == 0 );
}

void TestRenameFailureCleansUp(){
	const fs::path target = g_dir / "town.shsf";
	fs::create_directory( target );
	WriteBytes( target / "keep.txt", { 'k' } );
	std::string error;
	CHECK( !SaveSHQ3Map2Extension( GoldenExtension(), P( target ).c_str(), &error ) );
	CHECK_CONTAINS( error, "cannot rename" );
	CHECK( fs::is_directory( target ) );
	CHECK( ReadBytes( target / "keep.txt" ) == std::vector<uint8_t>{ 'k' } );
	CHECK( !fs::exists( g_dir / "town.shsf.tmp" ) );
	CHECK( CountEntries( g_dir ) == 1 );
	CHECK( CountEntries( target ) == 1 );
}

void TestOverwriteReplacesBytes(){
	const fs::path path = g_dir / "town.shsf";
	const fs::path golden = g_dir / "golden.shsf";
	CHECK( SaveSHQ3Map2Extension( OnePage( 3, 3 ), P( path ).c_str(), nullptr ) );
	const std::vector<uint8_t> first = ReadBytes( path );
	CHECK( SaveSHQ3Map2Extension( GoldenExtension(), P( path ).c_str(), nullptr ) );
	CHECK( SaveSHQ3Map2Extension( GoldenExtension(), P( golden ).c_str(), nullptr ) );
	CHECK( ReadBytes( path ) != first );
	CHECK( ReadBytes( path ) == ReadBytes( golden ) );
	CHECK( !fs::exists( g_dir / "town.shsf.tmp" ) );
}

// --- Reader ---------------------------------------------------------------

std::optional<SHQ3Map2Extension> RoundTrip( const SHQ3Map2Extension& ext, const char* name ){
	const fs::path path = g_dir / name;
	std::string error;
	CHECK_MSG( SaveSHQ3Map2Extension( ext, P( path ).c_str(), &error ), error );
	std::optional<SHQ3Map2Extension> read = LoadSHQ3Map2Extension( P( path ).c_str(), &error );
	CHECK_MSG( read.has_value(), error );
	return read;
}

void TestRoundTripKeepsOrderAndSizes(){
	SHQ3Map2Extension ext;
	ext.pages.push_back( MakePage( 4, 2, 0.3f ) );
	ext.pages.push_back( MakePage( 1, 1, 7.0f ) );
	ext.pages.push_back( MakePage( 3, 5, 1.25f ) );
	ext.hull_mesh = TwoHullMesh();
	std::optional<SHQ3Map2Extension> read = RoundTrip( ext, "town.shsf" );
	CHECK( fs::file_size( g_dir / "town.shsf" ) == 24u + ( 8 + 56 * 8 ) + ( 8 + 56 * 1 ) + ( 8 + 56 * 15 ) + 8 * 2 + 12 * 5 + 4 * 9 );
	if ( !read ) {
		return;
	}
	CHECK( read->pages.size() == 3 );
	if ( read->pages.size() == 3 ) {
		CHECK( read->pages[0].width == 4 && read->pages[0].height == 2 );
		CHECK( read->pages[1].width == 1 && read->pages[1].height == 1 );
		CHECK( read->pages[2].width == 3 && read->pages[2].height == 5 );
	}
	CheckSameValues( ext, *read );
}

void TestEmptyPartsAllowed(){
	SHQ3Map2Extension hulls_only;
	hulls_only.hull_mesh = TwoHullMesh();
	if ( auto read = RoundTrip( hulls_only, "a.shsf" ) ) {
		CheckSameValues( hulls_only, *read );
	}
	SHQ3Map2Extension pages_only = OnePage( 2, 3 );
	if ( auto read = RoundTrip( pages_only, "b.shsf" ) ) {
		CheckSameValues( pages_only, *read );
	}
	if ( auto read = RoundTrip( SHQ3Map2Extension(), "c.shsf" ) ) {
		CHECK( read->pages.empty() && read->hull_mesh.positions.empty() && read->hull_mesh.hulls.empty() );
	}
	CHECK( fs::file_size( g_dir / "c.shsf" ) == 24u );
}

void TestWindingKept(){
	SHQ3Map2Extension ext;
	ext.hull_mesh = TriangleMesh( { 0, 1, 2 }, { { 0, 3 } } );
	if ( auto read = RoundTrip( ext, "w.shsf" ) ) {
		CHECK( read->hull_mesh.indices == ( std::vector<uint32_t>{ 0, 1, 2 } ) );
	}
}

// Writes `bytes` and expects the read to fail with a message containing `reason`.
void CheckBytesRejected( const std::vector<uint8_t>& bytes, const std::string& reason ){
	const fs::path path = g_dir / "bad.shsf";
	WriteBytes( path, bytes );
	std::string error;
	CHECK( !LoadSHQ3Map2Extension( P( path ).c_str(), &error ).has_value() );
	CHECK_CONTAINS( error, reason );
}

// Writes `ext`, applies `mutate` to its bytes and expects the read to fail.
void CheckRejected( const SHQ3Map2Extension& ext, const std::function<void( std::vector<uint8_t>& )>& mutate, const std::string& reason ){
	const fs::path path = g_dir / "bad.shsf";
	CHECK( SaveSHQ3Map2Extension( ext, P( path ).c_str(), nullptr ) );
	std::vector<uint8_t> bytes = ReadBytes( path );
	mutate( bytes );
	CheckBytesRejected( bytes, reason );
}

void TestReaderRejections(){
	{
		std::string error;
		CHECK( !LoadSHQ3Map2Extension( P( g_dir / "nothing.shsf" ).c_str(), &error ).has_value() );
		CHECK_CONTAINS( error, "cannot open" );
	}
	const SHQ3Map2Extension golden = GoldenExtension();
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ b[0] = 'X'; }, "bad magic" );
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ PutU32At( b, 4, 2 ); }, "unknown version 2" );
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ b.pop_back(); }, "shorter than expected" );
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ b.push_back( 0 ); }, "longer than expected: 144 bytes implied, has 145" );
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ PutU32At( b, c_goldenWidth, 0 ); }, "page 0 has size 0 x 1" );
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ PutU32At( b, c_goldenWidth + 4, 0 ); }, "page 0 has size 1 x 0" );
	{ // Every count 0xFFFFFFFF in a 24-byte file.
		std::vector<uint8_t> bytes( 24, 0xFF );
		std::memcpy( bytes.data(), "SHSF", 4 );
		PutU32At( bytes, 4, 1 );
		CheckBytesRejected( bytes, "shorter than expected: needs at least 137438953464 bytes, has 24" );
	}
	{ // One huge page in a 32-byte file.
		std::vector<uint8_t> bytes( 32, 0 );
		std::memcpy( bytes.data(), "SHSF", 4 );
		PutU32At( bytes, 4, 1 );
		PutU32At( bytes, 8, 1 );
		PutU32At( bytes, 24, 0xFFFFFFFF );
		PutU32At( bytes, 28, 0xFFFFFFFF );
		CheckBytesRejected( bytes, "page 0 has size 4294967295 x 4294967295" );
		// The largest sizes a page may have still fail the size walk.
		PutU32At( bytes, 24, 0x7FFFFFFF );
		PutU32At( bytes, 28, 0x7FFFFFFF );
		CheckBytesRejected( bytes, "page 0 (2147483647 x 2147483647) does not fit in 32 bytes" );
	}
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ PutU32At( b, c_goldenIndices + 8, 3 ); },
	               "hull index 3 at 2 not below vertex count 3" );
	SHQ3Map2Extension two_hulls;
	two_hulls.hull_mesh = TwoHullMesh();
	// With no pages, hull h's range is at 24 + 8h: (first, count).
	CheckRejected( two_hulls, []( std::vector<uint8_t>& b ){ PutU32At( b, 24 + 8, 7 ); }, "hull 1 starts at index 7, expected 6" );
	CheckRejected( two_hulls, []( std::vector<uint8_t>& b ){ PutU32At( b, 24 + 4, 0 ); }, "hull 0 has index count 0, not a positive multiple of 3" );
	CheckRejected( two_hulls, []( std::vector<uint8_t>& b ){ PutU32At( b, 24 + 12, 2 ); }, "hull 1 has index count 2, not a positive multiple of 3" );
	// Drop hull 1 from the header count and its range from the file.
	CheckRejected( two_hulls, []( std::vector<uint8_t>& b ){
		PutU32At( b, 12, 1 );
		b.erase( b.begin() + 32, b.begin() + 40 );
	}, "hull ranges cover 6 of 9 indices" );
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ PutU16At( b, c_goldenCoeffs + 6 * 3 + 2, 0x7C00 ); },
	               "non-finite binary16 0x7c00 at page 0 luxel 0 coefficient 3 channel 1" );
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ PutU16At( b, c_goldenCoeffs + 6, 0x7E00 ); },
	               "non-finite binary16 0x7e00 at page 0 luxel 0 coefficient 1 channel 0" );
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ PutU16At( b, c_goldenVisibility, 0x3E00 ); },
	               "visibility 0x3e00 (1.5) outside [0, 1] at page 0 luxel 0" );
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ PutU16At( b, c_goldenVisibility, 0xBC00 ); },
	               "visibility 0xbc00 (-1) outside [0, 1] at page 0 luxel 0" );
	// L0 green, 0.5, becomes -0.5.
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ PutU16At( b, c_goldenCoeffs + 2, 0xB800 ); },
	               "negative L0 component -0.5 at page 0 luxel 0 coefficient 0 channel 1" );
	CheckRejected( golden, []( std::vector<uint8_t>& b ){ PutU32At( b, c_goldenVertices + 12 + 4, 0x7FC00000 ); },
	               "non-finite hull position (1, nan, 0) at vertex 1" );
}

// --- L0 dump --------------------------------------------------------------

// Decodes a flat Radiance RGBE file as written by the dump; empty on failure.
std::vector<float> ReadHdr( const fs::path& path, int* width, int* height ){
	const std::vector<uint8_t> bytes = ReadBytes( path );
	const std::string text( bytes.begin(), bytes.end() );
	const std::string magic = "#?RADIANCE\n";
	const size_t blank = text.find( "\n\n" );
	if ( text.compare( 0, magic.size(), magic ) != 0 || blank == std::string::npos ) {
		return {};
	}
	const size_t line_end = text.find( '\n', blank + 2 );
	if ( line_end == std::string::npos || std::sscanf( text.c_str() + blank + 2, "-Y %d +X %d", height, width ) != 2 ) {
		return {};
	}
	const size_t data = line_end + 1;
	const size_t pixels = size_t( *width ) * size_t( *height );
	if ( bytes.size() != data + 4 * pixels ) {
		return {};
	}
	std::vector<float> rgb;
	for ( size_t i = 0; i < pixels; ++i ) {
		const uint8_t* px = &bytes[data + 4 * i];
		const float scale = px[3] == 0 ? 0.0f : std::ldexp( 1.0f, px[3] - ( 128 + 8 ) );
		for ( int c = 0; c < 3; ++c ) {
			rgb.push_back( px[c] * scale );
		}
	}
	return rgb;
}

SHQ3Map2ExtensionPage L0Page(){
	SHQ3Map2ExtensionPage page = MakePage( 4, 2, 0.0f );
	for ( int y = 0; y < 2; ++y ) {
		for ( int x = 0; x < 4; ++x ) {
			Coeff( page, y * 4 + x, 0, 0 ) = float( x );
			Coeff( page, y * 4 + x, 0, 1 ) = float( y );
			Coeff( page, y * 4 + x, 0, 2 ) = 1.0f;
		}
	}
	return page;
}

void TestDumpMatchesL0(){
	const fs::path path = g_dir / "l0_page0.hdr";
	std::string error;
	CHECK_MSG( SaveSHQ3Map2ExtensionPageL0Hdr( L0Page(), P( path ).c_str(), &error ), error );
	int width = 0, height = 0;
	const std::vector<float> rgb = ReadHdr( path, &width, &height );
	CHECK( width == 4 && height == 2 );
	CHECK( rgb.size() == 4 * 2 * 3 );
	if ( rgb.size() != 4 * 2 * 3 ) {
		return;
	}
	for ( int y = 0; y < 2; ++y ) {
		for ( int x = 0; x < 4; ++x ) {
			const float expected[3] = { float( x ), float( y ), 1.0f };
			const float tolerance = std::max( { expected[0], expected[1], expected[2] } ) / 128.0f;
			for ( int c = 0; c < 3; ++c ) {
				const float got = rgb[( y * 4 + x ) * 3 + c];
				CHECK_MSG( std::fabs( got - expected[c] ) <= tolerance,
				           "pixel (" + std::to_string( x ) + ", " + std::to_string( y ) + ") channel " + std::to_string( c ) + ": " + std::to_string( got ) );
			}
		}
	}
}

void TestDumpWritesNegativeL0AsZero(){
	SHQ3Map2ExtensionPage page = MakePage( 1, 1, 0.0f );
	Coeff( page, 0, 0, 0 ) = -2.0f;
	Coeff( page, 0, 0, 1 ) = 0.5f;
	Coeff( page, 0, 0, 2 ) = 0.25f;
	const fs::path path = g_dir / "neg.hdr";
	CHECK( SaveSHQ3Map2ExtensionPageL0Hdr( page, P( path ).c_str(), nullptr ) );
	int width = 0, height = 0;
	const std::vector<float> rgb = ReadHdr( path, &width, &height );
	CHECK( rgb.size() == 3 );
	if ( rgb.size() == 3 ) {
		CHECK( rgb[0] == 0.0f );
		CHECK( std::fabs( rgb[1] - 0.5f ) <= 0.5f / 128 );
		CHECK( std::fabs( rgb[2] - 0.25f ) <= 0.5f / 128 );
	}
}

void TestDumpRefusesRend2Names(){
	for ( const char* name : { "LM_0000.hdr", "lm_x.hdr", "Lm_1.hdr" } ) {
		std::string error;
		CHECK_MSG( !SaveSHQ3Map2ExtensionPageL0Hdr( L0Page(), P( g_dir / name ).c_str(), &error ), name );
		CHECK_CONTAINS( error, "lm_" );
		CHECK( !fs::exists( g_dir / name ) );
	}
	{ // A Windows separator also ends the directory part.
		std::string error;
		const std::string path = P( g_dir ) + "/sub\\lm_y.hdr";
		CHECK( !SaveSHQ3Map2ExtensionPageL0Hdr( L0Page(), path.c_str(), &error ) );
		CHECK_CONTAINS( error, "rend2" );
		CHECK( !fs::exists( path ) );
	}
	CHECK( CountEntries( g_dir ) == 0 );
	CHECK( SaveSHQ3Map2ExtensionPageL0Hdr( L0Page(), P( g_dir / "l0_lm_.hdr" ).c_str(), nullptr ) );
	CHECK( fs::exists( g_dir / "l0_lm_.hdr" ) );
}

void TestDumpMissingDirectory(){
	std::string error;
	CHECK( !SaveSHQ3Map2ExtensionPageL0Hdr( L0Page(), P( g_dir / "missing" / "l0.hdr" ).c_str(), &error ) );
	CHECK_CONTAINS( error, "cannot write" );
}

void TestDumpRefusesNonFiniteAndClampsHuge(){
	SHQ3Map2ExtensionPage page = MakePage( 1, 1, 0.0f );
	Coeff( page, 0, 0, 1 ) = std::numeric_limits<float>::infinity();
	std::string error;
	CHECK( !SaveSHQ3Map2ExtensionPageL0Hdr( page, P( g_dir / "inf.hdr" ).c_str(), &error ) );
	CHECK_CONTAINS( error, "pages[0].coeffs: luxel 0 coefficient 0 channel 1: non-finite value inf" );
	CHECK( !fs::exists( g_dir / "inf.hdr" ) );

	// 2^127 would overflow the RGBE exponent byte; it is written as 65504.
	Coeff( page, 0, 0, 0 ) = std::ldexp( 1.0f, 127 );
	Coeff( page, 0, 0, 1 ) = 1.0f;
	Coeff( page, 0, 0, 2 ) = 0.0f;
	const fs::path path = g_dir / "huge.hdr";
	CHECK_MSG( SaveSHQ3Map2ExtensionPageL0Hdr( page, P( path ).c_str(), &error ), error );
	int width = 0, height = 0;
	const std::vector<float> rgb = ReadHdr( path, &width, &height );
	CHECK( rgb.size() == 3 );
	if ( rgb.size() == 3 ) {
		CHECK_MSG( std::fabs( rgb[0] - 65504.0f ) <= 65504.0f / 128, std::to_string( rgb[0] ) );
	}
}

void TestDumpChecksPageShape(){
	SHQ3Map2ExtensionPage page = L0Page();
	page.coeffs.resize( 7 * 27 );
	std::string error;
	CHECK( !SaveSHQ3Map2ExtensionPageL0Hdr( page, P( g_dir / "bad.hdr" ).c_str(), &error ) );
	CHECK_CONTAINS( error, "pages[0].coeffs: 189 values (7 luxels), expected 216" );
	CHECK( !fs::exists( g_dir / "bad.hdr" ) );
}

} // namespace

int main(){
	const struct
	{
		const char* name;
		void ( *run )();
	} tests[] = {
		{ "EveryHalfRoundTrips", TestEveryHalfRoundTrips },
		{ "ExactHalfValues", TestExactHalfValues },
		{ "HalfRoundingAndClamp", TestHalfRoundingAndClamp },
		{ "GoldenBytes", TestGoldenBytes },
		{ "WriterRefusals", TestWriterRefusals },
		{ "MissingDirectory", TestMissingDirectory },
		{ "RenameFailureCleansUp", TestRenameFailureCleansUp },
		{ "OverwriteReplacesBytes", TestOverwriteReplacesBytes },
		{ "RoundTripKeepsOrderAndSizes", TestRoundTripKeepsOrderAndSizes },
		{ "EmptyPartsAllowed", TestEmptyPartsAllowed },
		{ "WindingKept", TestWindingKept },
		{ "ReaderRejections", TestReaderRejections },
		{ "DumpMatchesL0", TestDumpMatchesL0 },
		{ "DumpWritesNegativeL0AsZero", TestDumpWritesNegativeL0AsZero },
		{ "DumpRefusesRend2Names", TestDumpRefusesRend2Names },
		{ "DumpMissingDirectory", TestDumpMissingDirectory },
		{ "DumpRefusesNonFiniteAndClampsHuge", TestDumpRefusesNonFiniteAndClampsHuge },
		{ "DumpChecksPageShape", TestDumpChecksPageShape },
	};
	for ( const auto& test : tests ) {
		g_test = test.name;
		g_dir = TestDir( test.name );
		const int before = g_failures;
		test.run();
		std::printf( "%s %s\n", g_failures == before ? "PASS" : "FAIL", test.name );
		std::error_code ec;
		fs::remove_all( g_dir, ec );
	}
	std::printf( "q3map2_extension_test: %d checks, %d failed\n", g_checks, g_failures );
	return g_failures == 0 ? 0 : 1;
}

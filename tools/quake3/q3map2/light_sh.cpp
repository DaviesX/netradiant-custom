/* -------------------------------------------------------------------------------

   The -shbake stage of -light (B5): bakes the editor's physical lights with sh-baker
   on q3map2's own luxels, grid points and vertices. Built only under SH_BAKER=1,
   with sh-baker's headers and -march=native to match libsh_baker.so's Eigen
   alignment; light_sh_stub.cpp links in its place otherwise (design decision 13).

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.

   ------------------------------------------------------------------------------- */

#include "q3map2.h"
#include "q3map2_extension.h"
#include "surface_extra.h"
#include "timer.h"
#include "os/path.h"

#include <cmath>
#include <map>
#include <optional>
#include <random>
#include <set>

/* sh-baker (tools/sh-baker/src): the bake call, the q3map2 loader, area-light UV maps
   and the light tree (only to report what the bake will sample) */
#include "baker.h"
#include "light_tree.h"
#include "loader_q3map2.h"
#include "rasterizer.h"

#if defined( __cpp_rtti ) || defined( __GXX_RTTI )
#error "light_sh.cpp is built as the rest of q3map2 is, with -fno-rtti"
#endif
#if defined( __cpp_exceptions ) || defined( __EXCEPTIONS )
#error "light_sh.cpp is built as the rest of q3map2 is, with -fno-exceptions"
#endif

const bool shBakeAvailable = true;



/* -------------------------------------------------------------------------------

   the scene (design decisions 2, 7 and 9)

   ------------------------------------------------------------------------------- */

/* sh-baker sees positions times this, an exact power of two, so its fixed ray offsets
   (tnear 0.005 and 0.001) are 0.16 and 0.032 map units (design decision 2) */
constexpr float c_shSceneScale = 1.0f / 32.0f;
/* the bake hull's drawn planes move inward by this many map units, above both scaled offsets */
constexpr float c_shHullInset = 0.25f;
/* a nodraw face of the bake hull reaches this close to a drawn neighbour's real plane: not on
   it, where a ray along the plane from a luxel on it would graze the face's edge (task 6.5) */
constexpr float c_shHullSeamInset = 1.0f / 32.0f;
/* a noshader side whose un-shrunk winding is smaller than this (units^2) is a bevel */
constexpr float c_shBevelArea = 0.1f;

/* what -shbake builds once and every bake reads (task 6.1; the points are 6.2's) */
struct ShBakeScene
{
	sh_baker::Scene scene;          /* scaled space: positions x c_shSceneScale */
	SHHullMesh hulls;               /* the un-shrunk hulls in map units, for the side file (6.4) */
	int defaultMaterial = 0;        /* the material every baked point names (design decision 10) */
};

template<typename T>
static Eigen::Vector3f ShEigen( const BasicVector3<T>& v ){
	return Eigen::Vector3f( v[ 0 ], v[ 1 ], v[ 2 ] );
}

/* an emitting draw surface, waiting for its area light */
struct ShEmitter
{
	size_t geometry;                /* index into the scene's geometries */
	bool singleTile;                /* its UVs fit one tile: the textured emission CDF */
	const shaderInfo_t *si;
	std::vector<Vector3> positions; /* map units, for the attachment test (task 6.6) */
	std::vector<Vector3> normals;
	std::vector<std::uint32_t> indices;
};

/* the scene's counts, printed once it is built */
struct ShSceneCounts
{
	int surfaces = 0, emitterSurfaces = 0, triangles = 0, patches = 0;
	int skippedSkybox = 0, skippedFoliage = 0, skippedReceiveOnly = 0, skippedEmpty = 0;
	int hulls = 0, hullTriangles = 0, hullBevels = 0, hullsFailed = 0, brushesNotSolid = 0, brushesNotCasting = 0;
	int imagesLoaded = 0, imagesMissing = 0;
	int areaLightsTextured = 0, areaLightsUniform = 0, areaLightsSkipped = 0;
	int pointLights = 0, spotLights = 0, sunLights = 0;
	int areaEntitiesSkipped = 0, proxiesSkipped = 0;
};

/* one warning per unsupported input kind, naming the first entity or shader using it (spec) */
class ShUnsupported
{
	std::set<std::string> m_warned;
public:
	void warn( const char *kind, const std::string& user, const char *consequence ){
		if ( m_warned.insert( kind ).second ) {
			Sys_Warning( "-shbake doesn't read %s (%s); %s\n", kind, user.c_str(), consequence );
		}
	}
};

static std::string ShEntityName( const entity_t& e ){
	return StringStream( "entity ", int( &e - entities.data() ), " (", e.classname(), ')' ).c_str();
}

/* the shader as light_sh_model's rules see it */
static ShShaderDesc ShShaderDescFor( const shaderInfo_t& si ){
	ShShaderDesc desc;
	desc.name = si.shader.c_str();
	desc.scripted = si.sh.scripted;
	desc.parms = si.sh.parms;
	desc.stages = si.sh.stages;
	desc.baseColorFactor = si.sh.pbrBaseColorFactor;
	desc.emissiveStrength = si.sh.pbrEmissiveStrength;
	desc.emissiveStrengthDeclared = si.sh.pbrEmissiveStrengthSet;
	desc.skyFarbox = si.sh.skyFarbox;
	return desc;
}

/* a 1x1 texture */
static sh_baker::Texture ShTexture1x1( std::initializer_list<std::uint8_t> texel ){
	sh_baker::Texture texture;
	texture.width = texture.height = 1;
	texture.channels = texel.size();
	texture.pixel_data = texel;
	return texture;
}

/* builds the bake materials, one per used shader, and remembers each shader's rules */
class ShMaterials
{
	struct Entry
	{
		ShMaterialRules rules;
		int material = -1;          /* index into the scene's materials; -1 until a surface needs it */
	};
	std::map<const shaderInfo_t*, Entry> m_entries;
	std::set<std::string> m_missingImages;
	sh_baker::Scene& m_scene;
	ShSceneCounts& m_counts;

	/* an image as RGBA8, or nullptr with one warning per image (design decision 7) */
	const image_t *load( const std::string& name ){
		const image_t *image = ImageLoad( name.c_str() );
		if ( image == nullptr || image->pixels == nullptr || image->width <= 0 || image->height <= 0 ) {
			if ( m_missingImages.insert( name ).second ) {
				Sys_Warning( "-shbake: can't load image %s; the bake uses white in its place\n", name.c_str() );
				++m_counts.imagesMissing;
			}
			return nullptr;
		}
		++m_counts.imagesLoaded;
		return image;
	}

	/* the base stage's image times qer_pbr_baseColorFactor in linear space, alpha rewritten by its alphaFunc */
	sh_baker::Texture albedo( const ShMaterialRules& rules, const Vector3& factor ){
		const image_t *image = ( rules.baseImage.empty() || rules.baseImage == "_white" )? nullptr : load( rules.baseImage );
		sh_baker::Texture texture = ShTexture1x1( { 255, 255, 255, 255 } );
		if ( image != nullptr ) {
			texture.width = image->width;
			texture.height = image->height;
			texture.channels = 4;
			texture.pixel_data.assign( image->pixels, image->pixels + std::size_t( image->width ) * image->height * 4 );
		}
		if ( factor != Vector3( 1, 1, 1 ) ) {
			std::uint8_t encoded[ 3 ][ 256 ];
			for ( int c = 0; c < 3; ++c )
				for ( int b = 0; b < 256; ++b )
					encoded[ c ][ b ] = std::clamp<long>( std::lround( 255.0 * ShSrgbEncode( std::clamp( ShSrgbDecode( b / 255.0 ) * factor[ c ], 0.0, 1.0 ) ) ), 0, 255 );
			for ( std::size_t i = 0; i < texture.pixel_data.size(); i += 4 )
				for ( int c = 0; c < 3; ++c )
					texture.pixel_data[ i + c ] = encoded[ c ][ texture.pixel_data[ i + c ] ];
		}
		ShAlphaRewriteRGBA( rules.alphaFunc, texture.pixel_data.data(), texture.pixel_data.size() / 4 );
		return texture;
	}

	/* the emissive stage's image as RGB8: sh-baker's Flux() takes the largest of every channel */
	sh_baker::Texture emissive( const ShMaterialRules& rules ){
		const image_t *image = load( rules.emissiveImage );
		if ( image == nullptr ) {
			return ShTexture1x1( { 255, 255, 255 } );
		}
		sh_baker::Texture texture;
		texture.width = image->width;
		texture.height = image->height;
		texture.channels = 3;
		texture.pixel_data.resize( std::size_t( image->width ) * image->height * 3 );
		for ( std::size_t i = 0, n = std::size_t( image->width ) * image->height; i < n; ++i )
			std::copy_n( image->pixels + i * 4, 3, texture.pixel_data.data() + i * 3 );
		return texture;
	}

public:
	ShMaterials( sh_baker::Scene& scene, ShSceneCounts& counts ) : m_scene( scene ), m_counts( counts ){
	}

	const ShMaterialRules& rules( const shaderInfo_t& si ){
		auto [ it, inserted ] = m_entries.try_emplace( &si );
		if ( inserted ) {
			it->second.rules = ShDeriveMaterial( ShShaderDescFor( si ) );
		}
		return it->second.rules;
	}

	/* the shader's bake material, built on first use: Lambert-relevant fields only */
	int material( const shaderInfo_t& si ){
		const ShMaterialRules& r = rules( si );
		Entry& entry = m_entries[ &si ];
		if ( entry.material >= 0 ) {
			return entry.material;
		}
		sh_baker::Material mat;
		mat.name = si.shader.c_str();
		mat.albedo = albedo( r, si.sh.pbrBaseColorFactor );
		/* only satisfies sh-baker's CHECK should the advanced BRDF ever be reached: roughness 1 (G), metallic 0 (B) */
		mat.metallic_roughness_texture = ShTexture1x1( { 0, 255, 0 } );
		if ( r.emits() ) {
			mat.emissive_factor = ShEigen( r.emissiveColour );
			mat.emissive_strength = r.emissiveStrength;
			mat.emissive_texture = emissive( r );
		}
		/* additive: emits and never occludes. A shader that emits but doesn't cast under the
		   caster rule is added only as an emitter, so it is kept out of the BVH the same way */
		mat.additive = r.additive || !r.casts;
		entry.material = m_scene.materials.size();
		m_scene.materials.push_back( std::move( mat ) );
		return entry.material;
	}
};

/* the unsupported light inputs of a used shader (spec: one warning per kind) */
static void ShWarnShaderLightInputs( const shaderInfo_t& si, ShUnsupported& unsupported ){
	if ( !si.suns.empty() ) {
		unsupported.warn( "q3map_sun / q3map_sunExt", StringStream( "shader ", si.shader.c_str() ).c_str(), "use a light_sun entity" );
	}
	if ( !si.skylights.empty() ) {
		unsupported.warn( "q3map_skyLight", StringStream( "shader ", si.shader.c_str() ).c_str(), "it adds no light" );
	}
	if ( si.value > 0 ) {
		unsupported.warn( "q3map_surfaceLight", StringStream( "shader ", si.shader.c_str() ).c_str(), "use an additive stage with qer_pbr_emissiveStrength" );
	}
}

/* every casting or emitting draw surface of every model (design decision 7); returns the
   emitting ones */
static std::vector<ShEmitter> ShAddDrawSurfaces( ShBakeScene& bake, ShMaterials& materials, ShUnsupported& unsupported, ShSceneCounts& counts ){
	std::vector<ShEmitter> emitters;
	for ( size_t num = 0; num < bspDrawSurfaces.size(); ++num )
	{
		const bspDrawSurface_t& ds = bspDrawSurfaces[ num ];
		const surfaceInfo_t& info = surfaceInfos[ num ];
		if ( info.si == nullptr ) {
			continue;
		}

		/* _skybox copies sit in the portal sky's space, and the bake has no skybox node */
		if ( info.parentSurfaceNum >= 0 ) {
			++counts.skippedSkybox;
			continue;
		}
		if ( ds.surfaceType == MST_FOLIAGE ) {
			++counts.skippedFoliage;
			continue;
		}
		if ( ds.surfaceType != MST_PLANAR && ds.surfaceType != MST_TRIANGLE_SOUP && ds.surfaceType != MST_PATCH ) {
			continue;
		}

		ShWarnShaderLightInputs( *info.si, unsupported );

		const ShMaterialRules& rules = materials.rules( *info.si );
		if ( !rules.casts && !rules.emits() ) {
			++counts.skippedReceiveOnly;
			continue;
		}

		sh_baker::Q3Map2Surface surface;
		std::vector<Vector3> positions, normals;
		const auto addVert = [&]( const bspDrawVert_t& dv ){
			positions.push_back( dv.xyz );
			normals.push_back( dv.normal );
			surface.positions.push_back( ShEigen( dv.xyz ) * c_shSceneScale );
			surface.normals.push_back( ShEigen( dv.normal ) );
			surface.texture_uvs.emplace_back( dv.st[ 0 ], dv.st[ 1 ] );
		};
		if ( ds.surfaceType == MST_PATCH ) {
			/* the tessellation q3map2's tracer uses for patch shadows (PopulateWithBSPModel) */
			const mesh_t mesh = TessellatedMesh( mesh_view_t( ds.patchWidth, ds.patchHeight, &yDrawVerts[ ds.firstVert ] ), info.patchIterations );
			const mesh_view_t view = mesh;
			for ( int i = 0; i < view.width * view.height; ++i )
				addVert( view.verts[ i ] );
			for ( MeshQuadIterator it( view ); it; ++it )
			{
				const std::array<int, 4>& q = it.idx();
				surface.indices.insert( surface.indices.end(), { std::uint32_t( q[ 0 ] ), std::uint32_t( q[ 1 ] ), std::uint32_t( q[ 2 ] ),
				                                                 std::uint32_t( q[ 0 ] ), std::uint32_t( q[ 2 ] ), std::uint32_t( q[ 3 ] ) } );
			}
			++counts.patches;
		}
		else{
			for ( const bspDrawVert_t& dv : Span( &yDrawVerts[ ds.firstVert ], ds.numVerts ) )
				addVert( dv );
			for ( const int index : Span( &bspDrawIndexes[ ds.firstIndex ], ds.numIndexes ) )
				surface.indices.push_back( index );
		}
		if ( surface.positions.empty() || surface.indices.size() < 3 ) {
			++counts.skippedEmpty;
			continue;
		}

		surface.material_id = materials.material( *info.si );

		/* an emitter whose UVs fit one tile gets the textured emission CDF; sh-baker
		   rasterises raw UVs, so the tile is taken off (design decision 7) */
		bool singleTile = false;
		if ( rules.emits() ) {
			std::vector<Vector2> uvs;
			uvs.reserve( surface.texture_uvs.size() );
			for ( const Eigen::Vector2f& uv : surface.texture_uvs )
				uvs.emplace_back( uv.x(), uv.y() );
			const ShUVTile tile = ShSingleTileUV( uvs, surface.indices );
			if ( ( singleTile = tile.single ) ) {
				for ( Eigen::Vector2f& uv : surface.texture_uvs )
					uv -= Eigen::Vector2f( tile.u, tile.v );
			}
		}

		counts.triangles += surface.indices.size() / 3;
		++counts.surfaces;
		if ( rules.emits() ) {
			++counts.emitterSurfaces;
			emitters.push_back( { bake.scene.geometries.size(), singleTile, info.si, std::move( positions ), std::move( normals ), surface.indices } );
		}
		sh_baker::AddQ3Map2Surface( surface, &bake.scene );
	}
	return emitters;
}

/* the origin of the entity whose model is *modelNum, as SetEntityOrigins applies it */
static Vector3 ShModelOrigin( int modelNum ){
	if ( modelNum == 0 ) {
		return g_vector3_identity;
	}
	for ( const entity_t& e : entities )
	{
		const char *model = e.valueForKey( "model" );
		if ( model[ 0 ] == '*' && atoi( model + 1 ) == modelNum ) {
			return e.vectorForKey( "origin" );
		}
	}
	return g_vector3_identity;
}

/* the convex polygon of plane i of a brush, chopped by every other plane, or empty */
static winding_t ShBrushWinding( const std::vector<Plane3>& planes, size_t i ){
	winding_accu_t w = BaseWindingForPlaneAccu( planes[ i ] );
	for ( size_t j = 0; j < planes.size() && w.size() >= 3; ++j )
	{
		if ( j != i ) {
			ChopWindingInPlaceAccu( w, plane3_flipped( planes[ j ] ), 0 );
		}
	}
	return w.size() >= 3? CopyWindingAccuToRegular( w ) : winding_t();
}

/* one closed hull per qualifying brush of every model (design decision 7): the un-shrunk one
   in map units for the side file, and one with every drawn side moved in by c_shHullInset as a
   pure occluder. A nodraw side (caulk) has no draw surface to be hit first, so it stays, and its
   polygon reaches to within c_shHullSeamInset of a drawn neighbour's real plane: two
   caulk-backed brushes meeting at a seam then leave no gap between their bake hulls, and no
   0.25-unit strip opens beside a drawn face (task 6.5) */
static void ShAddHulls( ShBakeScene& bake, ShMaterials& materials, ShSceneCounts& counts ){
	for ( size_t modelNum = 0; modelNum < bspModels.size(); ++modelNum )
	{
		const bspModel_t& model = bspModels[ modelNum ];
		const Vector3 origin = ShModelOrigin( modelNum );
		for ( int b = model.firstBSPBrush; b < model.firstBSPBrush + model.numBSPBrushes; ++b )
		{
			const bspBrush_t& brush = bspBrushes[ b ];
			if ( !( ShaderInfoForShader( bspShaders[ brush.shaderNum ].shader ).compileFlags & C_SOLID ) ) {
				++counts.brushesNotSolid;
				continue;
			}

			/* the planes, moved by the model's origin */
			const Span<const bspBrushSide_t> sides( &bspBrushSides[ brush.firstSide ], brush.numSides );
			std::vector<Plane3> planes;
			for ( const bspBrushSide_t& side : sides )
			{
				Plane3 plane( bspPlanes[ side.planeNum ] );
				plane.dist() += vector3_dot( plane.normal(), DoubleVector3( origin ) );
				planes.push_back( plane );
			}

			/* the un-shrunk hull, bevels and the side shaders */
			std::vector<winding_t> windings;
			std::vector<bool> bevel;
			bool casts = true;
			for ( size_t i = 0; i < sides.size(); ++i )
			{
				windings.push_back( ShBrushWinding( planes, i ) );
				bevel.push_back( striEqual( bspShaders[ sides[ i ].shaderNum ].shader, "noshader" )
				              && ( windings[ i ].size() < 3 || WindingArea( windings[ i ] ) < c_shBevelArea ) );
				if ( !bevel[ i ] ) {
					const ShMaterialRules& rules = materials.rules( ShaderInfoForShader( bspShaders[ sides[ i ].shaderNum ].shader ) );
					casts &= rules.casts && !rules.alphaTested();
				}
			}
			if ( !casts ) {
				++counts.brushesNotCasting;
				continue;
			}

			/* the faces, chopped by the non-bevel planes only: a bevel touches the hull at an edge or
			   corner, and chopping by it adds only collinear points; and the bake hull, with every
			   drawn non-bevel plane moved inward and the nodraw ones in place; a nodraw face is
			   chopped by the drawn planes moved in only by c_shHullSeamInset, so it reaches almost to
			   a drawn neighbour's real plane and leaves no 0.25-unit strip under the drawn face's
			   margin for light to run along (task 6.5). The bake hull is only an occluder, so its
			   faces needn't meet edge to edge */
			std::vector<Plane3> faces, inset, seam;
			std::vector<bool> nodraw;
			for ( size_t i = 0; i < sides.size(); ++i )
			{
				if ( !bevel[ i ] ) {
					nodraw.push_back( ShaderInfoForShader( bspShaders[ sides[ i ].shaderNum ].shader ).compileFlags & C_NODRAW );
					faces.push_back( planes[ i ] );
					inset.push_back( Plane3( planes[ i ].normal(), planes[ i ].dist() - ( nodraw.back()? 0 : c_shHullInset ) ) );
					seam.push_back( Plane3( planes[ i ].normal(), planes[ i ].dist() - ( nodraw.back()? 0 : c_shHullSeamInset ) ) );
				}
			}

			sh_baker::Q3Map2Surface occluder;
			SHHullMesh mesh;
			int faceCount = 0, insetFaces = 0;
			for ( size_t i = 0, k = 0; i < sides.size(); ++i )
			{
				if ( bevel[ i ] ) {
					++counts.hullBevels;
					continue;
				}
				/* fans in q3map2's winding: clockwise seen from outside */
				const winding_t w = ShBrushWinding( faces, k );
				if ( w.size() >= 3 ) {
					++faceCount;
					const uint32_t first = mesh.positions.size();
					mesh.positions.insert( mesh.positions.end(), w.begin(), w.end() );
					for ( size_t v = 1; v + 1 < w.size(); ++v )
						mesh.indices.insert( mesh.indices.end(), { first, uint32_t( first + v ), uint32_t( first + v + 1 ) } );
				}
				const winding_t wi = ShBrushWinding( nodraw[ k ]? seam : inset, k );
				++k;
				if ( wi.size() >= 3 ) {
					++insetFaces;
					const uint32_t first = occluder.positions.size();
					for ( const Vector3& p : wi )
					{
						occluder.positions.push_back( ShEigen( p ) * c_shSceneScale );
						occluder.normals.push_back( ShEigen( planes[ i ].normal() ) );
					}
					for ( size_t v = 1; v + 1 < wi.size(); ++v )
						occluder.indices.insert( occluder.indices.end(), { first, uint32_t( first + v ), uint32_t( first + v + 1 ) } );
				}
			}
			if ( faceCount < 4 || insetFaces < 4 ) {
				Sys_Warning( "-shbake: brush %d (model %zu) makes no closed hull (%d of %d faces, %d after the %g unit inset); skipped, its draw surfaces still occlude\n",
				             b, modelNum, faceCount, brush.numSides, insetFaces, c_shHullInset );
				++counts.hullsFailed;
				continue;
			}

			/* the side file's hull, appended as one range */
			const uint32_t firstVertex = bake.hulls.positions.size();
			bake.hulls.hulls.push_back( { uint32_t( bake.hulls.indices.size() ), uint32_t( mesh.indices.size() ) } );
			bake.hulls.positions.insert( bake.hulls.positions.end(), mesh.positions.begin(), mesh.positions.end() );
			for ( const uint32_t index : mesh.indices )
				bake.hulls.indices.push_back( firstVertex + index );

			occluder.material_id = -1;
			counts.hullTriangles += occluder.indices.size() / 3;
			++counts.hulls;
			sh_baker::AddQ3Map2Surface( occluder, &bake.scene );
		}
	}
}

/* one area light per emitting surface, mirroring the glTF loader's ProcessAreaLights
   (design decision 7); after every surface is added, since lights point into the geometries */
static void ShAddAreaLights( ShBakeScene& bake, const std::vector<ShEmitter>& emitters, ShSceneCounts& counts ){
	for ( const ShEmitter& emitter : emitters )
	{
		const sh_baker::Geometry& geo = bake.scene.geometries[ emitter.geometry ];
		const sh_baker::Material& mat = bake.scene.materials[ geo.material_id ];
		sh_baker::Light light;
		light.type = sh_baker::Light::Type::Area;
		light.intensity = mat.emissive_strength;
		light.color = mat.emissive_factor;
		light.material = &mat;
		light.geometry = &geo;
		light.area = sh_baker::SurfaceArea( geo );
		if ( geo.indices.empty() || !( light.area > 0 ) ) {
			++counts.areaLightsSkipped;
			continue;
		}

		const sh_baker::Texture& tex = *mat.emissive_texture;
		if ( emitter.singleTile ) {
			/* one tile, no overlap: the textured emission CDF */
			sh_baker::RasterConfig raster;
			raster.width = tex.width;
			raster.height = tex.height;
			sh_baker::GeometryUVMaps maps = sh_baker::RasterizeGeometryUVMaps( geo, raster );
			light.uv_to_world_area_ratio = std::move( maps.uv_to_world_area_ratio );
			light.prim_id_map = std::move( maps.prim_id_map );
			if ( std::optional<sh_baker::EmissionCDF> cdf = sh_baker::ComputeTextureEmissionCDF( tex, *light.uv_to_world_area_ratio ) ) {
				light.emission_cdf = std::move( cdf );
				light.flux = sh_baker::Flux( light );
				++counts.areaLightsTextured;
				bake.scene.lights.push_back( std::move( light ) );
				continue;
			}
			light.uv_to_world_area_ratio.reset();
			light.prim_id_map.reset();
		}

		/* uniform sampling: flux from the mean texel, alpha ignored, since RasterizeGeometryUVMaps
		   keeps one triangle per texel and a tiled emitter would lose power */
		double mean[ 3 ] = { 0, 0, 0 };
		const std::size_t texels = std::size_t( tex.width ) * tex.height;
		for ( std::size_t i = 0; i < texels; ++i )
			for ( int c = 0; c < 3; ++c )
				mean[ c ] += ShSrgbDecode( tex.pixel_data[ i * tex.channels + c ] / 255.0 );
		float maxComponent = 0;
		for ( int c = 0; c < 3; ++c )
			maxComponent = std::max( maxComponent, float( mat.emissive_factor[ c ] * mean[ c ] / texels ) );
		light.flux = mat.emissive_strength * maxComponent * light.area;
		++counts.areaLightsUniform;
		bake.scene.lights.push_back( std::move( light ) );
	}
}

/* the editor's light entities (design decision 9) and the warnings for what the bake doesn't read */
static void ShAddEntityLights( ShBakeScene& bake, ShUnsupported& unsupported, ShSceneCounts& counts ){
	const entity_t *firstSun = nullptr;
	for ( const entity_t& e : entities )
	{
		if ( !e.classname_prefixed( "light" ) ) {
			continue;
		}
		/* light_area and _proxy lights stand in for an emitter the bake already holds (task 6.6) */
		if ( ShIsAttachedEmitterLight( e.classname(), [&e]( const char *key ){ return e.valueForKey( key ); } ) ) {
			++( ShIsAreaLightClassname( e.classname() )? counts.areaEntitiesSkipped : counts.proxiesSkipped );
			continue;
		}
		const std::optional<ShLightType> type = ShLightTypeFromClassname( e.classname() );
		if ( !type ) {
			unsupported.warn( "light entities other than light, light_spot, light_sun and light_area", ShEntityName( e ), "it adds no light" );
			continue;
		}
		if ( !strEmpty( e.valueForKey( "spawnflags" ) ) ) {
			unsupported.warn( "a light's spawnflags", ShEntityName( e ), "they have no effect" );
		}
		if ( !strEmpty( e.valueForKey( "fade" ) ) ) {
			unsupported.warn( "a light's fade", ShEntityName( e ), "it has no effect" );
		}
		if ( *type == ShLightType::Sun ) {
			if ( firstSun != nullptr ) {
				Sys_Warning( "-shbake uses only the first light_sun, %s; %s adds no light\n", ShEntityName( *firstSun ).c_str(), ShEntityName( e ).c_str() );
				continue;
			}
			firstSun = &e;
		}

		const char *target = e.valueForKey( "target" );
		const entity_t *targetEntity = strEmpty( target )? nullptr : FindTargetEntity( target );
		const Vector3 targetOrigin = targetEntity != nullptr? targetEntity->vectorForKey( "origin" ) : g_vector3_identity;
		const ShLight l = ShLightFromKeys( *type, e.vectorForKey( "origin" ), [&e]( const char *key ){ return e.valueForKey( key ); },
		                                   targetEntity != nullptr? &targetOrigin : nullptr );
		if ( !( l.intensity > 0 ) ) {
			Sys_Warning( "-shbake: %s has intensity %g; the bake takes no light of zero or negative intensity\n", ShEntityName( e ).c_str(), l.intensity );
			continue;
		}

		/* every light casts: sh-baker has no per-light shadow switch (design decision 9) */
		sh_baker::Light light;
		light.color = ShEigen( l.colour );
		switch ( *type )
		{
		case ShLightType::Point:
			light.type = sh_baker::Light::Type::Point;
			++counts.pointLights;
			break;
		case ShLightType::Spot:
			light.type = sh_baker::Light::Type::Spot;
			light.cos_inner_cone = l.cosInner;
			light.cos_outer_cone = l.cosOuter;
			light.cone_falloff = sh_baker::Light::ConeFalloff::kSmoothstep;
			++counts.spotLights;
			break;
		case ShLightType::Sun:
			light.type = sh_baker::Light::Type::Directional;
			++counts.sunLights;
			break;
		}
		light.direction = ShEigen( l.direction );
		if ( *type == ShLightType::Sun ) {
			/* irradiance is unchanged by the scaling */
			light.intensity = l.intensity;
		}
		else{
			/* radiant intensity times s^2 and range times s give the unscaled irradiance (design decision 2) */
			light.position = ShEigen( l.origin ) * c_shSceneScale;
			light.intensity = l.radiantIntensity * c_shSceneScale * c_shSceneScale;
			light.range = l.range * c_shSceneScale;
		}
		light.flux = sh_baker::Flux( light );
		bake.scene.lights.push_back( std::move( light ) );
	}
}

/* one warning per emissive shader with surfaces that no light_area or _proxy light stands in
   for (spec "Attached emitter lights skipped"); the bake lights from them either way */
static void ShWarnUnattachedEmitters( const std::vector<ShEmitter>& emitters ){
	std::vector<ShAreaRect> rects;
	std::vector<Vector3> proxies;
	for ( const entity_t& e : entities )
	{
		const auto key = [&e]( const char *k ){ return e.valueForKey( k ); };
		if ( ShIsAreaLightClassname( e.classname() ) ) {
			rects.push_back( ShAreaRectFromKeys( e.vectorForKey( "origin" ), key ) );
		}
		else if ( ShIsAttachedEmitterLight( e.classname(), key ) ) {
			proxies.push_back( e.vectorForKey( "origin" ) );
		}
	}

	std::map<std::string, int> unattached;
	for ( const ShEmitter& emitter : emitters )
	{
		const bool attached = std::any_of( rects.cbegin(), rects.cend(), [&emitter]( const ShAreaRect& rect ){
			return ShAreaRectAttached( rect, emitter.positions, emitter.normals, emitter.indices );
		} ) || std::any_of( proxies.cbegin(), proxies.cend(), [&emitter]( const Vector3& origin ){
			return ShProxyAttached( origin, emitter.positions );
		} );
		if ( !attached ) {
			++unattached[ emitter.si->shader.c_str() ];
		}
	}
	for ( const auto& [ shader, count ] : unattached )
	{
		Sys_Warning( "-shbake: %d %s of emissive shader %s %s no light_area or _proxy light attached; the bake lights from %s, the editor preview and renderer_sh don't\n",
		             count, count == 1? "surface" : "surfaces", shader.c_str(), count == 1? "has" : "have", count == 1? "it" : "them" );
	}
}

/* worldspawn's and other entities' q3map2-only lighting keys (spec) */
static void ShWarnEntityKeys( ShUnsupported& unsupported ){
	const entity_t& world = entities[ 0 ];
	for ( const char *key : { "_minlight", "_minvertexlight", "_mingridlight", "_maxlight", "_ambient", "ambient" } )
	{
		if ( !strEmpty( world.valueForKey( key ) ) ) {
			unsupported.warn( StringStream( "worldspawn ", key ).c_str(), "worldspawn", "it has no effect" );
		}
	}
	for ( const entity_t& e : Span( entities.data() + 1, entities.size() - 1 ) )
	{
		if ( !strEmpty( e.valueForKey( "_ambient" ) ) || !strEmpty( e.valueForKey( "ambient" ) ) ) {
			unsupported.warn( "_ambient on an entity", ShEntityName( e ), "it has no effect" );
		}
	}
	/* func_group and other brush entities merged into worldspawn at -bsp leave no entity behind,
	   but their _ambient reaches their surfaces through the .srf file; a surface that sets none
	   reads back the .srf default, worldspawn's own ambient (GetSurfaceExtra( -1 )), already warned */
	const Vector3 worldAmbient = GetSurfaceExtra( -1 ).ambientColor;
	for ( size_t num = 0; num < bspDrawSurfaces.size(); ++num )
	{
		const surfaceExtra_t& se = GetSurfaceExtra( num );
		if ( se.entityNum != 0 && se.ambientColor != g_vector3_identity && se.ambientColor != worldAmbient ) {
			unsupported.warn( "_ambient on an entity", StringStream( "map entity ", se.entityNum ).c_str(), "it has no effect" );
		}
	}
}

/*
   ShBuildScene()
   builds the bake scene in scaled space (design decisions 2, 7 and 9): casting and emitting
   draw surfaces, the brush hulls, then the lights, and prints its counts
 */

/* the sky environment light (openspec change q3map2-sky-environment): the map's sky shader by the
   editor's rule, its radiance L(w) from libs/skylight.h, resampled into sh-baker's equirect texture
   (light_sh_model rule 8) and cross-checked through sh-baker's own projection. Both bakes read it:
   the full bake keeps direct and bounced sky light, the indirect bake only the bounced */
static void ShAddSky( ShBakeScene& bake ) {
	std::vector<std::string> names;
	for ( const bspShader_t& shader : bspShaders ) {
		if ( ShaderInfoForShader( shader.shader ).sh.parms.skyParms ) {
			names.push_back( shader.shader );
		}
	}
	const std::vector<std::string> candidates = skylight::SortCandidates( std::move( names ) );
	const std::optional<std::string> chosen = skylight::ChooseSky( candidates );
	if ( !chosen ) {
		Sys_Printf( "%9s no sky shader: no sky light\n", "" );
		return;
	}
	if ( candidates.size() > 1 ) {
		std::string list;
		for ( const std::string& candidate : candidates )
			list += ( list.empty() ? "" : ", " ) + candidate;
		Sys_Warning( "-shbake: the map uses %zu sky shaders (%s); the sky light uses %s\n", candidates.size(), list.c_str(), chosen->c_str() );
	}

	const skylight::SkySource source = ShSkySource( ShShaderDescFor( ShaderInfoForShader( chosen->c_str() ) ) );
	if ( skylight::EffectiveStrength( source ) <= 0 ) {
		/* a legacy sky: nothing is loaded, so its images can't warn */
		Sys_Printf( "%9s sky %s has no positive qer_pbr_emissiveStrength: no sky light\n", "", chosen->c_str() );
		return;
	}

	/* q3map2's image cache keeps every image for the whole run, so SkyRadiance may borrow them */
	const auto view = []( const char* name ) -> std::optional<skylight::ImageRGBA8> {
		const image_t* image = strEmpty( name ) ? nullptr : ImageLoad( name );
		if ( image == nullptr || image->pixels == nullptr || image->width <= 0 || image->height <= 0 ) {
			return std::nullopt;
		}
		skylight::ImageRGBA8 v;
		v.width = image->width;
		v.height = image->height;
		v.pixels = image->pixels;
		return v;
	};
	skylight::SkyRadiance sky;
	const char* kind;
	if ( !source.farbox.empty() ) {
		std::array<skylight::ImageRGBA8, skylight::c_faceCount> faces;
		for ( int face = 0; face < skylight::c_faceCount; ++face ) {
			const auto name = StringStream( source.farbox.c_str(), '_', skylight::FaceSuffix( face ) );
			const std::optional<skylight::ImageRGBA8> image = view( name );
			if ( !image ) {
				Sys_Warning( "-shbake: sky shader %s: farbox %s has no image %s; the map has no sky light\n", chosen->c_str(), source.farbox.c_str(), name.c_str() );
				return;
			}
			faces[face] = *image;
		}
		sky = skylight::FarboxRadiance( source, faces );
		kind = "farbox";
	}
	else {
		const std::optional<skylight::ImageRGBA8> image = view( source.stageImage.c_str() );
		if ( !image ) {
			Sys_Warning( "-shbake: sky shader %s: cloud image \"%s\" not found; the map has no sky light\n", chosen->c_str(), source.stageImage.c_str() );
			return;
		}
		sky = skylight::CloudRadiance( source, *image );
		kind = "cloud layer";
	}

	sh_baker::Environment env;
	env.type = sh_baker::Environment::Type::Texture;
	env.texture.width = c_shSkyWidth;
	env.texture.height = c_shSkyHeight;
	env.texture.channels = 3;
	env.texture.pixel_data = ShSkyEquirect( sky );
	for ( int r = 0; r < 3; ++r )
		for ( int c = 0; c < 3; ++c )
			env.to_environment( r, c ) = c_shSkyToEnvironment[r][c];

	/* the cross-check: sh-baker's projection of the texture, in the texture frame, against the derivation */
	env.sh_coeffs = sh_baker::ProjectEnvironmentToSH( env );
	skylight::SHCoeffs textureFrame;
	for ( int k = 0; k < 9; ++k )
		textureFrame[k] = skylight::Vec3( env.sh_coeffs.coeffs[k].x(), env.sh_coeffs.coeffs[k].y(), env.sh_coeffs.coeffs[k].z() );
	const skylight::SHCoeffs derived = skylight::ProjectRadiance( sky );
	const double error = ShSkyCheckError( derived, textureFrame );
	const skylight::Vec3 up = skylight::Irradiance( derived, skylight::Vec3( 0, 0, 1 ) );
	const skylight::Vec3 upTexture = skylight::Irradiance( textureFrame, skylight::Vec3( 0, 1, 0 ) ); /* world +Z in the texture frame */
	Sys_Printf( "%9s sky %s (%s, strength %g): irradiance at +Z %.4f %.4f %.4f; sampled %d x %d: %.4f %.4f %.4f (%.3f%% off at most)\n", "",
	            chosen->c_str(), kind, skylight::EffectiveStrength( source ), up[0], up[1], up[2], c_shSkyWidth, c_shSkyHeight,
	            upTexture[0], upTexture[1], upTexture[2], error * 100 );
	if ( error > 0.01 ) {
		Sys_Warning( "-shbake: sky %s: the sampled sky's irradiance is %.2f%% off the derivation's; the bake's sky light is inaccurate\n", chosen->c_str(), error * 100 );
	}
	bake.scene.environment = std::move( env );
}

static void ShBuildScene( ShBakeScene& bake ){
	ShSceneCounts counts;
	ShUnsupported unsupported;
	ShMaterials materials( bake.scene, counts );

	Sys_Printf( "--- ShBuildScene ---\n" );
	ShWarnEntityKeys( unsupported );

	/* surfaces first: lights and the BVH point into scene.geometries */
	const std::vector<ShEmitter> emitters = ShAddDrawSurfaces( bake, materials, unsupported, counts );
	ShAddHulls( bake, materials, counts );

	/* every baked point names a material, so the scene holds at least one (design decision 10) */
	if ( bake.scene.materials.empty() ) {
		sh_baker::Material mat;
		mat.name = "-shbake default";
		mat.albedo = ShTexture1x1( { 255, 255, 255, 255 } );
		mat.metallic_roughness_texture = ShTexture1x1( { 0, 255, 0 } );
		bake.scene.materials.push_back( std::move( mat ) );
	}
	bake.defaultMaterial = 0;

	ShAddAreaLights( bake, emitters, counts );
	ShWarnUnattachedEmitters( emitters );
	ShAddEntityLights( bake, unsupported, counts );

	/* what the bake's light tree will hold: it drops any bounded light whose flux is 0 */
	sh_baker::LightTree tree;
	tree.Build( bake.scene.lights );

	Sys_Printf( "%9d draw surfaces (%d emitting, %d patches), %d triangles\n", counts.surfaces, counts.emitterSurfaces, counts.patches, counts.triangles );
	Sys_Printf( "%9d surfaces left out: %d _skybox copies, %d foliage, %d receive only, %d empty\n",
	            counts.skippedSkybox + counts.skippedFoliage + counts.skippedReceiveOnly + counts.skippedEmpty,
	            counts.skippedSkybox, counts.skippedFoliage, counts.skippedReceiveOnly, counts.skippedEmpty );
	Sys_Printf( "%9d hulls, %d triangles (%d bevel sides; %d brushes not solid, %d not casting, %d failed)\n",
	            counts.hulls, counts.hullTriangles, counts.hullBevels, counts.brushesNotSolid, counts.brushesNotCasting, counts.hullsFailed );
	Sys_Printf( "%9d side-file hull vertices, %zu indices\n", int( bake.hulls.positions.size() ), bake.hulls.indices.size() );
	Sys_Printf( "%9zu materials, %d images loaded, %d missing\n", bake.scene.materials.size(), counts.imagesLoaded, counts.imagesMissing );
	Sys_Printf( "%9d area lights (%d with an emission CDF, %d uniform, %d empty)\n",
	            counts.areaLightsTextured + counts.areaLightsUniform, counts.areaLightsTextured, counts.areaLightsUniform, counts.areaLightsSkipped );
	Sys_Printf( "%9d entity lights (%d point, %d spot, %d sun)\n", counts.pointLights + counts.spotLights + counts.sunLights,
	            counts.pointLights, counts.spotLights, counts.sunLights );
	Sys_Printf( "%9d attached lights left out (%d light_area, %d _proxy)\n", counts.areaEntitiesSkipped + counts.proxiesSkipped,
	            counts.areaEntitiesSkipped, counts.proxiesSkipped );
	Sys_Printf( "%9zu lights in the light tree (%zu directional) of %zu in the scene\n", tree.NumLights(), tree.NumDirectionalLights(), bake.scene.lights.size() );
	Sys_Printf( "%9zu geometries in scaled space (x %g)\n", bake.scene.geometries.size(), c_shSceneScale );
	ShAddSky( bake );
}



/* -------------------------------------------------------------------------------

   the points and the bakes (design decisions 1, 3, 5, 6 and 10)

   ------------------------------------------------------------------------------- */

/* vertex points sit this far along the normal, clear of the scaled ray offsets (design decision 10) */
constexpr float c_shVertexOffset = 0.25f;

/* the one point list both bakes read, in three runs (design decision 10) */
struct ShPointList
{
	std::vector<sh_baker::SurfacePoint> points;
	std::vector<size_t> lightmapFirst;  /* per raw lightmap: the index of its super-luxel (0, 0) */
	size_t gridFirst = 0;               /* one point per grid point, in BSP grid order */
	size_t vertexFirst = 0;             /* then one point per entry of vertexNums */
	std::vector<int> vertexNums;        /* the draw vertex of each vertex point */
	int luxels = 0, grid = 0, gridInSolid = 0, vertices = 0;   /* baked points of each run */
};

/* what the side file takes from the stage besides the pages: the hull mesh and the SH grid */
static SHQ3Map2Extension g_shSideFile;

/* a unit tangent perpendicular to unit n, w = +1: it only orients the sampling hemisphere */
static Eigen::Vector4f ShTangent( const Vector3& n ){
	const Vector3 axis = std::fabs( n[ 0 ] ) < 0.9f? g_vector3_axis_x : g_vector3_axis_y;
	const Vector3 t = VectorNormalized( vector3_cross( n, axis ) );
	return Eigen::Vector4f( t[ 0 ], t[ 1 ], t[ 2 ], 1 );
}

/* a unit normal, +Z when it can't be normalised */
static Vector3 ShUnitNormal( const Vector3& n ){
	const float length = vector3_length( n );
	return ( length > 1e-6f && std::isfinite( length ) )? n * ( 1 / length ) : g_vector3_axis_z;
}

static sh_baker::SurfacePoint ShSurfacePoint( const Vector3& origin, const Vector3& normal, int material ){
	sh_baker::SurfacePoint point;
	const Vector3 n = ShUnitNormal( normal );
	point.position = ShEigen( origin ) * c_shSceneScale;
	point.normal = ShEigen( n );
	point.tangent = ShTangent( n );
	point.material_id = material;
	return point;
}

/* the grid points the stage bakes: none under -nogrid, where SetupGrid leaves gridBounds unset,
   even if the BSP still holds an earlier compile's grid */
static size_t ShGridPointCount(){
	return noGridLighting? 0 : bspGridPoints.size();
}

/* a uniform value in [0, 1) from a fixed generator, for the grid nudge */
static double ShUniform( std::mt19937& rng ){
	return ( rng() >> 8 ) * ( 1.0 / 16777216.0 );
}

/* grid point num's origin with TraceGrid's nudge, but from a sequence seeded by num instead of
   Random(), so the result doesn't depend on threads; false when it stays in solid (design decision 6) */
static bool ShGridOrigin( int num, Vector3& origin ){
	const int x = num % gridBounds[ 0 ];
	const int y = ( num / gridBounds[ 0 ] ) % gridBounds[ 1 ];
	const int z = num / ( gridBounds[ 0 ] * gridBounds[ 1 ] );
	origin = gridMins + Vector3( x, y, z ) * gridSize;
	if ( ClusterForPointExt( origin, GRID_EPSILON ) >= CLUSTER_NORMAL ) {
		return true;
	}

	std::mt19937 rng( num );
	const Vector3 baseOrigin( origin );
	double step = 0;
	while ( ( step += 0.005 ) <= 1 )
	{
		origin = baseOrigin;
		origin[ 0 ] += step * ( ShUniform( rng ) - 0.5 ) * gridSize[ 0 ];
		origin[ 1 ] += step * ( ShUniform( rng ) - 0.5 ) * gridSize[ 1 ];
		origin[ 2 ] += step * ( ShUniform( rng ) - 0.5 ) * gridSize[ 2 ];
		if ( ClusterForPointExt( origin, VERTEX_EPSILON ) >= CLUSTER_NORMAL ) {
			return true;
		}
	}
	return false;
}

/*
   ShBuildPoints()
   the super-luxels in raw lightmap order, row-major, baked where mapped and not occluded;
   one full-sphere point per grid point, unless -nogrid; the draw vertices in surface order,
   without q3map_noVertexLight shaders and _skybox copies, unless -novertex (design decision 10)
 */

static ShPointList ShBuildPoints( int material ){
	ShPointList list;

	/* super-luxels */
	size_t count = 0;
	for ( const rawLightmap_t& lm : Span( rawLightmaps, numRawLightmaps ) )
	{
		list.lightmapFirst.push_back( count );
		count += size_t( lm.sw ) * lm.sh;
	}
	list.points.resize( count );
	for ( int i = 0; i < numRawLightmaps; ++i )
	{
		const rawLightmap_t& lm = rawLightmaps[ i ];
		for ( int y = 0; y < lm.sh; ++y )
		{
			for ( int x = 0; x < lm.sw; ++x )
			{
				if ( lm.getSuperCluster( x, y ) >= CLUSTER_NORMAL ) {
					list.points[ list.lightmapFirst[ i ] + size_t( y ) * lm.sw + x ] = ShSurfacePoint( lm.getSuperOrigin( x, y ), lm.getSuperNormal( x, y ), material );
					++list.luxels;
				}
			}
		}
	}

	/* grid points: probes in open space, no normal or tangent */
	list.gridFirst = list.points.size();
	for ( size_t num = 0; num < ShGridPointCount(); ++num )
	{
		sh_baker::SurfacePoint point;
		point.full_sphere = true;
		Vector3 origin;
		if ( ShGridOrigin( num, origin ) ) {
			point.position = ShEigen( origin ) * c_shSceneScale;
			point.material_id = material;
			++list.grid;
		}
		else{
			++list.gridInSolid;
		}
		list.points.push_back( point );
	}

	/* draw vertices, offset along the normal; vertices in solid are not nudged */
	list.vertexFirst = list.points.size();
	if ( !noVertexLighting ) {
		for ( size_t num = 0; num < bspDrawSurfaces.size(); ++num )
		{
			const bspDrawSurface_t& ds = bspDrawSurfaces[ num ];
			const surfaceInfo_t& info = surfaceInfos[ num ];
			if ( info.si == nullptr || info.si->noVertexLight || info.parentSurfaceNum >= 0 ) {
				continue;
			}
			for ( int v = ds.firstVert; v < ds.firstVert + ds.numVerts; ++v )
			{
				const bspDrawVert_t& dv = yDrawVerts[ v ];
				const Vector3 n = ShUnitNormal( dv.normal );
				list.points.push_back( ShSurfacePoint( dv.xyz + n * c_shVertexOffset, n, material ) );
				list.vertexNums.push_back( v );
				++list.vertices;
			}
		}
	}

	return list;
}

/* one sh-baker call over the point list, with its progress on the console */
static sh_baker::BakeResult ShRunBake( const ShBakeScene& bake, const ShPointList& list, bool indirectOnly ){
	const char *name = indirectOnly? "indirect" : "full";
	Sys_Printf( "--- ShBake %s (exactly %d samples per point, %d bounces%s) ---\n", name, shSamples, shBounces,
	            indirectOnly? ", no direct light at the baked point" : "" );

	sh_baker::RasterConfig raster;
	raster.width = list.points.size();
	raster.height = 1;
	raster.supersample_scale = 1;

	sh_baker::BakeConfig config;
	config.samples = shSamples;
	config.bounces = shBounces;
	config.indirect_only = indirectOnly;
	/* no adaptive stop (sh-baker #68): every point takes exactly shSamples rays */
	config.confidence_threshold = 0;
	/* calls never overlap, so the next percent to print needs no lock */
	int nextPercent = 0;
	config.progress_fn = [&nextPercent]( size_t done, size_t total ){
		const int percent = int( done * 100 / total );
		for ( ; nextPercent <= percent; nextPercent += 10 )
			Sys_Printf( nextPercent == 100? "100%%\n" : "%d%%...", nextPercent );
	};

	const Timer timer;
	sh_baker::BakeResult result = sh_baker::BakeSHLightMap( bake.scene, list.points, raster, config );
	if ( nextPercent <= 100 ) {
		Sys_Printf( "\n" );
	}
	Sys_Printf( "%9.1f seconds for the %s bake of %zu points\n", timer.elapsed_sec(), name, list.points.size() );
	return result;
}

/* every super-luxel and vertex the full bake fills gets value with count 1, or the result's
   irradiance when there is one (design decisions 3 and 5) */
static int ShStoreColours( const ShPointList& list, const sh_baker::BakeResult *full ){
	int nonFinite = 0;
	const auto colour = [&]( size_t index ){
		if ( full == nullptr ) {
			return Vector3( 0 );
		}
		const Eigen::Vector3f& e = full->sh_texture.pixels[ index ].irradiance;
		Vector3 c( e.x(), e.y(), e.z() );
		if ( !( std::isfinite( c[ 0 ] ) && std::isfinite( c[ 1 ] ) && std::isfinite( c[ 2 ] ) ) ) {
			++nonFinite;
			c.set( 0 );
		}
		return c;
	};

	for ( int i = 0; i < numRawLightmaps; ++i )
	{
		rawLightmap_t& lm = rawLightmaps[ i ];
		for ( int y = 0; y < lm.sh; ++y )
		{
			for ( int x = 0; x < lm.sw; ++x )
			{
				const size_t index = list.lightmapFirst[ i ] + size_t( y ) * lm.sw + x;
				if ( list.points[ index ].material_id >= 0 ) {
					SuperLuxel& luxel = lm.getSuperLuxel( 0, x, y );
					luxel.value = colour( index );
					luxel.count = 1;
				}
			}
		}
	}

	for ( size_t k = 0; k < list.vertexNums.size(); ++k )
		getVertexLuxel( 0, list.vertexNums[ k ] ) = colour( list.vertexFirst + k );

	return nonFinite;
}

/* the full bake's grid SH fit into the grid bytes, or the neutral bytes without it; grid
   points in solid stay black (design decision 6) */
static void ShStoreGridBytes( const ShPointList& list, const sh_baker::BakeResult *full ){
	for ( size_t num = 0; num < ShGridPointCount(); ++num )
	{
		const size_t index = list.gridFirst + num;
		if ( list.points[ index ].material_id < 0 ) {
			continue;
		}
		ShGridFit fit;
		if ( full != nullptr ) {
			ShCoeffs9 sh;
			for ( size_t k = 0; k < sh.size(); ++k )
			{
				const Eigen::Vector3f& c = full->sh_texture.pixels[ index ].coeffs[ k ];
				sh[ k ] = Vector3( c.x(), c.y(), c.z() );
			}
			fit = ShFitGridPoint( sh );
		}
		else{
			fit = ShGridNeutralFit();
		}
		bspGridPoint_t& bgp = bspGridPoints[ num ];
		bgp.ambient[ 0 ] = fit.ambientBytes;
		bgp.directed[ 0 ] = fit.directedBytes;
		NormalToLatLong( fit.direction, bgp.latLong );
	}
}

/* the indirect bake's SH and visibility into the super SH luxels, and its grid SH into the
   side file's SH grid (design decisions 4 and 12). A point with a non-finite coefficient or
   visibility is stored as 0, its grid probe invalid, as ShStoreColours does for the full bake;
   the side-file writer would refuse it. Returns the number of such points */
static int ShStoreIndirect( const ShPointList& list, const sh_baker::BakeResult& indirect ){
	int nonFinite = 0;
	const auto shLuxel = [&]( size_t index, bool& finite ){
		ShLuxel luxel;
		finite = true;
		for ( size_t k = 0; k < luxel.sh.size(); ++k )
		{
			const Eigen::Vector3f& c = indirect.sh_texture.pixels[ index ].coeffs[ k ];
			luxel.sh[ k ] = Vector3( c.x(), c.y(), c.z() );
			finite = finite && std::isfinite( c.x() ) && std::isfinite( c.y() ) && std::isfinite( c.z() );
		}
		luxel.visibility = indirect.environment_visibility_texture.pixel_data[ index ];
		finite = finite && std::isfinite( luxel.visibility );
		if ( !finite ) {
			++nonFinite;
			luxel.set( 0 );
		}
		return luxel;
	};

	for ( int i = 0; i < numRawLightmaps; ++i )
	{
		rawLightmap_t& lm = rawLightmaps[ i ];
		for ( int y = 0; y < lm.sh; ++y )
		{
			for ( int x = 0; x < lm.sw; ++x )
			{
				const size_t index = list.lightmapFirst[ i ] + size_t( y ) * lm.sw + x;
				if ( list.points[ index ].material_id >= 0 ) {
					bool finite;
					lm.getSuperShLuxel( x, y ) = shLuxel( index, finite );
				}
			}
		}
	}

	/* the SH grid: the BSP grid's origin, spacing and dimensions, probe i is BSP grid point i;
	   probes in solid are invalid with zero coefficients. Under -nogrid it has no probes, and
	   origin and spacing stay 0, since SetupGrid leaves gridMins unset (design decision 12) */
	SHGrid& grid = g_shSideFile.grid;
	grid = SHGrid();
	const size_t probes = ShGridPointCount();
	if ( probes != 0 ) {
		grid.origin = gridMins;
		grid.spacing = gridSize;
		for ( int i = 0; i < 3; ++i )
			grid.dimensions[ i ] = gridBounds[ i ];
	}
	grid.coeffs.assign( probes * 27, 0.f );
	grid.valid.assign( probes, 0 );
	for ( size_t num = 0; num < probes; ++num )
	{
		const size_t index = list.gridFirst + num;
		if ( list.points[ index ].material_id < 0 ) {
			continue;
		}
		bool finite;
		const ShLuxel probe = shLuxel( index, finite );
		if ( !finite ) {
			continue;
		}
		for ( size_t k = 0; k < 9; ++k )
			for ( int c = 0; c < 3; ++c )
				grid.coeffs[ ( num * 9 + k ) * 3 + c ] = probe.sh[ k ][ c ];
		grid.valid[ num ] = 1;
	}
	return nonFinite;
}

/*
   ShFloodRawLightmap()
   IlluminateRawLightmap's neighbour flood (light_ydnar.cpp, filter pass) without splotchFix,
   -dark, ambient or deluxe handling: each unmapped or occluded super-luxel takes the average
   of its baked 3x3 neighbours, in colour, SH and visibility together, and becomes
   CLUSTER_FLOODED. It chooses by cluster and count only (design decision 3)
 */

static void ShFloodRawLightmap( int rawLightmapNum ){
	rawLightmap_t& lm = rawLightmaps[ rawLightmapNum ];
	const bool floodSH = lm.superShLuxels != nullptr;

	for ( int y = 0; y < lm.sh; ++y )
	{
		for ( int x = 0; x < lm.sw; ++x )
		{
			int& cluster = lm.getSuperCluster( x, y );
			if ( cluster >= CLUSTER_NORMAL ) {
				continue;
			}

			Vector3 averageColor( 0 );
			ShLuxel averageSH;
			averageSH.set( 0 );
			float samples = 0;
			for ( int sy = y - 1; sy <= y + 1; ++sy )
			{
				if ( sy < 0 || sy >= lm.sh ) {
					continue;
				}
				for ( int sx = x - 1; sx <= x + 1; ++sx )
				{
					if ( sx < 0 || sx >= lm.sw || ( sx == x && sy == y ) ) {
						continue;
					}
					const SuperLuxel& luxel2 = lm.getSuperLuxel( 0, sx, sy );
					if ( lm.getSuperCluster( sx, sy ) < CLUSTER_NORMAL || luxel2.count == 0 ) {
						continue;
					}
					averageColor += luxel2.value;
					samples += luxel2.count;
					if ( floodSH ) {
						averageSH += lm.getSuperShLuxel( sx, sy );
					}
				}
			}
			if ( samples <= 0 ) {
				continue;
			}

			SuperLuxel& luxel = lm.getSuperLuxel( 0, x, y );
			luxel.value = averageColor * ( 1.f / samples );
			luxel.count = 1;
			if ( floodSH ) {
				lm.getSuperShLuxel( x, y ) = ( averageSH *= ( 1.f / samples ) );
			}
			cluster = CLUSTER_FLOODED;
		}
	}
}



/* maps/<name>/<name>.shsf and maps/<name>/<name>_l0_<page>.hdr (design decision 12) */
static CopiedString ShSideFileDir(){
	return CopiedString( PathExtensionless( source ) );
}
static auto ShL0DumpPath( int page ){
	char number[ 8 ];
	snprintf( number, sizeof( number ), "%04d", page );
	return StringStream( ShSideFileDir(), '/', PathFilename( source ), "_l0_", number, ".hdr" );
}

/* an earlier run's side file and L0 dumps, removed before the bakes, so every run leaves only
   its own outputs: none under -shnoindirect, and no extra dumps past this run's pages */
static void ShRemoveStaleSideFile(){
	const auto path = StringStream( ShSideFileDir(), '/', PathFilename( source ), ".shsf" );
	int removed = remove( path.c_str() ) == 0? 1 : 0;
	for ( int page = 0; remove( ShL0DumpPath( page ).c_str() ) == 0; ++page )
		++removed;
	if ( removed != 0 ) {
		Sys_Printf( "Removed %d side-file outputs of an earlier run\n", removed );
	}
}

/*
   ShBakeWorld()
   builds the scene and the point list once, runs the indirect bake, then the full bake, as
   the options leave them on, each copied into q3map2's buffers and freed before the next
   starts, and floods the unbaked super-luxels (design decisions 1 and 3)
 */

void ShBakeWorld(){
	ShRemoveStaleSideFile();

	ShBakeScene bake;
	ShBuildScene( bake );

	Sys_Printf( "--- ShBuildPoints ---\n" );
	const ShPointList list = ShBuildPoints( bake.defaultMaterial );
	Sys_Printf( "%9d luxel points (of %zu super-luxels)\n", list.luxels, list.gridFirst );
	Sys_Printf( "%9d grid points (%d in solid, not baked)%s\n", list.grid, list.gridInSolid, noGridLighting? " (-nogrid)" : "" );
	Sys_Printf( "%9d vertex points%s\n", list.vertices, noVertexLighting? " (-novertex)" : "" );
	Sys_Printf( "%9zu points in each bake\n", list.points.size() );

	g_shSideFile.hull_mesh = std::move( bake.hulls );
	g_shSideFile.grid = SHGrid();

	if ( shNoFull && shNoIndirect ) {
		Sys_Printf( "No bake: -shnofull and -shnoindirect\n" );
	}

	/* the indirect bake first: its results are freed before the full bake starts */
	if ( !shNoIndirect ) {
		const sh_baker::BakeResult indirect = ShRunBake( bake, list, true );
		if ( const int nonFinite = ShStoreIndirect( list, indirect ) ) {
			Sys_Warning( "-shbake: %d points had a non-finite indirect SH or visibility; they are stored as 0\n", nonFinite );
		}
	}

	if ( !shNoFull ) {
		const sh_baker::BakeResult full = ShRunBake( bake, list, false );
		if ( const int nonFinite = ShStoreColours( list, &full ) ) {
			Sys_Warning( "-shbake: %d points had a non-finite irradiance; they are stored as 0\n", nonFinite );
		}
		ShStoreGridBytes( list, &full );
	}
	else{
		/* the placeholder 0 0 0 with count 1 on exactly the luxels the full bake fills;
		   ColorToBytes makes it the neutral 64, and the grid gets its neutral bytes */
		ShStoreColours( list, nullptr );
		ShStoreGridBytes( list, nullptr );
	}

	Sys_Printf( "--- ShFloodRawLightmap ---\n" );
	RunThreadsOnIndividual( numRawLightmaps, true, ShFloodRawLightmap );
}



/*
   ShWriteSideFile()
   after StoreSurfaceLightmaps, when the indirect bake ran: the page SH in BSP page order,
   luxels still holding the -1 marker set to 0, the un-shrunk hull mesh and the SH grid,
   written to maps/<name>/<name>.shsf, and with -shdumpl0 each page's L0 as
   maps/<name>/<name>_l0_<page>.hdr (design decision 12). A writer failure goes to Error()
 */

void ShWriteSideFile(){
	Sys_Printf( "--- ShWriteSideFile ---\n" );
	const Timer timer;

	/* page i is BSP lightmap page i: every output lightmap is a game-size BSP page, since
	   -shbake refuses every other page size and external lightmaps (design decision 4) */
	std::vector<const outLightmap_t*> byPage( numBSPLightmaps, nullptr );
	for ( const outLightmap_t& olm : Span( outLightmaps, numOutLightmaps ) )
	{
		if ( olm.lightmapNum < 0 || olm.lightmapNum >= numBSPLightmaps || byPage[ olm.lightmapNum ] != nullptr || olm.bspShLuxels == nullptr ) {
			Error( "-shbake: output lightmap %d (BSP page %d of %d) has no side-file page of its own", int( &olm - outLightmaps ), olm.lightmapNum, numBSPLightmaps );
		}
		byPage[ olm.lightmapNum ] = &olm;
	}

	SHQ3Map2Extension& ext = g_shSideFile;
	ext.pages.clear();
	ext.pages.reserve( numBSPLightmaps );
	size_t filled = 0, luxels = 0;
	for ( int i = 0; i < numBSPLightmaps; ++i )
	{
		if ( byPage[ i ] == nullptr ) {
			Error( "-shbake: BSP lightmap page %d has no output lightmap", i );
		}
		const outLightmap_t& olm = *byPage[ i ];
		SHQ3Map2ExtensionPage& page = ext.pages.emplace_back();
		page.width = olm.customWidth;
		page.height = olm.customHeight;
		const size_t n = size_t( page.width ) * page.height;
		page.coeffs.resize( n * 27 );
		page.visibility.resize( n );
		for ( size_t j = 0; j < n; ++j )
		{
			ShLuxel luxel = olm.bspShLuxels[ j ];
			/* the bake's "no value" marker: every value -1, and no real visibility is below 0 */
			if ( luxel.visibility < 0 ) {
				luxel.set( 0 );
				++filled;
			}
			for ( size_t k = 0; k < 9; ++k )
				for ( int c = 0; c < 3; ++c )
					page.coeffs[ ( j * 9 + k ) * 3 + c ] = luxel.sh[ k ][ c ];
			page.visibility[ j ] = luxel.visibility;
		}
		luxels += n;
	}

	const CopiedString dir( ShSideFileDir() );
	const CopiedString name( PathFilename( source ) );
	Q_mkdir( dir.c_str() );
	const auto path = StringStream( dir, '/', name, ".shsf" );

	std::string error;
	if ( !SaveSHQ3Map2Extension( ext, path.c_str(), &error ) ) {
		Error( "-shbake: can't write the side file: %s", error.c_str() );
	}
	Sys_Printf( "%9zu pages, %zu luxels (%zu still unbaked, written as 0)\n", ext.pages.size(), luxels, filled );
	Sys_Printf( "%9zu hulls, %zu vertices, %zu indices\n", ext.hull_mesh.hulls.size(), ext.hull_mesh.positions.size(), ext.hull_mesh.indices.size() );
	Sys_Printf( "%9zu SH grid probes (%u x %u x %u), %zu valid\n", ext.grid.valid.size(),
	            ext.grid.dimensions[ 0 ], ext.grid.dimensions[ 1 ], ext.grid.dimensions[ 2 ],
	            size_t( std::count( ext.grid.valid.begin(), ext.grid.valid.end(), 1 ) ) );
	Sys_Printf( "Wrote %s in %.2f seconds\n", path.c_str(), timer.elapsed_sec() );

	if ( shDumpL0 ) {
		for ( size_t i = 0; i < ext.pages.size(); ++i )
		{
			const auto hdr = ShL0DumpPath( int( i ) );
			if ( !SaveSHQ3Map2ExtensionPageL0Hdr( ext.pages[ i ], hdr.c_str(), &error ) ) {
				Error( "-shbake: can't write the L0 dump: %s", error.c_str() );
			}
		}
		Sys_Printf( "%9zu L0 dumps, %s_l0_0000.hdr onwards\n", ext.pages.size(), name.c_str() );
	}

	g_shSideFile = SHQ3Map2Extension();
}

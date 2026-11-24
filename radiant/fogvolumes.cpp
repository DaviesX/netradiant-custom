/*
   The map's fog volumes for the lighting preview (openspec change fog-preview,
   design decision 10).

   Every brush in every entity, hidden ones included, is a candidate. Its
   content shader is chosen as q3map2 chooses it; a brush whose content shader
   declares fogparms is a fog volume. The volumes are ordered as q3map2 leaves
   its brush list, the 32-plane and 8-volume limits applied, and the warnings
   printed only when what they report changes. The walk runs at the next
   lighting-mode draw after a scene change, a shader reload or a preference
   change, never per frame.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.
 */

#include "fogvolumes.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ientity.h"
#include "iscenegraph.h"
#include "ishaders.h"
#include "moduleobserver.h"
#include "scenelib.h"
#include "signal/isignal.h"
#include "stream/stringstream.h"

#include "brush.h"
#include "brushnode.h"

namespace
{

struct FogVolumesState
{
	bool dirty = true;
	std::vector<fogvolume::Volume> volumes;
	std::set<std::string> overPlanes; ///< brushes reported for exceeding the plane limit, re-armed when they drop back
	int reportedFound = -1;           ///< the volume count last warned about, -1 when within the limit
};

FogVolumesState g_fogVolumes;

/// one brush as the walk sees it
struct WalkBrush
{
	std::vector<fogvolume::Side> sides;
	std::string name;
};

/// one entity as the walk sees it: its kind and brushes in map order
struct WalkEntity
{
	fogvolume::EntityKind kind = fogvolume::EntityKind::Other;
	std::vector<WalkBrush> brushes;
};

/// the fog-relevant data of a shader, cached per name during a walk
class FogShaderCache
{
	std::map<std::string, fogvolume::FogShader> m_shaders;
public:
	const fogvolume::FogShader& get( const char* name ) {
		auto found = m_shaders.find( name );
		if ( found != m_shaders.end() ) {
			return found->second;
		}
		fogvolume::FogShader data;
		IShader* shader = GlobalShaderSystem().getShaderForName( name );
		data.hasFog = shader->hasFogParms();
		const Vector3& colour = shader->getFogColour();
		data.colour = fogvolume::V3( colour[0], colour[1], colour[2] );
		data.distance = shader->getFogDistance();
		const Vector3& direction = shader->getFogDirection();
		data.direction = fogvolume::V3( direction[0], direction[1], direction[2] );
		const unsigned int classes = shader->getQ3ContentClasses();
		data.liquid = ( classes & IShader::Q3CONTENT_LIQUID ) != 0;
		data.fog = ( classes & IShader::Q3CONTENT_FOG ) != 0;
		data.translucent = ( classes & IShader::Q3CONTENT_TRANSLUCENT ) != 0;
		data.nodraw = ( shader->getFlags() & QER_NODRAW ) != 0;
		shader->DecRef();
		return m_shaders.emplace( name, data ).first->second;
	}
};

/// collects every entity's brushes with their sides, in scene (map) order
class FogVolumeWalker : public scene::Graph::Walker
{
	std::vector<WalkEntity>& m_entities;
	FogShaderCache& m_shaders;
public:
	FogVolumeWalker( std::vector<WalkEntity>& entities, FogShaderCache& shaders ) : m_entities( entities ), m_shaders( shaders ) {
	}
	bool pre( const scene::Path& path, scene::Instance& instance ) const override {
		if ( path.size() == 2 ) {
			WalkEntity entity;
			if ( Entity* e = Node_getEntity( path.top() ) ) {
				const char* classname = e->getClassName();
				if ( string_equal_nocase( classname, "worldspawn" ) ) {
					entity.kind = fogvolume::EntityKind::Worldspawn;
				}
				else if ( string_equal_nocase( classname, "func_group" ) ) {
					entity.kind = fogvolume::EntityKind::FuncGroup;
				}
			}
			m_entities.push_back( std::move( entity ) );
			return true;
		}
		if ( path.size() == 3 && !m_entities.empty() ) {
			if ( Brush* brush = Node_getBrush( path.top() ) ) {
				WalkEntity& entity = m_entities.back();
				WalkBrush walked;
				walked.name = StringStream<64>( "entity ", m_entities.size() - 1, " brush ", entity.brushes.size() ).c_str();
				const Matrix4& localToWorld = instance.localToWorld();
				brush->evaluateBRep(); // windings are built lazily; contributes() needs them
				Brush_forEachFace( *brush, [&]( Face& face ) {
					if ( !face.contributes() ) {
						return; // a redundant plane, no side in the compiled brush
					}
					const Plane3 plane = plane3_transformed( face.plane3(), localToWorld );
					fogvolume::Side side;
					side.plane.normal = fogvolume::V3( plane.a, plane.b, plane.c );
					side.plane.dist = plane.d;
					side.shader = m_shaders.get( face.GetShader() );
					walked.sides.push_back( side );
				} );
				entity.brushes.push_back( std::move( walked ) );
			}
			return false;
		}
		return path.size() < 3;
	}
};

void FogVolumes_sceneChanged() {
	g_fogVolumes.dirty = true;
}

class FogVolumesShadersObserver : public ModuleObserver
{
public:
	void realise() override {
		g_fogVolumes.dirty = true;
	}
	void unrealise() override {
		g_fogVolumes.dirty = true;
	}
};

FogVolumesShadersObserver g_fogVolumesShadersObserver;

} // namespace

void FogVolumes_construct() {
	GlobalSceneGraph().addSceneChangedCallback( FreeCaller<void(), FogVolumes_sceneChanged>() );
	GlobalShaderSystem().attach( g_fogVolumesShadersObserver );
}

void FogVolumes_destroy() {
	GlobalShaderSystem().detach( g_fogVolumesShadersObserver );
}

void FogVolumes_setDirty() {
	g_fogVolumes.dirty = true;
}

void FogVolumes_update() {
	if ( !g_fogVolumes.dirty ) {
		return;
	}
	g_fogVolumes.dirty = false;

	std::vector<WalkEntity> entities;
	{
		FogShaderCache shaders;
		GlobalSceneGraph().traverse( FogVolumeWalker( entities, shaders ) );
	}

	std::vector<fogvolume::OrderEntity> order;
	for ( const WalkEntity& entity : entities ) {
		fogvolume::OrderEntity e;
		e.kind = entity.kind;
		for ( const WalkBrush& brush : entity.brushes ) {
			e.translucent.push_back( fogvolume::Translucent( brush.sides ) );
		}
		order.push_back( std::move( e ) );
	}

	// candidates: fog brushes in q3map2's order
	std::vector<const WalkBrush*> brushes;
	std::vector<int> contents;
	std::vector<fogvolume::Candidate> candidates;
	for ( const fogvolume::BrushKey& key : fogvolume::FogOrder( order ) ) {
		const WalkBrush& brush = entities[key.entity].brushes[key.brush];
		const int content = fogvolume::ContentSide( brush.sides );
		if ( content >= 0 && brush.sides[content].shader.hasFog ) {
			brushes.push_back( &brush );
			contents.push_back( content );
			candidates.push_back( fogvolume::Candidate{ int( brush.sides.size() ), brush.name } );
		}
	}

	std::vector<int> overPlanes;
	int found = 0;
	const std::vector<int> honoured = fogvolume::SelectVolumes( candidates, overPlanes, found );

	std::set<std::string> over;
	for ( int i : overPlanes ) {
		over.insert( candidates[i].name );
		if ( g_fogVolumes.overPlanes.count( candidates[i].name ) == 0 ) {
			globalWarningStream() << "WARNING: fog brush " << candidates[i].name.c_str() << " has " << candidates[i].planes
								  << " planes, more than " << fogvolume::c_maxPlanes << "; the preview ignores it\n";
		}
	}
	g_fogVolumes.overPlanes = std::move( over );

	if ( found > fogvolume::c_maxVolumes ) {
		if ( found != g_fogVolumes.reportedFound ) {
			globalWarningStream() << "WARNING: the map has " << found << " fog volumes; the preview honours the first "
								  << fogvolume::c_maxVolumes << '\n';
		}
		g_fogVolumes.reportedFound = found;
	}
	else {
		g_fogVolumes.reportedFound = -1;
	}

	g_fogVolumes.volumes.clear();
	for ( int i : honoured ) {
		g_fogVolumes.volumes.push_back( fogvolume::MakeVolume( brushes[i]->sides, contents[i], brushes[i]->name ) );
	}
}

const std::vector<fogvolume::Volume>& FogVolumes_volumes() {
	return g_fogVolumes.volumes;
}

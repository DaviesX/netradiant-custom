/*
   The map's sky environment light for the lighting preview (openspec change
   sky-environment-light, design decision 4).

   The candidates are the distinct shaders that declare skyparms (QER_SKY) on
   the map's brush faces and patches, hidden ones included; the alphabetically
   first is used. The walk runs at the next lighting-mode draw after a scene
   change, a shader reload or a "Lighting sky" change, never per frame. The
   chosen shader is projected only when it changes or shaders were reloaded.

   This file is part of NetRadiant-custom, under the GNU General Public License
   version 2 or later.
 */

#include "skyenvironment.h"

#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "iimage.h"
#include "iscenegraph.h"
#include "ishaders.h"
#include "itextures.h"
#include "moduleobserver.h"
#include "scenelib.h"
#include "signal/isignal.h"
#include "stream/stringstream.h"

#include "skylight.h"

#include "brush.h"
#include "brushnode.h"
#include "camwindow.h"
#include "patch.h"

namespace
{

struct SkyEnvironmentState
{
	bool dirty = true;
	bool shadersChanged = true;          ///< reproject even if the chosen shader is the same
	std::vector<std::string> candidates; ///< the last walk's candidates, sorted
	bool candidatesKnown = false;
	std::string projectedShader; ///< the shader the coefficients were projected from, "" for none
	bool projected = false;
	std::array<float, 27> coefficients{}; ///< A_l L_lm / pi, RGB per coefficient
};

SkyEnvironmentState g_skyEnvironment;
const std::array<float, 27> g_skyZero{};

/// collects the shader names of every brush face and patch in the scene, hidden ones included
class SkyShaderNameWalker : public scene::Graph::Walker
{
	std::set<std::string>& m_names;
public:
	SkyShaderNameWalker( std::set<std::string>& names ) : m_names( names ) {
	}
	bool pre( const scene::Path& path, scene::Instance& instance ) const override {
		scene::Node& node = path.top();
		if ( Brush* brush = Node_getBrush( node ) ) {
			Brush_forEachFace( *brush, [this]( Face& face ) {
				m_names.insert( face.GetShader() );
			} );
		}
		else if ( Patch* patch = Node_getPatch( node ) ) {
			m_names.insert( patch->GetShader() );
		}
		return true;
	}
};

/// an image loaded with the textures cache's loader, released on destruction
class LoadedImage
{
	Image* m_image = nullptr;
public:
	explicit LoadedImage( const char* name ) : m_image( GlobalTexturesCache().loadImage( name ) ) {
		if ( m_image != nullptr && ( m_image->getWidth() == 0 || m_image->getHeight() == 0 || m_image->getRGBAPixels() == nullptr ) ) {
			m_image->release();
			m_image = nullptr;
		}
	}
	LoadedImage( const LoadedImage& ) = delete;
	LoadedImage& operator=( const LoadedImage& ) = delete;
	~LoadedImage() {
		if ( m_image != nullptr ) {
			m_image->release();
		}
	}
	bool valid() const {
		return m_image != nullptr;
	}
	skylight::ImageRGBA8 view() const {
		skylight::ImageRGBA8 v;
		v.width = int( m_image->getWidth() );
		v.height = int( m_image->getHeight() );
		v.pixels = m_image->getRGBAPixels();
		return v;
	}
};

void SkyEnvironment_storeCoefficients( const skylight::SHCoeffs& radiance ) {
	const skylight::SHCoeffs convolved = skylight::ConvolveCosine( radiance );
	for ( int i = 0; i < 9; ++i ) {
		for ( int c = 0; c < 3; ++c ) {
			g_skyEnvironment.coefficients[i * 3 + c] = float( convolved[i][c] / skylight::c_pi );
		}
	}
}

/// \brief Projects the sky shader \p name, or clears the coefficients when \p name is empty. Warns about missing images.
void SkyEnvironment_project( const std::string& name ) {
	g_skyEnvironment.coefficients = g_skyZero;
	if ( name.empty() ) {
		return;
	}
	IShader* shader = GlobalShaderSystem().getShaderForName( name.c_str() );
	skylight::SkySource source;
	source.name = name;
	source.farbox = shader->getSkyBoxName();
	source.stageImage = shader->getSkyStageName();
	const Vector3& colour = shader->getSkyStageColour();
	source.stageColour = skylight::Vec3( colour[0], colour[1], colour[2] );
	source.hasStrength = shader->hasEmissiveStrength();
	source.strength = shader->getEmissiveStrength();
	shader->DecRef();

	if ( skylight::EffectiveStrength( source ) <= 0 ) {
		return; // a legacy sky emits nothing; nothing to load
	}

	skylight::SHCoeffs radiance;
	const char* kind;
	if ( !source.farbox.empty() ) {
		std::vector<std::unique_ptr<LoadedImage>> images;
		std::array<skylight::ImageRGBA8, skylight::c_faceCount> faces;
		for ( int face = 0; face < skylight::c_faceCount; ++face ) {
			const auto image = StringStream<64>( source.farbox.c_str(), '_', skylight::FaceSuffix( face ) );
			images.push_back( std::make_unique<LoadedImage>( image.c_str() ) );
			if ( !images.back()->valid() ) {
				globalWarningStream() << "WARNING: sky shader " << name.c_str() << ": farbox " << source.farbox.c_str() << " has no image "
									  << image.c_str() << "; the map has no sky light\n";
				return;
			}
			faces[face] = images.back()->view();
		}
		radiance = skylight::ProjectRadiance( skylight::FarboxRadiance( source, faces ) );
		kind = "farbox";
	}
	else {
		const LoadedImage image( source.stageImage.c_str() );
		if ( source.stageImage.empty() || !image.valid() ) {
			globalWarningStream() << "WARNING: sky shader " << name.c_str() << ": cloud image " << Quoted( source.stageImage.c_str() )
								  << " not found; the map has no sky light\n";
			return;
		}
		radiance = skylight::ProjectRadiance( skylight::CloudRadiance( source, image.view() ) );
		kind = "cloud layer";
	}
	SkyEnvironment_storeCoefficients( radiance );
	const skylight::Vec3 up = skylight::Irradiance( radiance, skylight::Vec3( 0, 0, 1 ) );
	globalOutputStream() << "Sky light: " << name.c_str() << " (" << kind << ", strength " << source.strength << "), irradiance at +Z "
						 << up[0] << ' ' << up[1] << ' ' << up[2] << '\n';
}

void SkyEnvironment_sceneChanged() {
	g_skyEnvironment.dirty = true;
}

class SkyEnvironmentShadersObserver : public ModuleObserver
{
public:
	void realise() override {
		g_skyEnvironment.dirty = true;
		g_skyEnvironment.shadersChanged = true;
	}
	void unrealise() override {
		g_skyEnvironment.dirty = true;
		g_skyEnvironment.shadersChanged = true;
	}
};

SkyEnvironmentShadersObserver g_skyEnvironmentShadersObserver;

} // namespace

void SkyEnvironment_construct() {
	GlobalSceneGraph().addSceneChangedCallback( FreeCaller<void(), SkyEnvironment_sceneChanged>() );
	GlobalShaderSystem().attach( g_skyEnvironmentShadersObserver );
}

void SkyEnvironment_destroy() {
	GlobalShaderSystem().detach( g_skyEnvironmentShadersObserver );
}

void SkyEnvironment_setDirty() {
	g_skyEnvironment.dirty = true;
}

void SkyEnvironment_update() {
	if ( !g_skyEnvironment.dirty ) {
		return;
	}
	g_skyEnvironment.dirty = false;

	std::set<std::string> names;
	GlobalSceneGraph().traverse( SkyShaderNameWalker( names ) );
	std::vector<std::string> candidates;
	for ( const std::string& name : names ) {
		IShader* shader = GlobalShaderSystem().getShaderForName( name.c_str() );
		if ( ( shader->getFlags() & QER_SKY ) != 0 ) {
			candidates.push_back( name );
		}
		shader->DecRef();
	}
	candidates = skylight::SortCandidates( std::move( candidates ) );

	const std::optional<std::string> chosen = skylight::ChooseSky( candidates );
	if ( candidates.size() > 1 && ( !g_skyEnvironment.candidatesKnown || candidates != g_skyEnvironment.candidates ) ) {
		std::string list;
		for ( const std::string& candidate : candidates ) {
			list += ( list.empty() ? "" : ", " ) + candidate;
		}
		globalWarningStream() << "WARNING: the map uses " << candidates.size() << " sky shaders (" << list.c_str() << "); the sky light uses "
							  << chosen->c_str() << '\n';
	}
	g_skyEnvironment.candidates = candidates;
	g_skyEnvironment.candidatesKnown = true;

	const std::string name = chosen ? *chosen : std::string();
	if ( !g_skyEnvironment.projected || g_skyEnvironment.shadersChanged || name != g_skyEnvironment.projectedShader ) {
		SkyEnvironment_project( name );
		g_skyEnvironment.projectedShader = name;
		g_skyEnvironment.projected = true;
		g_skyEnvironment.shadersChanged = false;
	}
}

const float* SkyEnvironment_baseCoefficients() {
	return Camera_lightingSky() ? g_skyEnvironment.coefficients.data() : g_skyZero.data();
}

const float* SkyEnvironment_zeroCoefficients() {
	return g_skyZero.data();
}

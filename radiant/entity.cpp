/*
   Copyright (C) 1999-2006 Id Software, Inc. and contributors.
   For a list of contributors, see the accompanying CONTRIBUTORS file.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "entity.h"

#include "ientity.h"
#include "iselection.h"
#include "imodel.h"
#include "ifilesystem.h"
#include "iundo.h"

#include "eclasslib.h"
#include "scenelib.h"
#include "os/path.h"
#include "os/file.h"
#include "stream/stringstream.h"
#include "stringio.h"

#include "gtkutil/filechooser.h"
#include "gtkutil/widget.h"
#include "gtkmisc.h"
#include "select.h"
#include "map.h"
#include "preferences.h"
#include "gtkdlgs.h"
#include "mainframe.h"
#include "qe3.h"
#include "commands.h"

#include "brushmanip.h"
#include "patchmanip.h"
#include "filterbar.h"
#include "brush.h"
#include "patch.h"
#include "tools.h"

#include "ishaders.h"
#include "itextures.h"
#include "iimage.h"
#include "emissivelight.h"

#include <map>
#include <set>


struct entity_globals_t
{
	Vector3 color_entity = Vector3( 1 );
};

entity_globals_t g_entity_globals;

class EntitySetKeyValueSelected : public scene::Graph::Walker
{
	const char* m_key;
	const char* m_value;
public:
	EntitySetKeyValueSelected( const char* key, const char* value )
		: m_key( key ), m_value( value ){
	}
	bool pre( const scene::Path& path, scene::Instance& instance ) const override {
		return true;
	}
	void post( const scene::Path& path, scene::Instance& instance ) const override {
		Entity* entity = Node_getEntity( path.top() );
		if ( entity != 0
		     && ( instance.childSelected() || Instance_isSelected( instance ) ) ) {
			entity->setKeyValue( m_key, m_value );
		}
	}
};

class EntitySetClassnameSelected : public scene::Graph::Walker
{
	const char* m_classname;
	scene::Node* m_world;
	const bool m_2world;
public:
	EntitySetClassnameSelected( const char* classname )
		: m_classname( classname ), m_world( Map_FindWorldspawn( g_map ) ), m_2world( m_world && string_equal( m_classname, "worldspawn" ) ){
	}
	bool pre( const scene::Path& path, scene::Instance& instance ) const override {
		return true;
	}
	void post( const scene::Path& path, scene::Instance& instance ) const override {
		Entity* entity = Node_getEntity( path.top() );
		if ( entity != 0 && ( instance.childSelected() || Instance_isSelected( instance ) ) ) {
			if( path.top().get_pointer() == m_world ){ /* do not want to convert whole worldspawn entity */
				if( instance.childSelected() && !m_2world ){ /* create an entity from world brushes instead */
					EntityClass* entityClass = GlobalEntityClassManager().findOrInsert( m_classname, true );
					if( entityClass->fixedsize )
						return;

					//is important to have retexturing here; if doing in the end, undo doesn't succeed; //don't do this extra now, as it requires retexturing, working for subgraph
//					if ( string_compare_nocase_n( m_classname, "trigger_", 8 ) == 0 ){
//						Scene_PatchSetShader_Selected( GlobalSceneGraph(), GetCommonShader( "trigger" ).c_str() );
//						Scene_BrushSetShader_Selected( GlobalSceneGraph(), GetCommonShader( "trigger" ).c_str() );
//					}

					NodeSmartReference node( GlobalEntityCreator().createEntity( entityClass ) );
					Node_getTraversable( GlobalSceneGraph().root() )->insert( node );

					scene::Path entitypath( makeReference( GlobalSceneGraph().root() ) );
					entitypath.push( makeReference( node.get() ) );
					scene::Instance& entityInstance = findInstance( entitypath );

					if ( g_pGameDescription->mGameType == "doom3" ) {
						Node_getEntity( node )->setKeyValue( "model", Node_getEntity( node )->getKeyValue( "name" ) );
					}

					//Scene_parentSelectedBrushesToEntity( GlobalSceneGraph(), node );
					Scene_parentSubgraphSelectedBrushesToEntity( GlobalSceneGraph(), node, path );
					Scene_forEachChildSelectable( SelectableSetSelected( true ), entityInstance.path() );
				}
				return;
			}
			else if( m_2world ){ /* ungroupSelectedEntities */ //condition is skipped with world = 0, so code next to this may create multiple worldspawns; todo handle this very special case?
				if( node_is_group( path.top() ) ){
					parentBrushes( path.top(), *m_world );
					Path_deleteTop( path );
				}
				return;
			}

			EntityClass* eclass = GlobalEntityClassManager().findOrInsert( m_classname, node_is_group( path.top() ) );
			NodeSmartReference node( GlobalEntityCreator().createEntity( eclass ) );

			if( entity->isContainer() && eclass->fixedsize ){ /* group entity to point one */
				char value[64];
				sprintf( value, "%g %g %g", instance.worldAABB().origin[0], instance.worldAABB().origin[1], instance.worldAABB().origin[2] );
				entity->setKeyValue( "origin", value );
			}

			EntityCopyingVisitor visitor( *Node_getEntity( node ) );
//		entity->forEachKeyValue( visitor );

			NodeSmartReference child( path.top().get() );
			NodeSmartReference parent( path.parent().get() );
//		Node_getTraversable( parent )->erase( child );
			if ( Node_getTraversable( child ) != 0 && node_is_group( node ) ) { /* group entity to group one */
				parentBrushes( child, node );
			}
			Node_getTraversable( parent )->insert( node );

			entity->forEachKeyValue( visitor ); /* must do this after inserting node, otherwise problem: targeted + having model + not loaded b4 new entities aren't selectable normally + rendered only while 0 0 0 is rendered */

			if( !entity->isContainer() && !eclass->fixedsize ){ /* point entity to group one */
				AABB bounds( g_vector3_identity, Vector3( 16, 16, 16 ) );
				if ( !string_parse_vector3( entity->getKeyValue( "origin" ), bounds.origin ) ) {
					bounds.origin = g_vector3_identity;
				}
				Brush_ConstructPlacehoderCuboid( node.get(), bounds );
				Node_getEntity( node )->setKeyValue( "origin", "" );
			}

			Node_getTraversable( parent )->erase( child );
		}
	}
};

void Scene_EntitySetKeyValue_Selected( const char* key, const char* value ){
	GlobalSceneGraph().traverse( EntitySetKeyValueSelected( key, value ) );
}

void Scene_EntitySetClassname_Selected( const char* classname ){
	if ( GlobalSelectionSystem().countSelected() > 0 ) {
		StringOutputStream command( 64 );
		if( string_equal( classname, "worldspawn" ) )
			command << "ungroupSelectedEntities";
		else
			command << "entitySetClass -class " << classname;
		UndoableCommand undo( command );
		GlobalSceneGraph().traverse( EntitySetClassnameSelected( classname ) );
	}
}

void Entity_ungroup(){
	Scene_EntitySetClassname_Selected( "worldspawn" );
}

#if 0
void Entity_ungroupSelected(){
	if ( GlobalSelectionSystem().countSelected() < 1 ) {
		return;
	}

	UndoableCommand undo( "ungroupSelectedEntities" );

	scene::Instance &instance = GlobalSelectionSystem().ultimateSelected();
	scene::Path path = instance.path();

	scene::Node& world = Map_FindOrInsertWorldspawn( g_map );

	if ( !Node_isEntity( path.top() ) && path.size() > 1 ) {
		path.pop();
	}

	if ( Node_isEntity( path.top() )
	  && node_is_group( path.top() ) ) {
		if ( &world != path.top().get_pointer() ) {
			parentBrushes( path.top(), world );
			Path_deleteTop( path );
		}
	}
}
#endif

#if 0
class EntityFindSelected : public scene::Graph::Walker
{
public:
	mutable const scene::Path *groupPath;
	mutable scene::Instance *groupInstance;
	EntityFindSelected() : groupPath( 0 ), groupInstance( 0 ){
	}
	bool pre( const scene::Path& path, scene::Instance& instance ) const {
		return true;
	}
	void post( const scene::Path& path, scene::Instance& instance ) const {
		Entity* entity = Node_getEntity( path.top() );
		if ( entity != 0
		  && Instance_isSelected( instance )
		  && node_is_group( path.top() )
		  && !groupPath ) {
			groupPath = &path;
			groupInstance = &instance;
		}
	}
};

class EntityGroupSelected : public scene::Graph::Walker
{
	NodeSmartReference group, worldspawn;
//typedef std::pair<NodeSmartReference, NodeSmartReference> DeletionPair;
//Stack<DeletionPair> deleteme;
public:
	EntityGroupSelected( const scene::Path &p ) : group( p.top().get() ), worldspawn( Map_FindOrInsertWorldspawn( g_map ) ){
	}
	bool pre( const scene::Path& path, scene::Instance& instance ) const {
		return true;
	}
	void post( const scene::Path& path, scene::Instance& instance ) const {
		if ( Instance_isSelected( instance ) ) {
			Entity* entity = Node_getEntity( path.top() );
			if ( entity == 0 && Node_isPrimitive( path.top() ) ) {
				NodeSmartReference child( path.top().get() );
				NodeSmartReference parent( path.parent().get() );

				if ( path.size() >= 3 && parent != worldspawn ) {
					NodeSmartReference parentparent( path[path.size() - 3].get() );

					Node_getTraversable( parent )->erase( child );
					Node_getTraversable( group )->insert( child );

					if ( Node_getTraversable( parent )->empty() ) {
						//deleteme.push( DeletionPair( parentparent, parent ) );
						Node_getTraversable( parentparent )->erase( parent );
					}
				}
				else
				{
					Node_getTraversable( parent )->erase( child );
					Node_getTraversable( group )->insert( child );
				}
			}
		}
	}
};
/// moves selected primitives to entity, whose entityNode is selected or to worldspawn, if none
void Entity_moveSelectedPrimitives(){
	if ( GlobalSelectionSystem().countSelected() < 1 ) {
		return;
	}

	UndoableCommand undo( "reGroupSelectedEntities" );

	scene::Path world_path( makeReference( GlobalSceneGraph().root() ) );
	world_path.push( makeReference( Map_FindOrInsertWorldspawn( g_map ) ) );

	EntityFindSelected fse;
	GlobalSceneGraph().traverse( fse );
	if ( fse.groupPath ) {
		GlobalSceneGraph().traverse( EntityGroupSelected( *fse.groupPath ) );
	}
	else
	{
		GlobalSceneGraph().traverse( EntityGroupSelected( world_path ) );
	}
}
#else
/// moves selected primitives to entity, which is or its primitive is ultimateSelected() or firstSelected()
void Entity_moveSelectedPrimitives( bool toLast ){
	if ( GlobalSelectionSystem().countSelected() < 2 ) {
		globalErrorStream() << "Source and target entity primitives should be selected!\n";
		return;
	}

	const scene::Path& path = toLast? GlobalSelectionSystem().ultimateSelected().path() : GlobalSelectionSystem().firstSelected().path();
	scene::Node& node = ( !Node_isEntity( path.top() ) && path.size() > 1 )? path.parent() : path.top();

	if ( Node_isEntity( node ) && node_is_group( node ) ) {
		const auto command = StringStream<64>( "movePrimitivesToEntity ", Quoted( Node_getEntity( node )->getClassName() ) );
		UndoableCommand undo( command );
		Scene_parentSelectedBrushesToEntity( GlobalSceneGraph(), node );
	}
}
void Entity_moveSelectedPrimitivesToLast(){
	Entity_moveSelectedPrimitives( true );
}
void Entity_moveSelectedPrimitivesToFirst(){
	Entity_moveSelectedPrimitives( false );
}
#endif



void Entity_connectSelected(){
	if ( GlobalSelectionSystem().countSelected() == 2 ) {
		GlobalEntityCreator().connectEntities(
		    GlobalSelectionSystem().penultimateSelected().path(),
		    GlobalSelectionSystem().ultimateSelected().path(),
		    0
		);
	}
	else
	{
		globalErrorStream() << "entityConnectSelected: exactly two instances must be selected\n";
	}
}

void Entity_killconnectSelected(){
	if ( GlobalSelectionSystem().countSelected() == 2 ) {
		GlobalEntityCreator().connectEntities(
		    GlobalSelectionSystem().penultimateSelected().path(),
		    GlobalSelectionSystem().ultimateSelected().path(),
		    1
		);
	}
	else
	{
		globalErrorStream() << "entityKillConnectSelected: exactly two instances must be selected\n";
	}
}

AABB Doom3Light_getBounds( const AABB& workzone ){
	AABB aabb( workzone );

	Vector3 defaultRadius( 300, 300, 300 );
	if ( !string_parse_vector3( EntityClass_valueForKey( *GlobalEntityClassManager().findOrInsert( "light", false ), "light_radius" ), defaultRadius ) ) {
		globalErrorStream() << "Doom3Light_getBounds: failed to parse default light radius\n";
	}

	if ( aabb.extents[0] == 0 ) {
		aabb.extents[0] = defaultRadius[0];
	}
	if ( aabb.extents[1] == 0 ) {
		aabb.extents[1] = defaultRadius[1];
	}
	if ( aabb.extents[2] == 0 ) {
		aabb.extents[2] = defaultRadius[2];
	}

	if ( aabb_valid( aabb ) ) {
		return aabb;
	}
	return AABB( Vector3( 0, 0, 0 ), Vector3( 64, 64, 64 ) );
}


int g_iLastLightIntensity = 300;

void Entity_createFromSelection( const char* name, const Vector3& origin ){
#if 0
	if ( string_equal_nocase( name, "worldspawn" ) ) {
		qt_MessageBox( MainFrame_getWindow(), "Can't create an entity with worldspawn.", "info" );
		return;
	}
#else
	if ( string_equal( name, "worldspawn" ) ) {
		// only process if worldspawn is present
		// Map_FindOrInsertWorldspawn( g_map ) ) would be no action (since worldspawn insertion deselects everything)
		if( scene::Node* world_node = Map_FindWorldspawn( g_map ) ){
			UndoableCommand undo( "ungroupSelectedPrimitives" );
			Scene_parentSelectedBrushesToEntity( GlobalSceneGraph(), *world_node );
			return;
		}
	}
#endif

	const auto command = StringStream<64>( "entityCreate -class ", name );
	UndoableCommand undo( command );

	EntityClass* entityClass = GlobalEntityClassManager().findOrInsert( name, true );

	const bool isModel = entityClass->miscmodel_is
	                     || ( GlobalSelectionSystem().countSelected() == 0 && classname_equal( name, "func_static" ) && g_pGameDescription->mGameType == "doom3" );

	const bool brushesSelected = Scene_countSelectedBrushes( GlobalSceneGraph() ) != 0;

	//is important to have retexturing here; if doing in the end, undo doesn't succeed;
	if ( string_compare_nocase_n( name, "trigger_", 8 ) == 0 && brushesSelected && !entityClass->fixedsize ){
		//const char* shader = GetCommonShader( "trigger" ).c_str();
		Scene_PatchSetShader_Selected( GlobalSceneGraph(), GetCommonShader( "trigger" ).c_str() );
		Scene_BrushSetShader_Selected( GlobalSceneGraph(), GetCommonShader( "trigger" ).c_str() );
	}

	if ( !( entityClass->fixedsize || isModel ) && !brushesSelected ) {
		globalErrorStream() << "failed to create a group entity - no brushes are selected\n";
		return;
	}

	AABB workzone( aabb_for_minmax( Select_getWorkZone().d_work_min, Select_getWorkZone().d_work_max ) );


	NodeSmartReference node( GlobalEntityCreator().createEntity( entityClass ) );

	Node_getTraversable( GlobalSceneGraph().root() )->insert( node );

	scene::Path entitypath( makeReference( GlobalSceneGraph().root() ) );
	entitypath.push( makeReference( node.get() ) );
	scene::Instance& instance = findInstance( entitypath );

	Entity* entity = Node_getEntity( node );

	if ( entityClass->fixedsize || ( isModel && !brushesSelected ) ) {
		//Select_Delete();

		Transformable* transform = Instance_getTransformable( instance );
		if ( transform != 0 ) {
			transform->setType( TRANSFORM_PRIMITIVE );
			transform->setTranslation( origin );
			transform->freezeTransform();
		}

		GlobalSelectionSystem().setSelectedAll( false );

		Instance_setSelected( instance, true );
	}
	else
	{
		if ( g_pGameDescription->mGameType == "doom3" ) {
			entity->setKeyValue( "model", entity->getKeyValue( "name" ) );
		}

		Scene_parentSelectedBrushesToEntity( GlobalSceneGraph(), node );
		Scene_forEachChildSelectable( SelectableSetSelected( true ), instance.path() );
	}

	// tweaking: when right click dropping a light entity, ask for light value in a custom dialog box
	// see SF bug 105383

	if ( g_pGameDescription->mGameType == "hl" ) {
		// FIXME - Hydra: really we need a combined light AND color dialog for halflife.
		if ( string_equal_nocase( name, "light" )
		  || string_equal_nocase( name, "light_environment" )
		  || string_equal_nocase( name, "light_spot" ) ) {
			int intensity = g_iLastLightIntensity;

			if ( DoLightIntensityDlg( &intensity ) ) {
				g_iLastLightIntensity = intensity;
				char buf[30];
				sprintf( buf, "255 255 255 %d", intensity );
				entity->setKeyValue( "_light", buf );
			}
		}
	}
	else if ( string_equal_nocase( name, "light" ) ) {
		if ( g_pGameDescription->mGameType != "doom3" ) {
			int intensity = g_iLastLightIntensity;

			if ( DoLightIntensityDlg( &intensity ) ) {
				g_iLastLightIntensity = intensity;
				char buf[10];
				sprintf( buf, "%d", intensity );
				entity->setKeyValue( "light", buf );
			}
		}
		else if ( brushesSelected ) { // use workzone to set light position/size for doom3 lights, if there are brushes selected
			const AABB bounds( Doom3Light_getBounds( workzone ) );
			entity->setKeyValue( "origin", StringStream<64>( bounds.origin[0], ' ', bounds.origin[1], ' ', bounds.origin[2] ) );
			entity->setKeyValue( "light_radius", StringStream<64>( bounds.extents[0], ' ', bounds.extents[1], ' ', bounds.extents[2] ) );
		}
	}

	if ( isModel && string_empty( EntityClass_valueForKey( *entityClass, entityClass->miscmodel_key() ) ) ) { // handle set default model key value : no dialog needed
		const char* model = misc_model_dialog( MainFrame_getWindow() );
		if ( model != 0 ) {
			entity->setKeyValue( entityClass->miscmodel_key(), model );
		}
	}
}

void Entity_ungroupSelectedPrimitives(){
	Entity_createFromSelection( "worldspawn", g_vector3_identity );
}


/* scale color so that at least one component is at 1.0F */
void NormalizeColor( Vector3& color ){
	const auto max = vector3_max_component( color );
	if ( max == 0 )
		color = Vector3( 1, 1, 1 );
	else
		color /= max;
}

void Entity_normalizeColor(){
	if ( GlobalSelectionSystem().countSelected() != 0 ) {
		const scene::Path& path = GlobalSelectionSystem().ultimateSelected().path();
		Entity* entity = Node_getEntity( path.top() );

		if( entity == 0 && path.size() == 3 ){
			entity = Node_getEntity( path.parent() );
		}

		if ( entity != 0 ) {
			const char* strColor = entity->getKeyValue( "_color" );
			if ( !string_empty( strColor ) ) {
				Vector3 rgb;
				if ( string_parse_vector3( strColor, rgb ) ) {
					g_entity_globals.color_entity = rgb;
					NormalizeColor( g_entity_globals.color_entity );

					char buffer[128];
					sprintf( buffer, "%g %g %g", g_entity_globals.color_entity[0],
					                             g_entity_globals.color_entity[1],
					                             g_entity_globals.color_entity[2] );

					const auto command = StringStream<64>( "entityNormalizeColour ", buffer );
					UndoableCommand undo( command );
					Scene_EntitySetKeyValue_Selected( "_color", buffer );
				}
			}
		}
	}
}

void Entity_setColour(){
	if ( GlobalSelectionSystem().countSelected() != 0 ) {
		const scene::Path& path = GlobalSelectionSystem().ultimateSelected().path();
		Entity* entity = Node_getEntity( path.top() );

		if( entity == 0 && path.size() == 3 ){
			entity = Node_getEntity( path.parent() );
		}

		if ( entity != 0 ) {
			const char* strColor = entity->getKeyValue( "_color" );
			if ( !string_empty( strColor ) ) {
				Vector3 rgb;
				if ( string_parse_vector3( strColor, rgb ) ) {
					g_entity_globals.color_entity = rgb;
				}
			}
			if ( color_dialog( MainFrame_getWindow(), g_entity_globals.color_entity ) ) {
				char buffer[128];
				sprintf( buffer, "%g %g %g", g_entity_globals.color_entity[0],
				                             g_entity_globals.color_entity[1],
				                             g_entity_globals.color_entity[2] );

				const auto command = StringStream<64>( "entitySetColour ", buffer );
				UndoableCommand undo( command );
				Scene_EntitySetKeyValue_Selected( "_color", buffer );
			}
		}
	}
}

const char* misc_model_dialog( QWidget* parent, const char* filepath ){
	StringOutputStream buffer( 256 );

	if( !string_empty( filepath ) ){
		const char* root = GlobalFileSystem().findFile( filepath );
		if( !string_empty( root ) && file_is_directory( root ) )
			buffer << root << filepath;
	}
	if( buffer.empty() ){
		buffer << g_qeglobals.m_userGamePath << "models/";

		if ( !file_readable( buffer ) ) {
			// just go to fsmain
			buffer( g_qeglobals.m_userGamePath );
		}
	}

	const char *filename = file_dialog( parent, true, "Choose Model", buffer, ModelLoader::Name );
	if ( filename != 0 ) {
		// use VFS to get the correct relative path
		const char* relative = path_make_relative( filename, GlobalFileSystem().findRoot( filename ) );
		if ( relative == filename ) {
			globalWarningStream() << "WARNING: could not extract the relative path, using full path instead\n";
		}
		return relative;
	}
	return 0;
}

void Entity_reloadDefinitions(){
	if( ConfirmModified( "Reload Entity Definitions" ) ){
		GlobalEntityClassManager().unrealise();
		GlobalEntityClassManager().realise();
	}
}

/*
void LightRadiiImport( EntityCreator& self, bool value ){
	self.setLightRadii( value );
}
typedef ReferenceCaller<EntityCreator, void(bool), LightRadiiImport> LightRadiiImportCaller;

void LightRadiiExport( EntityCreator& self, const BoolImportCallback& importer ){
	importer( self.getLightRadii() );
}
typedef ReferenceCaller<EntityCreator, void(const BoolImportCallback&), LightRadiiExport> LightRadiiExportCaller;

void Entity_constructPreferences( PreferencesPage& page ){
	page.appendCheckBox(
		"Show", "Light Radii",
		LightRadiiImportCaller( GlobalEntityCreator() ),
		LightRadiiExportCaller( GlobalEntityCreator() )
		);
}
*/
void ShowNamesDistImport( EntityCreator& self, int value ){
	self.setShowNamesDist( value );
	UpdateAllWindows();
}
typedef ReferenceCaller<EntityCreator, void(int), ShowNamesDistImport> ShowNamesDistImportCaller;

void ShowNamesDistExport( EntityCreator& self, const IntImportCallback& importer ){
	importer( self.getShowNamesDist() );
}
typedef ReferenceCaller<EntityCreator, void(const IntImportCallback&), ShowNamesDistExport> ShowNamesDistExportCaller;


void ShowNamesRatioImport( EntityCreator& self, int value ){
	self.setShowNamesRatio( value );
	UpdateAllWindows();
}
typedef ReferenceCaller<EntityCreator, void(int), ShowNamesRatioImport> ShowNamesRatioImportCaller;

void ShowNamesRatioExport( EntityCreator& self, const IntImportCallback& importer ){
	importer( self.getShowNamesRatio() );
}
typedef ReferenceCaller<EntityCreator, void(const IntImportCallback&), ShowNamesRatioExport> ShowNamesRatioExportCaller;


void ShowTargetNamesImport( EntityCreator& self, bool value ){
	if( self.getShowTargetNames() != value )
		PreferencesDialog_restartRequired( "Entity Names = Targetnames" ); // technically map reloading or entities recreation do update too, as it's not LatchedValue
	self.setShowTargetNames( value );
}
typedef ReferenceCaller<EntityCreator, void(bool), ShowTargetNamesImport> ShowTargetNamesImportCaller;

void ShowTargetNamesExport( EntityCreator& self, const BoolImportCallback& importer ){
	importer( self.getShowTargetNames() );
}
typedef ReferenceCaller<EntityCreator, void(const BoolImportCallback&), ShowTargetNamesExport> ShowTargetNamesExportCaller;


void Entity_constructPreferences( PreferencesPage& page ){
	page.appendSpinner(	"Names Display Distance (3D)", 0, 200500,
	                    IntImportCallback( ShowNamesDistImportCaller( GlobalEntityCreator() ) ),
	                    IntExportCallback( ShowNamesDistExportCaller( GlobalEntityCreator() ) )
	                  );
	page.appendSpinner(	"Names Display Ratio (2D)", 0, 100500,
	                    IntImportCallback( ShowNamesRatioImportCaller( GlobalEntityCreator() ) ),
	                    IntExportCallback( ShowNamesRatioExportCaller( GlobalEntityCreator() ) )
	                  );
	page.appendCheckBox( "Entity Names", "= Targetnames",
	                     BoolImportCallback( ShowTargetNamesImportCaller( GlobalEntityCreator() ) ),
	                     BoolExportCallback( ShowTargetNamesExportCaller( GlobalEntityCreator() ) ) );
}
void Entity_constructPage( PreferenceGroup& group ){
	PreferencesPage page( group.createPage( "Entities", "Entity Display Preferences" ) );
	Entity_constructPreferences( page );
}
void Entity_registerPreferencesPage(){
	PreferencesDialog_addDisplayPage( makeCallbackF( Entity_constructPage ) );
}


void ShowLightRadiiExport( const BoolImportCallback& importer ){
	importer( GlobalEntityCreator().getLightRadii() );
}
typedef FreeCaller<void(const BoolImportCallback&), ShowLightRadiiExport> ShowLightRadiiExportCaller;
ShowLightRadiiExportCaller g_show_lightradii_caller;
ToggleItem g_show_lightradii_item( g_show_lightradii_caller );
void ToggleShowLightRadii(){
	GlobalEntityCreator().setLightRadii( !GlobalEntityCreator().getLightRadii() );
	g_show_lightradii_item.update();
	UpdateAllWindows();
}

// Emissive lights (openspec change emissive-area-lights, design decision 11): "Create area light from selected face",
// "Create proxy light from selection" and "Re-derive light from emitter". The maths lives in libs/emissivelight.h.

inline bool game_has_pbrLights(){
	return string_equal( GlobalRadiant().getRequiredGameDescriptionKeyValue( "entities" ), "pbr" );
}

namespace
{
using emissivelight::Vec2;
using emissivelight::Vec3;

/// a command's refusal: one console line saying why; nothing is changed
void EmissiveLight_refuse( const char* command, const char* why ){
	globalErrorStream() << command << ": " << why << '\n';
}

/// the emissive data of one shader, as the derivation reads it
struct EmitterShader
{
	bool emissive = false;         ///< IShader::hasEmissive()
	bool allAdditive = false;      ///< every map stage adds: a two-sided billboard, k = 2
	bool srcAlpha = false;         ///< GL_SRC_ALPHA GL_ONE: texels weighted by alpha
	Vec3 factor = Vec3( 0, 0, 0 ); ///< qer_pbr_emissiveStrength x the emissive colour
	std::string image;             ///< the emissive stage's image
	Image* pixels = nullptr;       ///< loaded from disk on first use; null when missing
	bool loaded = false;
};

/// shaders and emissive images read during one command, released at its end
class EmitterCache
{
	std::map<std::string, EmitterShader> m_shaders;
public:
	~EmitterCache(){
		for ( auto& [name, shader] : m_shaders )
			if ( shader.pixels != nullptr )
				shader.pixels->release();
	}
	EmitterShader& get( const char* name ){
		auto it = m_shaders.find( name );
		if ( it == m_shaders.end() ) {
			EmitterShader data;
			IShader* shader = GlobalShaderSystem().getShaderForName( name );
			data.emissive = shader->hasEmissive();
			data.allAdditive = shader->isAllAdditive();
			data.srcAlpha = shader->getEmissiveSrcAlpha();
			const Vector3& colour = shader->getEmissiveFactor();
			data.factor = Vec3( colour[0], colour[1], colour[2] ) * double( shader->getEmissiveStrength() );
			data.image = shader->getEmissiveName();
			shader->DecRef();
			it = m_shaders.emplace( name, std::move( data ) ).first;
		}
		return it->second;
	}
	/// the shader's emissive image, loaded with the textures cache's loader (independent of lighting mode)
	const Image* image( EmitterShader& shader ){
		if ( !shader.loaded ) {
			shader.loaded = true;
			shader.pixels = GlobalTexturesCache().loadImage( shader.image.c_str() );
			if ( shader.pixels != nullptr && ( shader.pixels->getWidth() == 0 || shader.pixels->getHeight() == 0 || shader.pixels->getRGBAPixels() == nullptr ) ) {
				shader.pixels->release();
				shader.pixels = nullptr;
			}
		}
		return shader.pixels;
	}
};

emissivelight::ImageRGBA8 ImageRGBA8_forImage( const Image& image ){
	emissivelight::ImageRGBA8 rgba;
	rgba.width = int( image.getWidth() );
	rgba.height = int( image.getHeight() );
	rgba.pixels = image.getRGBAPixels();
	return rgba;
}

/// the mean emissive radiance strength x colour x M over the UV polygon \p uvs
Vec3 EmitterShader_radiance( const EmitterShader& shader, const Image& image, const std::vector<Vec2>& uvs ){
	const Vec3 mean = emissivelight::FootprintMean( ImageRGBA8_forImage( image ), uvs, shader.srcAlpha );
	return emissivelight::EmissiveRadiance( 1, shader.factor, mean );
}

/// a brush face's winding, texture coordinates and outward normal
struct FacePolygon
{
	std::vector<Vec3> points;
	std::vector<Vec2> uvs;
	Vec3 normal;
};

FacePolygon FacePolygon_forFace( const Face& face ){
	FacePolygon polygon;
	const Winding& winding = face.getWinding();
	for ( std::size_t i = 0; i < winding.numpoints; ++i )
	{
		polygon.points.push_back( winding[i].vertex );
		polygon.uvs.push_back( Vec2( winding[i].texcoord.x(), winding[i].texcoord.y() ) );
	}
	polygon.normal = face.plane3().normal();
	return polygon;
}

/// the face's fan triangles, each wound so that ( b - a ) x ( c - a ) points along the outward normal
std::vector<std::array<Vec3, 3>> FacePolygon_triangles( const FacePolygon& polygon ){
	std::vector<std::array<Vec3, 3>> triangles;
	for ( std::size_t i = 1; i + 1 < polygon.points.size(); ++i )
	{
		std::array<Vec3, 3> t = { polygon.points[0], polygon.points[i], polygon.points[i + 1] };
		if ( vector3_dot( vector3_cross( t[1] - t[0], t[2] - t[0] ), polygon.normal ) < 0 ) {
			std::swap( t[1], t[2] );
		}
		triangles.push_back( t );
	}
	return triangles;
}

/// the face's affine texture mapping, through its fan triangle of largest area
std::optional<emissivelight::AffineUV> FacePolygon_affineUV( const FacePolygon& polygon ){
	std::size_t best = 0;
	double bestArea = 0;
	for ( std::size_t i = 1; i + 1 < polygon.points.size(); ++i )
	{
		const double area = vector3_length( vector3_cross( polygon.points[i] - polygon.points[0], polygon.points[i + 1] - polygon.points[0] ) );
		if ( area > bestArea ) {
			bestArea = area;
			best = i;
		}
	}
	if ( best == 0 ) {
		return std::nullopt;
	}
	return emissivelight::AffineUVFromTriangle( polygon.points[0], polygon.points[best], polygon.points[best + 1],
	                                            polygon.uvs[0], polygon.uvs[best], polygon.uvs[best + 1] );
}

/// a patch's tessellated triangles with their texture coordinates, each wound to its vertex normals
struct PatchTriangle
{
	std::array<Vec3, 3> points;
	std::vector<Vec2> uvs;
};

std::vector<PatchTriangle> Patch_triangles( const Patch& patch ){
	std::vector<PatchTriangle> triangles;
	const PatchTesselation& tess = patch.getTesselation();
	const auto add = [&]( RenderIndex a, RenderIndex b, RenderIndex c ){
		const ArbitraryMeshVertex& va = tess.m_vertices.data()[a];
		const ArbitraryMeshVertex& vb = tess.m_vertices.data()[b];
		const ArbitraryMeshVertex& vc = tess.m_vertices.data()[c];
		PatchTriangle t;
		t.points = { Vec3( vertex3f_to_vector3( va.vertex ) ), Vec3( vertex3f_to_vector3( vb.vertex ) ), Vec3( vertex3f_to_vector3( vc.vertex ) ) };
		t.uvs = { Vec2( va.texcoord.s(), va.texcoord.t() ), Vec2( vb.texcoord.s(), vb.texcoord.t() ), Vec2( vc.texcoord.s(), vc.texcoord.t() ) };
		const Vec3 normal = Vec3( normal3f_to_vector3( va.normal ) ) + Vec3( normal3f_to_vector3( vb.normal ) ) + Vec3( normal3f_to_vector3( vc.normal ) );
		if ( vector3_dot( vector3_cross( t.points[1] - t.points[0], t.points[2] - t.points[0] ), normal ) < 0 ) {
			std::swap( t.points[1], t.points[2] );
			std::swap( t.uvs[1], t.uvs[2] );
		}
		triangles.push_back( std::move( t ) );
	};
	// quad strips: indices 2k and 2k + 1 are one column of the strip
	for ( std::size_t s = 0; s < tess.m_numStrips; ++s )
	{
		const RenderIndex* strip = tess.m_indices.data() + s * tess.m_lenStrips;
		for ( std::size_t k = 0; k + 3 < tess.m_lenStrips; k += 2 )
		{
			add( strip[k], strip[k + 1], strip[k + 2] );
			add( strip[k + 2], strip[k + 1], strip[k + 3] );
		}
	}
	return triangles;
}

/// one emitting surface's proxy contribution; the surface's radiance is its area-weighted mean
struct GatherResult
{
	std::vector<emissivelight::ProxySurface> surfaces;
	std::size_t candidates = 0; ///< faces and patches considered, emissive or not
};

/// "Emitter gathering for proxies": the selected brush faces, every face of each wholly selected brush and the
/// selected patches (by their tessellation), keeping those whose shader has an emissive stage. Returns false after
/// printing a refusal when an emissive image is missing.
bool EmissiveLight_gatherProxySurfaces( const char* command, EmitterCache& cache, GatherResult& result ){
	std::vector<const Face*> faces;
	std::set<const Face*> seen;
	g_SelectedFaceInstances.foreach( [&]( FaceInstance& instance ){
		if ( seen.insert( &instance.getFace() ).second )
			faces.push_back( &instance.getFace() );
	} );
	Scene_forEachSelectedBrush( [&]( BrushInstance& brush ){
		Brush_forEachFace( brush, [&]( Face& face ){
			if ( seen.insert( &face ).second )
				faces.push_back( &face );
		} );
	} );
	std::vector<const Patch*> patches;
	Scene_forEachSelectedPatch( [&]( PatchInstance& patch ){
		patches.push_back( &patch.getPatch() );
	} );
	result.candidates = faces.size() + patches.size();

	for ( const Face* face : faces )
	{
		EmitterShader& shader = cache.get( face->GetShader() );
		if ( !shader.emissive || !face->contributes() ) {
			continue;
		}
		const Image* image = cache.image( shader );
		if ( image == nullptr ) {
			EmissiveLight_refuse( command, StringStream( "emissive image ", shader.image.c_str(), " not found" ) );
			return false;
		}
		const FacePolygon polygon = FacePolygon_forFace( *face );
		const Vec3 radiance = EmitterShader_radiance( shader, *image, polygon.uvs );
		result.surfaces.push_back( emissivelight::ProxySurfaceFromTriangles( radiance, shader.allAdditive, FacePolygon_triangles( polygon ) ) );
	}
	for ( const Patch* patch : patches )
	{
		EmitterShader& shader = cache.get( patch->GetShader() );
		if ( !shader.emissive ) {
			continue;
		}
		const Image* image = cache.image( shader );
		if ( image == nullptr ) {
			EmissiveLight_refuse( command, StringStream( "emissive image ", shader.image.c_str(), " not found" ) );
			return false;
		}
		// a patch's texture mapping is not affine: its mean is weighted by each triangle's surface area
		const std::vector<PatchTriangle> triangles = Patch_triangles( *patch );
		std::vector<std::array<Vec3, 3>> points;
		Vec3 sum( 0, 0, 0 );
		double total = 0;
		for ( const PatchTriangle& t : triangles )
		{
			const double area = 0.5 * vector3_length( vector3_cross( t.points[1] - t.points[0], t.points[2] - t.points[0] ) );
			points.push_back( t.points );
			if ( area > 0 ) {
				sum += EmitterShader_radiance( shader, *image, t.uvs ) * area;
				total += area;
			}
		}
		if ( total > 0 ) {
			// the normal comes from the tessellation (sum of A_t n_t), never a normalised patch normal
			result.surfaces.push_back( emissivelight::ProxySurfaceFromTriangles( sum / total, shader.allAdditive, points ) );
		}
	}
	return true;
}

/// a key value: "%g" at six significant digits, snapped to an integer within 1e-9
std::string EmissiveLight_formatValue( double value ){
	const double r = std::round( value );
	if ( std::fabs( value - r ) < 1e-9 ) {
		value = r;
	}
	if ( value == 0 ) {
		value = 0;
	}
	char buffer[32];
	std::snprintf( buffer, sizeof( buffer ), "%g", value );
	return buffer;
}

std::string EmissiveLight_formatVector( const Vec3& v ){
	return emissivelight::FormatAngles( v );
}

void EmissiveLight_setKey( Entity& entity, const char* key, const std::string& value ){
	entity.setKeyValue( key, value.c_str() );
	globalOutputStream() << "  " << key << " " << value.c_str() << '\n';
}

/// creates a point entity of \p classname at the map root and makes it the only selection
Entity& EmissiveLight_createEntity( const char* classname, scene::Instance*& created ){
	EntityClass* entityClass = GlobalEntityClassManager().findOrInsert( classname, true );
	NodeSmartReference node( GlobalEntityCreator().createEntity( entityClass ) );
	Node_getTraversable( GlobalSceneGraph().root() )->insert( node );
	scene::Path entitypath( makeReference( GlobalSceneGraph().root() ) );
	entitypath.push( makeReference( node.get() ) );
	created = &findInstance( entitypath );
	return *Node_getEntity( node );
}

void EmissiveLight_selectOnly( scene::Instance& instance ){
	GlobalSelectionSystem().setSelectedAllComponents( false );
	GlobalSelectionSystem().setSelectedAll( false );
	if ( GlobalSelectionSystem().Mode() == SelectionSystem::eComponent ) {
		SelectionSystem_DefaultMode();
		ComponentModeChanged();
	}
	Instance_setSelected( instance, true );
}

/// parses "x y z"; false when missing or unparsable
bool EmissiveLight_parseVector( const char* value, Vec3& v ){
	Vector3 f;
	if ( !string_parse_vector3( value, f ) ) {
		return false;
	}
	v = Vec3( f[0], f[1], f[2] );
	return true;
}

/// a light's "pitch yaw roll" from `angles`, else from `angle` (a yaw; -1 up, -2 down), else 0 0 0, as q3map2 reads
/// them (AnglesFromKeys in tools/quake3/q3map2/light_sh_model.cpp)
Vec3 EmissiveLight_anglesFromKeys( const Entity& entity ){
	const char* angles = entity.getKeyValue( "angles" );
	const char* angle = entity.getKeyValue( "angle" );
	Vec3 result( 0, 0, 0 );
	if ( !string_empty( angles ) ) {
		if ( !EmissiveLight_parseVector( angles, result ) ) {
			result = Vec3( 0, 0, 0 );
		}
	}
	else if ( !string_empty( angle ) ) {
		float a;
		if ( !string_parse_float( angle, a ) ) {
			a = 0;
		}
		result = a == -1 ? Vec3( -90, 0, 0 ) : a == -2 ? Vec3( 90, 0, 0 ) : Vec3( 0, a, 0 );
	}
	return result;
}

double EmissiveLight_sizeKey( const Entity& entity, const char* key ){
	float value;
	if ( !string_parse_float( entity.getKeyValue( key ), value ) || !( value > 0 ) ) {
		return 64;
	}
	return value;
}

const char* const c_areaLightFromFace = "Create area light from selected face";
const char* const c_proxyLightFromSelection = "Create proxy light from selection";
const char* const c_rederiveLight = "Re-derive light from emitter";

} // namespace

void Entity_areaLightFromFace(){
	const char* command = c_areaLightFromFace;
	std::vector<const Face*> faces;
	g_SelectedFaceInstances.foreach( [&faces]( FaceInstance& instance ){ faces.push_back( &instance.getFace() ); } );
	if ( faces.size() != 1 ) {
		EmissiveLight_refuse( command, faces.empty() ? "no brush face is selected" : StringStream( faces.size(), " brush faces are selected; select exactly one" ).c_str() );
		return;
	}
	const Face& face = *faces.front();
	const FacePolygon polygon = FacePolygon_forFace( face );
	const std::optional<std::array<Vec3, 4>> corners = emissivelight::RectangleCorners( polygon.points );
	if ( !corners ) {
		EmissiveLight_refuse( command, "face is not a rectangle" );
		return;
	}
	EmitterCache cache;
	EmitterShader& shader = cache.get( face.GetShader() );
	if ( !shader.emissive ) {
		EmissiveLight_refuse( command, StringStream( "shader ", face.GetShader(), " has no emissive stage" ) );
		return;
	}
	const Image* image = cache.image( shader );
	if ( image == nullptr ) {
		EmissiveLight_refuse( command, StringStream( "emissive image ", shader.image.c_str(), " not found" ) );
		return;
	}
	const std::optional<emissivelight::Split> split = emissivelight::SplitRadiance( EmitterShader_radiance( shader, *image, polygon.uvs ) );
	if ( !split ) {
		EmissiveLight_refuse( command, StringStream( "the emissive radiance of ", face.GetShader(), " over the face is zero" ) );
		return;
	}
	const emissivelight::Rect rect = emissivelight::AreaRectFromCorners( *corners, polygon.normal );

	UndoableCommand undo( "entityAreaLightFromFace" );
	scene::Instance* instance;
	Entity& entity = EmissiveLight_createEntity( "light_area", instance );
	globalOutputStream() << command << ": light_area\n";
	EmissiveLight_setKey( entity, "origin", EmissiveLight_formatVector( rect.centre ) );
	EmissiveLight_setKey( entity, "angles", emissivelight::FormatAngles( emissivelight::AnglesFromBasis( rect.forward, rect.right ) ) );
	EmissiveLight_setKey( entity, "width", EmissiveLight_formatValue( rect.width ) );
	EmissiveLight_setKey( entity, "height", EmissiveLight_formatValue( rect.height ) );
	EmissiveLight_setKey( entity, "radius", EmissiveLight_formatValue( emissivelight::AreaLightRadius( rect.width, rect.height ) ) );
	EmissiveLight_setKey( entity, "intensity", EmissiveLight_formatValue( split->intensity ) );
	EmissiveLight_setKey( entity, "_color", EmissiveLight_formatVector( split->colour ) );
	EmissiveLight_selectOnly( *instance );
}

void Entity_proxyLightFromSelection(){
	const char* command = c_proxyLightFromSelection;
	EmitterCache cache;
	GatherResult gathered;
	if ( !EmissiveLight_gatherProxySurfaces( command, cache, gathered ) ) {
		return;
	}
	if ( gathered.surfaces.empty() ) {
		EmissiveLight_refuse( command, gathered.candidates == 0 ? "no brush, face or patch is selected" : "no selected surface has an emissive stage" );
		return;
	}
	const emissivelight::Proxy proxy = emissivelight::DeriveProxy( gathered.surfaces );
	const std::optional<emissivelight::Split> split = emissivelight::SplitRadiance( proxy.flux );
	if ( !split ) {
		EmissiveLight_refuse( command, "the emitted flux is zero" );
		return;
	}
	const bool spot = proxy.kind == emissivelight::ProxyKind::Spot;

	UndoableCommand undo( "entityProxyLightFromSelection" );
	scene::Instance* instance;
	Entity& entity = EmissiveLight_createEntity( spot ? "light_spot" : "light", instance );
	globalOutputStream() << command << ": " << ( spot ? "light_spot" : "light" ) << " from " << gathered.surfaces.size() << " surface(s)\n";
	EmissiveLight_setKey( entity, "origin", EmissiveLight_formatVector( proxy.origin ) );
	if ( spot ) {
		EmissiveLight_setKey( entity, "angles", emissivelight::FormatAngles( proxy.angles ) );
		EmissiveLight_setKey( entity, "cone", "90" );
		EmissiveLight_setKey( entity, "cone_inner", "89" );
		EmissiveLight_setKey( entity, "_shadows", "0" );
	}
	EmissiveLight_setKey( entity, "_proxy", "1" );
	EmissiveLight_setKey( entity, "intensity", EmissiveLight_formatValue( split->intensity ) );
	EmissiveLight_setKey( entity, "_color", EmissiveLight_formatVector( split->colour ) );
	EmissiveLight_selectOnly( *instance );
}

void Entity_rederiveLight(){
	const char* command = c_rederiveLight;
	class LightCollector : public SelectionSystem::Visitor
	{
	public:
		mutable std::vector<Entity*> m_lights;
		void visit( scene::Instance& instance ) const override {
			Entity* entity = Node_getEntity( instance.path().top() );
			if ( entity != 0 ) {
				const char* classname = entity->getClassName();
				if ( string_equal( classname, "light" ) || string_equal( classname, "light_spot" )
				  || string_equal( classname, "light_sun" ) || string_equal( classname, "light_area" ) ) {
					m_lights.push_back( entity );
				}
			}
		}
	} collector;
	GlobalSelectionSystem().foreachSelected( collector );
	if ( collector.m_lights.size() != 1 ) {
		EmissiveLight_refuse( command, collector.m_lights.empty() ? "no light is selected" : "more than one light is selected; select exactly one" );
		return;
	}
	Entity& light = *collector.m_lights.front();
	const char* classname = light.getClassName();
	const bool area = string_equal( classname, "light_area" );
	// "_proxy" is read here only (the entity module keeps no copy), as q3map2 reads a boolean: the value starts with '1'
	const bool proxy = ( string_equal( classname, "light" ) || string_equal( classname, "light_spot" ) ) && light.getKeyValue( "_proxy" )[0] == '1';
	if ( !area && !proxy ) {
		EmissiveLight_refuse( command, StringStream( "the selected ", classname, " is not an area light or a point or spot proxy" ) );
		return;
	}

	EmitterCache cache;
	std::optional<emissivelight::Split> split;
	std::string source;
	if ( area ) {
		Vec3 origin( 0, 0, 0 );
		EmissiveLight_parseVector( light.getKeyValue( "origin" ), origin );
		const Vec3 angles = EmissiveLight_anglesFromKeys( light );
		const double width = EmissiveLight_sizeKey( light, "width" ), height = EmissiveLight_sizeKey( light, "height" );
		const emissivelight::AttachRect attach = emissivelight::AttachRectFromAngles( origin, angles, width, height );
		const emissivelight::Basis basis = emissivelight::AngleVectors( angles );
		emissivelight::Rect rect;
		rect.centre = origin;
		rect.forward = basis.forward;
		rect.right = basis.right;
		rect.up = basis.up;
		rect.width = width;
		rect.height = height;

		// q3map2 -shbake's attachment test applied to every brush face in the map
		std::vector<const Face*> attached;
		Scene_ForEachBrush_ForEachFace( GlobalSceneGraph(), [&]( Face& face ){
			if ( face.contributes() && cache.get( face.GetShader() ).emissive ) {
				const FacePolygon polygon = FacePolygon_forFace( face );
				if ( emissivelight::FaceAttached( attach, polygon.points, polygon.normal ) ) {
					attached.push_back( &face );
				}
			}
		} );
		if ( attached.empty() ) {
			EmissiveLight_refuse( command, "no emitter found: no emissive face lies on the area light's rectangle" );
			return;
		}
		std::vector<emissivelight::Overlap> overlaps;
		for ( const Face* face : attached )
		{
			EmitterShader& shader = cache.get( face->GetShader() );
			const Image* image = cache.image( shader );
			if ( image == nullptr ) {
				EmissiveLight_refuse( command, StringStream( "emissive image ", shader.image.c_str(), " not found" ) );
				return;
			}
			const FacePolygon polygon = FacePolygon_forFace( *face );
			const emissivelight::ClippedPolygon clipped = emissivelight::ClipPolygonToRect( polygon.points, rect );
			const std::optional<emissivelight::AffineUV> uv = FacePolygon_affineUV( polygon );
			if ( clipped.area > 0 && uv ) {
				std::vector<Vec2> uvs;
				for ( const Vec3& p : clipped.points )
					uvs.push_back( ( *uv )( p ) );
				overlaps.push_back( { clipped.area, EmitterShader_radiance( shader, *image, uvs ) } );
			}
		}
		split = emissivelight::SplitRadiance( emissivelight::OverlapWeightedRadiance( overlaps ) );
		source = StringStream( "light_area from ", attached.size(), " attached face(s)" ).c_str();
	}
	else
	{
		GatherResult gathered;
		if ( !EmissiveLight_gatherProxySurfaces( command, cache, gathered ) ) {
			return;
		}
		if ( gathered.candidates == 0 ) {
			EmissiveLight_refuse( command, "select the emitting surfaces together with the proxy light" );
			return;
		}
		if ( gathered.surfaces.empty() ) {
			EmissiveLight_refuse( command, "no emitter found: no selected surface has an emissive stage" );
			return;
		}
		split = emissivelight::SplitRadiance( emissivelight::DeriveProxy( gathered.surfaces ).flux );
		source = StringStream( classname, " proxy from ", gathered.surfaces.size(), " surface(s)" ).c_str();
	}
	if ( !split ) {
		EmissiveLight_refuse( command, "the derived value is zero" );
		return;
	}

	UndoableCommand undo( "entityRederiveLight" );
	globalOutputStream() << command << ": " << source.c_str() << '\n';
	EmissiveLight_setKey( light, "intensity", EmissiveLight_formatValue( split->intensity ) );
	EmissiveLight_setKey( light, "_color", EmissiveLight_formatVector( split->colour ) );
}


inline bool game_has_killConnect(){
	return g_pGameDescription->mGameType == "nexuiz"
	    || g_pGameDescription->mGameType == "xonotic"
	    || g_pGameDescription->mGameType == "q1";
}

void Entity_constructMenu( QMenu* menu ){
	create_menu_item_with_mnemonic( menu, "&Connect Entities", "EntitiesConnect" );
	if ( game_has_killConnect() ) {
		create_menu_item_with_mnemonic( menu, "&KillConnect Entities", "EntitiesKillConnect" );
	}
	create_menu_item_with_mnemonic( menu, "&Move Primitives to Entity", "EntityMovePrimitivesToLast" );
	create_menu_item_with_mnemonic( menu, "&Select Color...", "EntityColorSet" );
	create_menu_item_with_mnemonic( menu, "&Normalize Color", "EntityColorNormalize" );
	if ( game_has_pbrLights() ) {
		menu->addSeparator();
		create_menu_item_with_mnemonic( menu, c_areaLightFromFace, "EntityAreaLightFromFace" );
		create_menu_item_with_mnemonic( menu, c_proxyLightFromSelection, "EntityProxyLightFromSelection" );
		create_menu_item_with_mnemonic( menu, c_rederiveLight, "EntityRederiveLight" );
	}
	menu->addSeparator();
	create_menu_item_with_mnemonic( menu, "Reload Entity Definitions", "EntityReloadDefinitions" );
}

void Entity_registerShortcuts(){
	command_connect_accelerator( "EntityMovePrimitivesToFirst" );
	command_connect_accelerator( "EntityUngroup" );
	command_connect_accelerator( "EntityUngroupPrimitives" );
}



#include "preferencesystem.h"
#include "stringio.h"

void Entity_Construct(){
	GlobalCommands_insert( "EntityColorSet", makeCallbackF( Entity_setColour ), QKeySequence( "K" ) );
	GlobalCommands_insert( "EntityColorNormalize", makeCallbackF( Entity_normalizeColor ) );
	GlobalCommands_insert( "EntitiesConnect", makeCallbackF( Entity_connectSelected ), QKeySequence( "Ctrl+K" ) );
	if ( game_has_killConnect() )
		GlobalCommands_insert( "EntitiesKillConnect", makeCallbackF( Entity_killconnectSelected ), QKeySequence( "Shift+K" ) );
	GlobalCommands_insert( "EntityMovePrimitivesToLast", makeCallbackF( Entity_moveSelectedPrimitivesToLast ), QKeySequence( "Ctrl+M" ) );
	GlobalCommands_insert( "EntityMovePrimitivesToFirst", makeCallbackF( Entity_moveSelectedPrimitivesToFirst ) );
	GlobalCommands_insert( "EntityUngroup", makeCallbackF( Entity_ungroup ) );
	GlobalCommands_insert( "EntityUngroupPrimitives", makeCallbackF( Entity_ungroupSelectedPrimitives ) );
	GlobalCommands_insert( "EntityReloadDefinitions", makeCallbackF( Entity_reloadDefinitions ) );
	if ( game_has_pbrLights() ) {
		GlobalCommands_insert( "EntityAreaLightFromFace", makeCallbackF( Entity_areaLightFromFace ) );
		GlobalCommands_insert( "EntityProxyLightFromSelection", makeCallbackF( Entity_proxyLightFromSelection ) );
		GlobalCommands_insert( "EntityRederiveLight", makeCallbackF( Entity_rederiveLight ) );
	}

	GlobalToggles_insert( "ShowLightRadiuses", makeCallbackF( ToggleShowLightRadii ), ToggleItem::AddCallbackCaller( g_show_lightradii_item ) );

	GlobalPreferenceSystem().registerPreference( "EntitySelectedColor", Vector3ImportStringCaller( g_entity_globals.color_entity ), Vector3ExportStringCaller( g_entity_globals.color_entity ) );
	GlobalPreferenceSystem().registerPreference( "LastLightIntensity", IntImportStringCaller( g_iLastLightIntensity ), IntExportStringCaller( g_iLastLightIntensity ) );

	Entity_registerPreferencesPage();
}

void Entity_Destroy(){
}

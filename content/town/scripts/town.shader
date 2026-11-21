// Town materials: vanilla Quake 3 stages, PBR data in top-level qer_pbr_* keywords.
// Stock renderers ignore qer_* lines; the editor preview and renderer_sh read them.
// Normal maps are *_nrm (never _n, _nh or _s, which rend2 loads by name). The metallic-roughness
// maps hold roughness in G and metallic in B; occlusion is in R.
// Every image is a placeholder written by tools/town/gen_textures.py.

// ---------------------------------------------------------------- street

textures/town/asphalt
{
	qer_editorimage textures/town/asphalt_c
	qer_pbr_normal textures/town/asphalt_nrm
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 1
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/asphalt_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

textures/town/sidewalk
{
	qer_editorimage textures/town/sidewalk_c
	qer_pbr_normal textures/town/sidewalk_nrm
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.9
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/sidewalk_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// facades: every map type
textures/town/brick
{
	qer_editorimage textures/town/brick_c
	qer_pbr_normal textures/town/brick_nrm
	qer_pbr_metallicRoughness textures/town/brick_mr
	qer_pbr_occlusion textures/town/brick_ao
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/brick_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// an unlit window: an opaque dark pane, smooth enough to catch the flashlight
textures/town/window
{
	qer_editorimage textures/town/window_c
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.15
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/window_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// a door that teleports: a static slab behind a trigger_teleport
textures/town/door
{
	qer_editorimage textures/town/door_c
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.6
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/door_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// lamp posts, arms and the outside of the reflector covers (factors come from the map)
textures/town/metal
{
	qer_editorimage textures/town/metal_c
	qer_pbr_metallicRoughness textures/town/metal_mr
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/metal_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// the inside of a street lamp's reflector cover: emissive. Each lamp has a light_spot with _proxy 1 attached;
// after changing lamp_e or the strength, re-derive those lights (content/town/README.md, "Lights").
textures/town/lamp
{
	qer_editorimage textures/town/lamp_c
	qer_pbr_emissiveStrength 16
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.5
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/lamp_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
	{
		map textures/town/lamp_e
		blendFunc add
		rgbGen const ( 1 1 1 )
	}
}

// chain-link fence: alpha-tested, both sides, solid, casts through its alpha
textures/town/fence
{
	qer_editorimage textures/town/fence
	qer_pbr_metallicFactor 0.6
	qer_pbr_roughnessFactor 0.5
	surfaceparm alphashadow
	surfaceparm trans
	cull none
	{
		map textures/town/fence
		alphaFunc GE128
		depthWrite
		rgbGen identity
	}
	{
		map $lightmap
		blendFunc filter
		depthFunc equal
		rgbGen identity
	}
}

// ---------------------------------------------------------------- interiors

textures/town/plaster
{
	qer_editorimage textures/town/plaster_c
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.8
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/plaster_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

textures/town/woodfloor
{
	qer_editorimage textures/town/woodfloor_c
	qer_pbr_normal textures/town/woodfloor_nrm
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.5
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/woodfloor_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// a ceiling light panel: emissive, with a light_area attached (re-derive it after changing panel_e or the strength)
textures/town/panel
{
	qer_editorimage textures/town/panel_c
	qer_pbr_emissiveStrength 8
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.6
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/panel_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
	{
		map textures/town/panel_e
		blendFunc add
		rgbGen const ( 1 1 1 )
	}
}

// ---------------------------------------------------------------- vegetation
// Trees and grass are misc_model meshes (tools/benchmark/gen_foliage.py, called by tools/town/gen_textures.py).
// q3map2 merges them into the BSP and vertex-lights model triangles unless the shader asks for lightmaps, so every
// vegetation shader carries q3map_forceMeta and a $lightmap stage.

// trunk and branches: solid through q3map_clipModel
textures/town/bark
{
	qer_editorimage textures/town/bark_c
	qer_pbr_normal textures/town/bark_nrm
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.9
	q3map_forceMeta
	q3map_clipModel
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/bark_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// leaf cards: alpha-tested, both sides, cast through their alpha
textures/town/leaves
{
	qer_editorimage textures/town/leaves
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.7
	q3map_forceMeta
	q3map_lightmapSampleSize 32
	surfaceparm alphashadow
	surfaceparm trans
	surfaceparm nonsolid
	cull none
	{
		map textures/town/leaves
		alphaFunc GE128
		depthWrite
		rgbGen identity
	}
	{
		map $lightmap
		blendFunc filter
		depthFunc equal
		rgbGen identity
	}
}

// grass (static crossed quads): alpha-tested, both sides, casts nothing
textures/town/grass
{
	qer_editorimage textures/town/grass
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.8
	q3map_forceMeta
	q3map_lightmapSampleSize 32
	surfaceparm trans
	surfaceparm nonsolid
	cull none
	{
		map textures/town/grass
		alphaFunc GE128
		depthWrite
		rgbGen identity
	}
	{
		map $lightmap
		blendFunc filter
		depthFunc equal
		rgbGen identity
	}
}

// the lawn's ground
textures/town/dirt
{
	qer_editorimage textures/town/dirt_c
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 1
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/town/dirt_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// ---------------------------------------------------------------- sky and fog
// The sky is one flat colour and the fog volume has the same colour, so a building fading into fog meets the sky
// without a seam. tools/town/gen_textures.py paints sky.tga and refuses to run when this fogparms colour differs from
// it. The sun is the map's light_sun.

textures/town/sky
{
	qer_editorimage textures/town/sky
	surfaceparm sky
	surfaceparm noimpact
	surfaceparm nolightmap
	skyparms - 512 -
	{
		map textures/town/sky
	}
}

textures/town/fog
{
	qer_editorimage textures/town/sky
	qer_trans 0.35
	surfaceparm trans
	surfaceparm nonsolid
	surfaceparm fog
	surfaceparm nolightmap
	fogparms ( 0.200392 0.258824 0.231373 ) 2048
}

// ---------------------------------------------------------------- tool shaders

textures/town/caulk
{
	qer_editorimage textures/town/caulk
	surfaceparm nodraw
	surfaceparm nolightmap
	surfaceparm nomarks
}

textures/town/clip
{
	qer_editorimage textures/town/clip
	qer_trans 0.4
	surfaceparm nodraw
	surfaceparm nolightmap
	surfaceparm nonsolid
	surfaceparm trans
	surfaceparm nomarks
	surfaceparm noimpact
	surfaceparm playerclip
}

textures/town/hint
{
	qer_editorimage textures/town/hint
	qer_trans 0.3
	qer_nocarve
	surfaceparm nodraw
	surfaceparm nonsolid
	surfaceparm structural
	surfaceparm trans
	surfaceparm noimpact
	surfaceparm hint
}

textures/town/skip
{
	qer_editorimage textures/town/skip
	qer_trans 0.3
	qer_nocarve
	surfaceparm nodraw
	surfaceparm nonsolid
	surfaceparm structural
	surfaceparm trans
	surfaceparm noimpact
	surfaceparm skip
}

// surfaceparm trigger makes the brush non-solid to q3map2, so -shbake does not turn it into a shadow-casting hull
textures/town/trigger
{
	qer_editorimage textures/town/trigger
	qer_trans 0.4
	qer_nocarve
	surfaceparm nodraw
	surfaceparm trigger
}

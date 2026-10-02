// Benchmark materials: vanilla Quake 3 stages, PBR data in top-level qer_pbr_* keywords.
// Stock renderers ignore qer_* lines; the editor preview and renderer_sh read them.
// Normal maps are *_nrm (never _n, _nh or _s, which rend2 loads by name). The metallic-roughness
// maps hold roughness in G and metallic in B; occlusion is in R.

// brick: every map type
textures/bench/brick
{
	qer_editorimage textures/bench/brick_c
	qer_pbr_normal textures/bench/brick_nrm
	qer_pbr_metallicRoughness textures/bench/brick_mr
	qer_pbr_occlusion textures/bench/brick_ao
	qer_pbr_roughnessFactor 1
	q3map_lightmapsamplesize 8
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/bench/brick_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// rough dielectric ground
textures/bench/asphalt
{
	qer_editorimage textures/bench/asphalt_c
	qer_pbr_normal textures/bench/asphalt_nrm
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 1
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/bench/asphalt_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

textures/bench/concrete
{
	qer_editorimage textures/bench/concrete_c
	qer_pbr_normal textures/bench/concrete_nrm
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.9
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/bench/concrete_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

textures/bench/plaster
{
	qer_editorimage textures/bench/plaster_c
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.8
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/bench/plaster_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// polished metal: tight highlight tinted by the base colour, no diffuse (factors come from the map)
textures/bench/metal
{
	qer_editorimage textures/bench/metal_c
	qer_pbr_normal textures/bench/metal_nrm
	qer_pbr_metallicRoughness textures/bench/metal_mr
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/bench/metal_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// emissive neon sign: the additive stage is the emission, rgbGen const its colour
textures/bench/neon
{
	qer_editorimage textures/bench/neon_c
	qer_pbr_emissiveStrength 8
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.6
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/bench/neon_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
	{
		map textures/bench/neon_e
		blendFunc add
		rgbGen const ( 1 0.35 0.25 )
	}
}

// chain-link fence: alpha-tested, both sides, casts through its alpha
textures/bench/fence
{
	qer_editorimage textures/bench/fence
	qer_pbr_metallicFactor 0.6
	qer_pbr_roughnessFactor 0.5
	surfaceparm alphashadow
	surfaceparm trans
	surfaceparm nonsolid
	cull none
	{
		map textures/bench/fence
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

// window glass: alpha blend; the old baseColorFactor (0.8 0.9 1 0.35) is baked into the image
textures/bench/glass
{
	qer_editorimage textures/bench/glass
	qer_trans 0.5
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.05
	surfaceparm trans
	surfaceparm nolightmap
	{
		map textures/bench/glass
		blendFunc blend
	}
}

// double-sided opaque sheet (hanging tarp)
textures/bench/tarp
{
	qer_editorimage textures/bench/tarp_c
	qer_pbr_metallicFactor 0
	qer_pbr_roughnessFactor 0.9
	cull none
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/bench/tarp_c
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// the sun is the map's light_sun; stock draws pak0's dark night farbox
textures/bench/sky
{
	qer_editorimage textures/bench/sky
	surfaceparm sky
	surfaceparm noimpact
	surfaceparm nolightmap
	skyparms env/xnight2 - -
}

textures/bench/fog
{
	qer_editorimage textures/bench/fog
	qer_trans 0.35
	surfaceparm trans
	surfaceparm nonsolid
	surfaceparm fog
	surfaceparm nolightmap
	fogparms ( 0.45 0.45 0.5 ) 1024
}

textures/bench/caulk
{
	qer_editorimage textures/bench/caulk
	surfaceparm nodraw
	surfaceparm nolightmap
	surfaceparm nomarks
}

textures/bench/clip
{
	qer_editorimage textures/bench/clip
	qer_trans 0.4
	surfaceparm nodraw
	surfaceparm nolightmap
	surfaceparm nonsolid
	surfaceparm trans
	surfaceparm nomarks
	surfaceparm noimpact
	surfaceparm playerclip
}

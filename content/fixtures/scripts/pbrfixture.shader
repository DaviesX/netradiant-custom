// Parser test shaders for the q3-shader-pbr-materials change: one shader per spec scenario.
// This mod never ships and the stock check never packs it. The rend2 fixture is deliberately
// invalid for renderergl1.

// base colour: $lightmap stage first, texture multiplied onto it
textures/fixture/lmfirst
{
	qer_editorimage textures/fixture/fx_base
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/fixture/fx_base
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
}

// base colour: texture stage first, lightmap filtered onto it
textures/fixture/texfirst
{
	qer_editorimage textures/fixture/fx_base
	{
		map textures/fixture/fx_base
		rgbGen identity
	}
	{
		map $lightmap
		blendFunc filter
		rgbGen identity
	}
}

// classification from a single keyword
textures/fixture/normalonly
{
	qer_editorimage textures/fixture/fx_base
	qer_pbr_normal textures/fixture/fx_nrm
	{
		map textures/fixture/fx_base
		rgbGen identity
	}
	{
		map $lightmap
		blendFunc filter
	}
}

// keywords read: normal map and roughness factor
textures/fixture/keywords
{
	qer_editorimage textures/fixture/fx_base
	qer_pbr_normal textures/fixture/fx_nrm
	qer_pbr_roughnessFactor 0.8
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_base
		blendFunc filter
	}
}

// three-component base colour factor
textures/fixture/basefactor
{
	qer_editorimage textures/fixture/fx_base
	qer_pbr_baseColorFactor 0.5 0.5 0.5
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_base
		blendFunc filter
	}
}

// INVALID for renderergl1: a rend2 normal-map stage. The editor warns once and ignores it.
textures/fixture/rend2stage
{
	qer_editorimage textures/fixture/fx_base
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_base
		blendFunc filter
	}
	{
		stage normalMap
		map textures/fixture/fx_nrm
	}
}

// emissive: additive stage with rgbGen const
textures/fixture/glow
{
	qer_editorimage textures/fixture/fx_glow
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_base
		blendFunc filter
	}
	{
		map textures/fixture/fx_glow
		blendFunc add
		rgbGen const ( 1 0.4 0.3 )
	}
}

// emissive: uniform white glow at strength 1
textures/fixture/glowwhite
{
	qer_editorimage textures/fixture/fx_white
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_base
		blendFunc filter
	}
	{
		map textures/fixture/fx_white
		blendFunc add
	}
}

// emissive strength keyword: four times the glow above
textures/fixture/glowx4
{
	qer_editorimage textures/fixture/fx_white
	qer_pbr_emissiveStrength 4
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_base
		blendFunc filter
	}
	{
		map textures/fixture/fx_white
		blendFunc add
	}
}

// an environment-mapped additive stage is not emission
textures/fixture/envadd
{
	qer_editorimage textures/fixture/fx_env
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_base
		blendFunc filter
	}
	{
		map textures/fixture/fx_env
		blendFunc add
		tcGen environment
	}
}

// defaults: metallic factor alone makes a fully rough metal
textures/fixture/metalfactor
{
	qer_editorimage textures/fixture/fx_base
	qer_pbr_metallicFactor 1
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_base
		blendFunc filter
	}
}

// defaults: roughness factor alone makes a dielectric with that roughness
textures/fixture/roughfactor
{
	qer_editorimage textures/fixture/fx_base
	qer_pbr_roughnessFactor 0.4
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_base
		blendFunc filter
	}
}

// defaults: a map with factors multiplies its channels
textures/fixture/mrmap
{
	qer_editorimage textures/fixture/fx_base
	qer_pbr_metallicRoughness textures/fixture/fx_mr
	qer_pbr_metallicFactor 0.5
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_base
		blendFunc filter
	}
}

// alpha-tested fence, both sides, casting through its alpha
textures/fixture/fence
{
	qer_editorimage textures/fixture/fx_alpha
	surfaceparm trans
	surfaceparm alphashadow
	cull none
	{
		map textures/fixture/fx_alpha
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

// less-than alpha test
textures/fixture/lt128
{
	qer_editorimage textures/fixture/fx_alpha
	{
		map textures/fixture/fx_alpha
		alphaFunc LT128
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

// not lightmapped: additive glow
textures/fixture/nolmadd
{
	qer_editorimage textures/fixture/fx_glow
	surfaceparm nolightmap
	surfaceparm trans
	{
		map textures/fixture/fx_glow
		blendFunc add
	}
}

// blended glass
textures/fixture/glass
{
	qer_editorimage textures/fixture/fx_glass
	qer_trans 0.4
	surfaceparm trans
	{
		map textures/fixture/fx_glass
		blendFunc blend
	}
}

// a sky with a qer_pbr_ keyword is still a sky
textures/fixture/skypbr
{
	qer_editorimage textures/fixture/fx_env
	qer_pbr_roughnessFactor 0.5
	surfaceparm sky
	surfaceparm noimpact
	surfaceparm nolightmap
	skyparms env/xnight2 - -
}

// a scripted shader with no stages
textures/fixture/stageless
{
	qer_editorimage textures/fixture/fx_base
	surfaceparm nomarks
}

// solid caulk
textures/fixture/caulk
{
	qer_editorimage textures/fixture/fx_white
	surfaceparm nodraw
	surfaceparm nomarks
	surfaceparm nolightmap
}

// malformed factor: keeps its default and warns
textures/fixture/malformed
{
	qer_editorimage textures/fixture/fx_base
	qer_pbr_roughnessFactor
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_base
		blendFunc filter
	}
}

// a sky declared only by surfaceparm, otherwise lightmapped and opaque: surfaceparm sky alone keeps it unlit and
// out of the caster set
textures/fixture/skystage
{
	qer_editorimage textures/fixture/fx_env
	surfaceparm sky
	surfaceparm noimpact
	{
		map $lightmap
	}
	{
		map textures/fixture/fx_env
		blendFunc filter
	}
}

// alpha-tested but blended by qer_trans, so not lit; still casts through its alpha test
textures/fixture/alphatrans
{
	qer_editorimage textures/fixture/fx_alpha
	qer_trans 0.5
	cull none
	{
		map textures/fixture/fx_alpha
		alphaFunc GE128
		depthWrite
	}
	{
		map $lightmap
		blendFunc filter
		depthFunc equal
	}
}

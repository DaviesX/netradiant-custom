// q3map2 compile shim for the benchmark map.
// The editor and the baker read materials/bench.mtr; q3map2 does not read .mtr files in Stage 1,
// so the surfaceparms that affect BSP/VIS (sky, fog, nodraw, clip) are mirrored here.
// Keep the names and surfaceparms in sync with materials/bench.mtr.

textures/bench/sky
{
	qer_editorimage textures/bench/sky.tga
	surfaceparm sky
	surfaceparm noimpact
	surfaceparm nolightmap
	q3map_sun 1 0.95 0.85 120 30 50
	skyparms - 512 -
}

textures/bench/fog
{
	qer_editorimage textures/bench/fog.tga
	qer_trans 0.35
	surfaceparm trans
	surfaceparm nonsolid
	surfaceparm fog
	surfaceparm nolightmap
	fogparms ( 0.45 0.45 0.5 ) 1024
}

textures/bench/caulk
{
	qer_editorimage textures/bench/caulk.tga
	surfaceparm nodraw
	surfaceparm nolightmap
	surfaceparm nomarks
}

textures/bench/clip
{
	qer_editorimage textures/bench/clip.tga
	qer_trans 0.4
	surfaceparm nodraw
	surfaceparm nolightmap
	surfaceparm nonsolid
	surfaceparm trans
	surfaceparm nomarks
	surfaceparm noimpact
	surfaceparm playerclip
}

textures/bench/fence
{
	qer_editorimage textures/bench/fence.tga
	surfaceparm alphashadow
	surfaceparm nonsolid
	surfaceparm trans
	cull none
	{
		map textures/bench/fence.tga
		alphaFunc GE128
	}
}

textures/bench/glass
{
	qer_editorimage textures/bench/glass.tga
	qer_trans 0.5
	surfaceparm trans
	{
		map textures/bench/glass.tga
		blendFunc blend
	}
}

/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
/* models.c -- model loading and caching */

#include "gl_local.h"

void		GL_FindPolyCenters(msurface_t * surf);

model_t        *loadmodel;
int		modfilelen;

void		Mod_LoadSpriteModel(model_t * mod, void *buffer);
void		Mod_LoadBrushModel(model_t * mod, void *buffer);
void		Mod_LoadAliasModel(model_t * mod, void *buffer);

/* Harven++ MD3 */
void		Mod_LoadAliasMD3Model(model_t * mod, void *buffer);

/* Harven-- MD3 */
model_t        *Mod_LoadModel(model_t * mod, qboolean crash);

byte		mod_novis [MAX_MAP_LEAFS / 8];

#define	MAX_MOD_KNOWN	512
model_t		mod_known[MAX_MOD_KNOWN];
int		mod_numknown;

/* the inline * models from the current map are kept seperate */
model_t		mod_inline[MAX_MOD_KNOWN];

int		registration_sequence;

qboolean inlava  = false;
qboolean inslime = false;
qboolean inwater = false;

/*
 * =============== Mod_PointInLeaf ===============
 */
mleaf_t        *
Mod_PointInLeaf(vec3_t p, model_t * model)
{
	mnode_t        *node;
	float		d;
	cplane_t       *plane;

	if (!model || !model->nodes)
		ri.Sys_Error(ERR_DROP, "Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1) {
		if (node->contents != -1)
			return (mleaf_t *) node;
		plane = node->plane;
		d = DotProduct(p, plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}

	return NULL;		/* never reached */
}


/*
 * =================== Mod_DecompressVis ===================
 */
byte           *
Mod_DecompressVis(byte * in, model_t * model)
{
	static byte	decompressed[MAX_MAP_LEAFS / 8];
	int		c;
	byte           *out;
	int		row;

	row = (model->vis->numclusters + 7) >> 3;
	out = decompressed;

	if (!in) {		/* no vis info, so make all visible */
		while (row) {
			*out++ = 0xff;
			row--;
		}
		return decompressed;
	}
	do {
		if (*in) {
			*out++ = *in++;
			continue;
		}
		c = in[1];
		in += 2;
		while (c) {
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);

	return decompressed;
}

/*
 * ============== Mod_ClusterPVS ==============
 */
byte           *
Mod_ClusterPVS(int cluster, model_t * model)
{
	if (cluster == -1 || !model->vis)
		return mod_novis;
	return Mod_DecompressVis((byte *) model->vis + model->vis->bitofs[cluster][DVIS_PVS],
	    model);
}


/*
 * ===========================================================================
 * ====
 */

/*
 * ================ Mod_Modellist_f ================
 */
void
Mod_Modellist_f(void)
{
	int		i;
	model_t        *mod;
	int		total;

	total = 0;
	ri.Con_Printf(PRINT_ALL, "Loaded models:\n");
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++) {
		if (!mod->name[0])
			continue;
		ri.Con_Printf(PRINT_ALL, "%8i : %s\n", mod->extradatasize, mod->name);
		total += mod->extradatasize;
	}
	ri.Con_Printf(PRINT_ALL, "Total resident: %i\n", total);
}

/*
 * =============== Mod_Init ===============
 */
void
Mod_Init(void)
{
	memset(mod_novis, 0xff, sizeof(mod_novis));
	GL_ClearDecals();	/* Decals  */
}



/*
 * ================== Mod_ForName
 *
 * Loads in a model for the given name ==================
 */

char           *
Get_ModelName(struct model_s *mod)
{
	return mod->name;
}

model_t        *
Mod_ForName(char *name, qboolean crash)
{
	model_t        *mod;
	unsigned       *buf;
	int		i;

	if (!name[0])
		ri.Sys_Error(ERR_DROP, "Mod_ForName: NULL name");

	/* inline models are grabbed only from worldmodel */
	if (name[0] == '*') {
		i = atoi(name + 1);
		if (i < 1 || !r_worldmodel || i >= r_worldmodel->numsubmodels)
			ri.Sys_Error(ERR_DROP, "bad inline model number");
		return &mod_inline[i];
	}
	/* search the currently loaded models */
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++) {
		if (!mod->name[0])
			continue;
		if (!strcmp(mod->name, name))
			return mod;
	}

	/* find a free model slot spot */
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++) {
		if (!mod->name[0])
			break;	/* free spot */
	}
	if (i == mod_numknown) {
		if (mod_numknown == MAX_MOD_KNOWN)
			ri.Sys_Error(ERR_DROP, "mod_numknown == MAX_MOD_KNOWN");
		mod_numknown++;
	}
	Q_strncpyz(mod->name, name, sizeof(mod->name));

	/* load the file */
	modfilelen = ri.FS_LoadFile(mod->name, (void **)&buf);
	if (!buf) {
		if (crash)
			ri.Sys_Error(ERR_DROP, "Mod_NumForName: %s not found", mod->name);
		memset(mod->name, 0, sizeof(mod->name));
		return NULL;
	}
	loadmodel = mod;

	/* fill it in */


	/* call the apropriate loader */

	    switch (LittleLong(*(unsigned *)buf)) {
	case IDALIASHEADER:
		loadmodel->extradata = Hunk_Begin(0x200000);
		Mod_LoadAliasModel(mod, buf);
		break;

		/* Harven++ MD3 */
	case IDMD3HEADER:
		loadmodel->extradata = Hunk_Begin(0x800000);
		Mod_LoadAliasMD3Model(mod, buf);
		break;
		/* Harven-- MD3 */

	case IDSPRITEHEADER:
		loadmodel->extradata = Hunk_Begin(0x10000);
		Mod_LoadSpriteModel(mod, buf);
		break;

	case IDBSPHEADER:
		if (cl_big_maps->value) {
			loadmodel->extradata = Hunk_Begin(0x2000000);
		} else {
			loadmodel->extradata = Hunk_Begin(0x1000000);
		}
		Mod_LoadBrushModel(mod, buf);
		break;

	default:
		ri.Sys_Error(ERR_DROP, "Mod_NumForName: unknown fileid for %s", mod->name);
		break;
	}

	loadmodel->extradatasize = Hunk_End();

	ri.FS_FreeFile(buf);

	return mod;
}

/*
 *
 * ============================================================================
 * ===
 *
 * BRUSHMODEL LOADING
 *
 * ==================
 * ============================================================
 */

byte           *mod_base;


/*
 * ================= Mod_LoadLighting =================
 */
void
Mod_LoadLighting(lump_t * l)
{
	if (!l->filelen) {
		loadmodel->lightdata = NULL;
		return;
	}
	loadmodel->lightdata = Hunk_Alloc(l->filelen);
	memcpy(loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
	loadmodel->staindata = Hunk_Alloc(l->filelen);
	memcpy(loadmodel->staindata, mod_base + l->fileofs, l->filelen);
}


/*
 * ================= Mod_LoadVisibility =================
 */
void
Mod_LoadVisibility(lump_t * l)
{
	int		i;

	if (!l->filelen) {
		loadmodel->vis = NULL;
		return;
	}
	loadmodel->vis = Hunk_Alloc(l->filelen);
	memcpy(loadmodel->vis, mod_base + l->fileofs, l->filelen);

	loadmodel->vis->numclusters = LittleLong(loadmodel->vis->numclusters);
	for (i = 0; i < loadmodel->vis->numclusters; i++) {
		loadmodel->vis->bitofs[i][0] = LittleLong(loadmodel->vis->bitofs[i][0]);
		loadmodel->vis->bitofs[i][1] = LittleLong(loadmodel->vis->bitofs[i][1]);
	}
}


/*
 * ================= Mod_LoadVertexes =================
 */
void
Mod_LoadVertexes(lump_t * l)
{
	dvertex_t      *in;
	mvertex_t      *out;
	int		i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for (i = 0; i < count; i++, in++, out++) {
		out->position[0] = LittleFloat(in->point[0]);
		out->position[1] = LittleFloat(in->point[1]);
		out->position[2] = LittleFloat(in->point[2]);
	}
}

/*
 * ================= RadiusFromBounds =================
 */
float
RadiusFromBounds(vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t		corner;

	for (i = 0; i < 3; i++) {
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return VectorLength(corner);
}


/*
 * ================= Mod_LoadSubmodels =================
 */
void
Mod_LoadSubmodels(lump_t * l)
{
	dmodel_t       *in;
	mmodel_t       *out;
	int		i, j, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for (i = 0; i < count; i++, in++, out++) {
		for (j = 0; j < 3; j++) {	/* spread the mins / maxs by
						 * a pixel */
			out->mins[j] = LittleFloat(in->mins[j]) - 1;
			out->maxs[j] = LittleFloat(in->maxs[j]) + 1;
			out->origin[j] = LittleFloat(in->origin[j]);
		}
		out->radius = RadiusFromBounds(out->mins, out->maxs);
		out->headnode = LittleLong(in->headnode);
		out->firstface = LittleLong(in->firstface);
		out->numfaces = LittleLong(in->numfaces);
	}
}

/*
 * ================= Mod_LoadEdges =================
 */
void
Mod_LoadEdges(lump_t * l)
{
	dedge_t        *in;
	medge_t        *out;
	int		i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc((count + 1) * sizeof(*out));

	loadmodel->edges = out;
	loadmodel->numedges = count;

	for (i = 0; i < count; i++, in++, out++) {
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

/*
 * ================= Mod_LoadTexinfo =================
 */
void
Mod_LoadTexinfo(lump_t * l)
{
	texinfo_t      *in;
	mtexinfo_t     *out, *step;
	int		i, j, count;
	char		name[MAX_QPATH];
	int		next;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for (i = 0; i < count; i++, in++, out++) {
		for (j = 0; j < 8; j++)
			out->vecs[0][j] = LittleFloat(in->vecs[0][j]);

		out->value = LittleLong(in->value);
		out->flags = LittleLong(in->flags);
		next = LittleLong(in->nexttexinfo);
		if (next > 0)
			out->next = loadmodel->texinfo + next;
		else
			out->next = NULL;

		Com_sprintf(name, sizeof(name), "textures/%s.tga", in->texture);
		out->image = GL_FindImage(name, it_wall);

		if (!out->image) {
			Com_sprintf(name, sizeof(name), "textures/%s.wal", in->texture);
			out->image = GL_FindImage(name, it_wall);
		}		
		if (!out->image) {
			ri.Con_Printf(PRINT_ALL, "Couldn't load %s\n", name);
			out->image = r_notexture;
		}
	}

	/* count animation frames */
	for (i = 0; i < count; i++) {
		out = &loadmodel->texinfo[i];
		out->numframes = 1;
		for (step = out->next; step && step != out; step = step->next)
			out->numframes++;
	}
}

/*
 * ================ CalcSurfaceExtents
 *
 * Fills in s->texturemins[] and s->extents[] ================
 */
void
CalcSurfaceExtents(msurface_t * s)
{
	float		mins[2], maxs[2], val;
	int		i, j, e;
	mvertex_t      *v;
	mtexinfo_t     *tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;

	for (i = 0; i < s->numedges; i++) {
		e = loadmodel->surfedges[s->firstedge + i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		for (j = 0; j < 2; j++) {
			val = v->position[0] * tex->vecs[j][0] +
			    v->position[1] * tex->vecs[j][1] +
			    v->position[2] * tex->vecs[j][2] +
			    tex->vecs[j][3];
			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i = 0; i < 2; i++) {
		bmins[i] = floor(mins[i] / 16);
		bmaxs[i] = ceil(maxs[i] / 16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

#if 0
		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 512 /* 256 */ )
			ri.Sys_Error (ERR_DROP, "Bad surface extents");
#endif
	}
}


void		GL_BuildPolygonFromSurface(msurface_t * fa);
void		GL_CreateSurfaceLightmap(msurface_t * surf);
void		GL_EndBuildingLightmaps(void);
void		GL_BeginBuildingLightmaps(model_t * m);

/*
 * ================
 * GL_cleanupLightsArray
 *
 * Move all the "holes" (empty lights) in the list to the end.
 * ================
 */
void
GL_cleaupLightsArray(void)
{
	int		i;
	int		j;

	for (i = 0; i < numberOfWallLights; i++) {
		if (wallLightArray[i] != NULL)
			continue;

		for (j = i + 1; j < numberOfWallLights; j++) {
			if (wallLightArray[j] == NULL)
				continue;
			wallLightArray[i] = wallLightArray[j];
			wallLightArray[j] = NULL;
			break;
		}
	}

	/* Count number of lights left. */
	for (i = 0; i < numberOfWallLights; i++)
		if (wallLightArray[i] == NULL)
			break;

	ri.Con_Printf(PRINT_DEVELOPER, "Number of wall lights: %d.\n",
	    numberOfWallLights);

	numberOfWallLights = i;

	ri.Con_Printf(PRINT_DEVELOPER,
	    "Number of wall lights: %d (after clean-up).\n",
	    numberOfWallLights);
}

/*
 * ================
 * GL_mergeCloseLights
 *
 * Basically deletes lights which are too close to each other because they are
 * not needed.
 * ================
 */
void
GL_mergeCloseLights(void)
{
	int		i, j;
	float		distanceSquared;
	vec3_t		displacement;

	for (i = 0; i < numberOfWallLights; i++) {
		for (j = 0; j < numberOfWallLights; j++) {
			if (wallLightArray[i] == NULL)
				continue;
			if (wallLightArray[j] == NULL)
				continue;

			/* Otherwise we are gonna delete all lights. */
			if (i == j)
				continue;

			VectorSubtract(wallLightArray[i]->origin, wallLightArray[j]->origin, displacement);

			distanceSquared = displacement[0] * displacement[0];
			distanceSquared += displacement[1] * displacement[1];
			distanceSquared += displacement[2] * displacement[2];

			/*
			 * TODO:
			 * Perhaps only want to merge lights of the same
			 * colour. That would make sense but this will do for
			 * now.
			 */
			if (distanceSquared < 100 * 100)
				wallLightArray[j] = NULL;
		}
	}
	GL_cleaupLightsArray();
}

void
GL_buildDynamicWallLights(msurface_t * surf)
{
	byte           *lightmap;
	int		i;
	int		maps;
	vec3_t		scale;

	if (numberOfWallLights >= MAX_WALL_LIGHTS)
		return;

	surf->wallLight = Hunk_Alloc(sizeof(wallLight_t));

	VectorCopy(surf->center, surf->wallLight->origin);
	VectorClear(surf->wallLight->color);
	VectorNegate(surf->plane->normal, surf->wallLight->normal);

	lightmap = surf->samples;	/* Time to get lights color. */

	if (lightmap) {
		for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++) {
			for (i = 0; i < 3; i++) {
				scale[i] = gl_modulate->value * r_refdef.lightstyles[surf->styles[maps]].rgb[i];
				surf->wallLight->color[i] += lightmap[i] * scale[i] * (1.0 / 255);
			}
			lightmap += 3 * ((surf->extents[0] >> 4) + 1) * ((surf->extents[1] >> 4) + 1);
		}
	} else
		VectorSet(surf->wallLight->color, 1.0, 1.0, 1.0);

	wallLightArray[numberOfWallLights++] = surf->wallLight;
}

/*
 * ================= Mod_LoadFaces =================
 */
void
Mod_LoadFaces(lump_t * l)
{
	dface_t        *in;
	msurface_t     *out;
	int		i, count, surfnum;
	int		planenum, side;
	int		ti;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	currentmodel = loadmodel;

	GL_BeginBuildingLightmaps(loadmodel);

	/* Clear flares and wall lights. */
	r_numflares = 0;
	numberOfWallLights = 0;

	for (surfnum = 0; surfnum < count; surfnum++, in++, out++) {
		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);
		out->flags = 0;
		out->polys = NULL;

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);
		if (side)
			out->flags |= SURF_PLANEBACK;

		out->plane = loadmodel->planes + planenum;

		ti = LittleShort(in->texinfo);
		if (ti < 0 || ti >= loadmodel->numtexinfo)
			ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: bad texinfo number");
		out->texinfo = loadmodel->texinfo + ti;

		CalcSurfaceExtents(out);

		/* Lighting info. */
		for (i = 0; i < MAXLIGHTMAPS; i++)
			out->styles[i] = in->styles[i];
		i = LittleLong(in->lightofs);
		if (i == -1) {
			out->samples = NULL;
			out->stain_samples = NULL;
		} else {
			out->samples = loadmodel->lightdata + i;
			out->stain_samples = loadmodel->staindata + i;
		}

		/* Set the drawing flags. */
		if (out->texinfo->flags & SURF_WARP) {
			out->flags |= SURF_DRAWTURB;
			for (i = 0; i < 2; i++) {
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}
			/* Cut up polygon for warps. */
			GL_SubdivideSurface(out);
		}

		/* Create lightmaps and polygons. */
		if (!(out->texinfo->flags & SURF_WARP)) {
			if (!(out->texinfo->flags & (SURF_SKY | SURF_TRANS33 |
			    SURF_TRANS66)))
				GL_CreateSurfaceLightmap(out);

			GL_BuildPolygonFromSurface(out);
			GL_FindPolyCenters(out);

			if (!(out->texinfo->flags & (SURF_SKY | SURF_TRANS33 |
			    SURF_TRANS66)) &&
			    (out->texinfo->flags & SURF_LIGHT)) {
				GL_buildDynamicWallLights(out);
				GL_AddFlareSurface(out);
			}
		}
	}

	GL_mergeCloseLights();
	GL_EndBuildingLightmaps();
}


/*
 * ================= Mod_SetParent =================
 */
void
Mod_SetParent(mnode_t * node, mnode_t * parent)
{
	node->parent = parent;
	if (node->contents != -1)
		return;
	Mod_SetParent(node->children[0], node);
	Mod_SetParent(node->children[1], node);
}

/*
 * ================= Mod_LoadNodes =================
 */
void
Mod_LoadNodes(lump_t * l)
{
	int		i, j, count, p;
	dnode_t        *in;
	mnode_t        *out;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i = 0; i < count; i++, in++, out++) {
		for (j = 0; j < 3; j++) {
			out->minmaxs[j] = LittleShort(in->mins[j]);
			out->minmaxs[3 + j] = LittleShort(in->maxs[j]);
		}

		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleShort(in->firstface);
		out->numsurfaces = LittleShort(in->numfaces);
		out->contents = -1;	/* differentiate from leafs */

		for (j = 0; j < 2; j++) {
			p = LittleLong(in->children[j]);
			if (p >= 0)
				out->children[j] = loadmodel->nodes + p;
			else
				out->children[j] = (mnode_t *) (loadmodel->leafs + (-1 - p));
		}
	}

	Mod_SetParent(loadmodel->nodes, NULL);	/* sets nodes and leafs */
}

/*
 * ================= Mod_LoadLeafs =================
 */
void
Mod_LoadLeafs(lump_t * l)
{
	dleaf_t        *in;
	mleaf_t        *out;
	int		i, j, count, p;
	/* glpoly_t	*poly; */
	
	inlava  = false;
	inslime = false;
	inwater = false;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i = 0; i < count; i++, in++, out++) {
		for (j = 0; j < 3; j++) {
			out->minmaxs[j] = LittleShort(in->mins[j]);
			out->minmaxs[3 + j] = LittleShort(in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->cluster = LittleShort(in->cluster);
		out->area = LittleShort(in->area);

		out->firstmarksurface = loadmodel->marksurfaces +
		    LittleShort(in->firstleafface);
		out->nummarksurfaces = LittleShort(in->numleaffaces);

		/* gl underwater warp */
#if 0
		if (out->contents & (CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA)) {
			for (j = 0; j < out->nummarksurfaces; j++) {
				if ((out->firstmarksurface[j]->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)))
					continue;

				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
			}
		}
#else	
		if (out->contents & (CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA)) {
			for (j = 0; j < out->nummarksurfaces; j++) {
				if ((out->firstmarksurface[j]->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)))
					continue;
					
				/* qglDisable(GL_FOG); */
				
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
					
				if (out->contents & CONTENTS_LAVA) {
					out->firstmarksurface[j]->flags |= SURF_LAVA;
					inlava = true;
				}
				if (out->contents & CONTENTS_SLIME) {
					out->firstmarksurface[j]->flags |= SURF_SLIME;
					inslime = true;
				}
				if (out->contents & CONTENTS_WATER) {
					out->firstmarksurface[j]->flags |= SURF_WATER;
					inwater = true;
                    		}
				
				/* qglEnable(GL_FOG); */
			}
		}
#endif
	}
}

/*
 * ================= Mod_LoadMarksurfaces =================
 */
void
Mod_LoadMarksurfaces(lump_t * l)
{
	int		i, j, count;
	short          *in;
	msurface_t    **out;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for (i = 0; i < count; i++) {
		j = LittleShort(in[i]);
		if (j < 0 || j >= loadmodel->numsurfaces)
			ri.Sys_Error(ERR_DROP, "Mod_ParseMarksurfaces: bad surface number");
		out[i] = loadmodel->surfaces + j;
	}
}

/*
 * ================= Mod_LoadSurfedges =================
 */
void
Mod_LoadSurfedges(lump_t * l)
{
	int		i, count;
	int            *in, *out;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
	count = l->filelen / sizeof(*in);
	if (count < 1 || count >= MAX_MAP_SURFEDGES)
		ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: bad surfedges count in %s: %i",
		    loadmodel->name, count);

	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for (i = 0; i < count; i++)
		out[i] = LittleLong(in[i]);
}


/*
 * ================= Mod_LoadPlanes =================
 */
void
Mod_LoadPlanes(lump_t * l)
{
	int		i, j;
	cplane_t       *out;
	dplane_t       *in;
	int		count;
	int		bits;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error(ERR_DROP, "MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * 2 * sizeof(*out));

	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for (i = 0; i < count; i++, in++, out++) {
		bits = 0;
		for (j = 0; j < 3; j++) {
			out->normal[j] = LittleFloat(in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1 << j;
		}

		out->dist = LittleFloat(in->dist);
		out->type = LittleLong(in->type);
		out->signbits = bits;
	}
}

/*
 * ================= Mod_LoadBrushModel =================
 */
void
Mod_LoadBrushModel(model_t * mod, void *buffer)
{
	int		i;
	dheader_t      *header;
	mmodel_t       *bm;

	loadmodel->type = mod_brush;
	if (loadmodel != mod_known)
		ri.Sys_Error(ERR_DROP, "Loaded a brush model after the world");

	header = (dheader_t *) buffer;

	i = LittleLong(header->version);
	if (i != BSPVERSION)
		ri.Sys_Error(ERR_DROP, "Mod_LoadBrushModel: %s has wrong version number (%i should be %i)", mod->name, i, BSPVERSION);

	/* swap all the lumps */
	mod_base = (byte *) header;

	for (i = 0; i < sizeof(dheader_t) / 4; i++)
		((int *)header)[i] = LittleLong(((int *)header)[i]);

	/* load into heap */

	Mod_LoadVertexes(&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges(&header->lumps[LUMP_EDGES]);
	Mod_LoadSurfedges(&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadLighting(&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes(&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo(&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces(&header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces(&header->lumps[LUMP_LEAFFACES]);
	Mod_LoadVisibility(&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs(&header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes(&header->lumps[LUMP_NODES]);
	Mod_LoadSubmodels(&header->lumps[LUMP_MODELS]);
	mod->numframes = 2;	/* regular and alternate animation */


	/* set up the submodels */
	for (i = 0; i < mod->numsubmodels; i++) {
		model_t        *starmod;

		bm = &mod->submodels[i];
		starmod = &mod_inline[i];

		*starmod = *loadmodel;

		starmod->firstmodelsurface = bm->firstface;
		starmod->nummodelsurfaces = bm->numfaces;
		starmod->firstnode = bm->headnode;
		if (starmod->firstnode >= loadmodel->numnodes)
			ri.Sys_Error(ERR_DROP, "Inline model %i has bad firstnode", i);

		VectorCopy(bm->maxs, starmod->maxs);
		VectorCopy(bm->mins, starmod->mins);
		starmod->radius = bm->radius;

		if (i == 0)
			*loadmodel = *starmod;

		starmod->numleafs = bm->visleafs;
	}
}

/*
 *
 * ============================================================================
 * ==
 *
 * ALIAS MODELS
 *
 * ============
 * =================================================================
 */

/*
 * ================= Mod_LoadAliasModel =================
 */
void
Mod_LoadAliasModel(model_t * mod, void *buffer)
{
	int		i, j;
	dmdl_t         *pinmodel, *pheader;
	dstvert_t      *pinst, *poutst;
	dtriangle_t    *pintri, *pouttri;
	daliasframe_t  *pinframe, *poutframe;
	int            *pincmd, *poutcmd;
	int		version;

	pinmodel = (dmdl_t *) buffer;

	version = LittleLong(pinmodel->version);
	if (version != ALIAS_VERSION)
		ri.Sys_Error(ERR_DROP, "%s has wrong version number (%i should be %i)",
		    mod->name, version, ALIAS_VERSION);

	pheader = Hunk_Alloc(LittleLong(pinmodel->ofs_end));

	/* byte swap the header fields and sanity check */
	for (i = 0; i < sizeof(dmdl_t) / 4; i++)
		((int *)pheader)[i] = LittleLong(((int *)buffer)[i]);

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		ri.Sys_Error(ERR_DROP, "model %s has a skin taller than %d", mod->name,
		    MAX_LBM_HEIGHT);

	if (pheader->num_xyz <= 0)
		ri.Sys_Error(ERR_DROP, "model %s has no vertices", mod->name);

	if (pheader->num_xyz > MAX_VERTS)
		ri.Sys_Error(ERR_DROP, "model %s has too many vertices", mod->name);

	if (pheader->num_st <= 0)
		ri.Sys_Error(ERR_DROP, "model %s has no st vertices", mod->name);

	if (pheader->num_tris <= 0)
		ri.Sys_Error(ERR_DROP, "model %s has no triangles", mod->name);

	if (pheader->num_frames <= 0)
		ri.Sys_Error(ERR_DROP, "model %s has no frames", mod->name);

	/* load base s and t vertices (not used in gl version) */
	pinst = (dstvert_t *) ((byte *) pinmodel + pheader->ofs_st);
	poutst = (dstvert_t *) ((byte *) pheader + pheader->ofs_st);

	for (i = 0; i < pheader->num_st; i++) {
		poutst[i].s = LittleShort(pinst[i].s);
		poutst[i].t = LittleShort(pinst[i].t);
	}

	/* load triangle lists */
	pintri = (dtriangle_t *) ((byte *) pinmodel + pheader->ofs_tris);
	pouttri = (dtriangle_t *) ((byte *) pheader + pheader->ofs_tris);

	for (i = 0; i < pheader->num_tris; i++) {
		for (j = 0; j < 3; j++) {
			pouttri[i].index_xyz[j] = LittleShort(pintri[i].index_xyz[j]);
			pouttri[i].index_st[j] = LittleShort(pintri[i].index_st[j]);
		}
	}

	/* load the frames */
	for (i = 0; i < pheader->num_frames; i++) {
		pinframe = (daliasframe_t *) ((byte *) pinmodel
		    + pheader->ofs_frames + i * pheader->framesize);
		poutframe = (daliasframe_t *) ((byte *) pheader
		    + pheader->ofs_frames + i * pheader->framesize);

		memcpy(poutframe->name, pinframe->name, sizeof(poutframe->name));
		for (j = 0; j < 3; j++) {
			poutframe->scale[j] = LittleFloat(pinframe->scale[j]);
			poutframe->translate[j] = LittleFloat(pinframe->translate[j]);
		}
		/* verts are all 8 bit, so no swapping needed */
		memcpy(poutframe->verts, pinframe->verts,
		    pheader->num_xyz * sizeof(dtrivertx_t));

	}

	mod->type = mod_alias;

	/* load the glcmds */
	pincmd = (int *)((byte *) pinmodel + pheader->ofs_glcmds);
	poutcmd = (int *)((byte *) pheader + pheader->ofs_glcmds);
	for (i = 0; i < pheader->num_glcmds; i++)
		poutcmd[i] = LittleLong(pincmd[i]);


	/* register all skins */
	memcpy((char *)pheader + pheader->ofs_skins, (char *)pinmodel + pheader->ofs_skins,
	    pheader->num_skins * MAX_SKINNAME);
	for (i = 0; i < pheader->num_skins; i++) {
		mod->skins[i] = GL_FindImage((char *)pheader + pheader->ofs_skins + i * MAX_SKINNAME
		    ,it_skin);
	}

	mod->mins[0] = -32;
	mod->mins[1] = -32;
	mod->mins[2] = -32;
	mod->maxs[0] = 32;
	mod->maxs[1] = 32;
	mod->maxs[2] = 32;
}

/*
 *
 * ============================================================================
 * ==
 *
 * SPRITE MODELS
 *
 * =============
 * ================================================================
 */

/*
 * ================= Mod_LoadSpriteModel =================
 */
void
Mod_LoadSpriteModel(model_t * mod, void *buffer)
{
	dsprite_t      *sprin, *sprout;
	int		i;

	sprin = (dsprite_t *) buffer;
	sprout = Hunk_Alloc(modfilelen);

	sprout->ident = LittleLong(sprin->ident);
	sprout->version = LittleLong(sprin->version);
	sprout->numframes = LittleLong(sprin->numframes);

	if (sprout->version != SPRITE_VERSION)
		ri.Sys_Error(ERR_DROP, "%s has wrong version number (%i should be %i)",
		    mod->name, sprout->version, SPRITE_VERSION);

	if (sprout->numframes > MAX_MD2SKINS)
		ri.Sys_Error(ERR_DROP, "%s has too many frames (%i > %i)",
		    mod->name, sprout->numframes, MAX_MD2SKINS);

	/* byte swap everything */
	for (i = 0; i < sprout->numframes; i++) {
		sprout->frames[i].width = LittleLong(sprin->frames[i].width);
		sprout->frames[i].height = LittleLong(sprin->frames[i].height);
		sprout->frames[i].origin_x = LittleLong(sprin->frames[i].origin_x);
		sprout->frames[i].origin_y = LittleLong(sprin->frames[i].origin_y);
		memcpy(sprout->frames[i].name, sprin->frames[i].name, MAX_SKINNAME);
		mod->skins[i] = GL_FindImage(sprout->frames[i].name,
		    it_sprite);
	}

	mod->type = mod_sprite;
}

/*
 * ===========================================================================
 * ==
 */

/*
 * @@@@@@@@@@@@@@@@@@@@@ R_BeginRegistration
 *
 * Specifies the model that will be used as the world @@@@@@@@@@@@@@@@@@@@@
 */
extern cvar_t  *gl_anisotropic;	/* Jitspoe */
extern cvar_t  *gl_lightmap_texture_saturation;	/* jitsaturation */
void
R_BeginRegistration(char *model)
{
	char		fullname  [MAX_QPATH];
	cvar_t         *flushmap;

	if (gl_anisotropic->value < 0)
		ri.Cvar_Set("gl_anisotropic", "0");
	if (gl_anisotropic->value > gl_config.max_anisotropy)
		ri.Cvar_SetValue("gl_anisotropic", gl_config.max_anisotropy);

	if (gl_lightmap_texture_saturation->value > 1 || gl_lightmap_texture_saturation->value < 0)
		ri.Cvar_Set("gl_lightmap_texture_saturation", "1");	/* jitsaturation */

	registration_sequence++;
	r_oldviewcluster = -1;	/* force markleafs */

	Com_sprintf(fullname, sizeof(fullname), "maps/%s.bsp", model);
	GL_ClearDecals();	/* Decals  */

	/* explicitly free the old map if different */
	/* this guarantees that mod_known[0] is the world map */
	flushmap = ri.Cvar_Get("flushmap", "0", 0);
	if (strcmp(mod_known[0].name, fullname) || flushmap->value)
		Mod_Free(&mod_known[0]);
	r_worldmodel = Mod_ForName(fullname, true);

	r_viewcluster = -1;
}


/*
 * @@@@@@@@@@@@@@@@@@@@@ R_RegisterModel
 *
 * @@@@@@@@@@@@@@@@@@@@@
 */
struct model_s *
R_RegisterModel(char *name)
{
	model_t        *mod;
	int		i;
	dsprite_t      *sprout;
	dmdl_t         *pheader;

	/* Harven++ MD3 */
	maliasmodel_t  *pheader3;

	/* Knightmare- MD3 autoreplace code */
	int		len = strlen(name);

	if (!strcmp(name + len - 4, ".md2")) {	/* look if we have a .md2 file */
		char		s[128];

		Q_strncpyz(s, name, sizeof(s));
		s[len - 1] = '3';
		mod = R_RegisterModel(s);
		if (mod)
			return mod;
	}
	/* end Knightmare */

	mod = Mod_ForName(name, false);
	if (mod) {
		mod->registration_sequence = registration_sequence;

		/* register any images used by the models */
		if (mod->type == mod_sprite) {
			sprout = (dsprite_t *) mod->extradata;
			for (i = 0; i < sprout->numframes; i++)
				mod->skins[i] = GL_FindImage(sprout->frames[i].name, it_sprite);
		} else if (mod->type == mod_alias) {
			pheader = (dmdl_t *) mod->extradata;
			for (i = 0; i < pheader->num_skins; i++)
				mod->skins[i] = GL_FindImage((char *)pheader + pheader->ofs_skins + i * MAX_SKINNAME, it_skin);
			/* PGM */
			mod->numframes = pheader->num_frames;
			/* PGM */
		}
		/* Harven++ MD3 */
		else if (mod->type == mod_alias_md3) {
			int		k;

			pheader3 = (maliasmodel_t *) mod->extradata;

			for (i = 0; i < pheader3->num_meshes; i++) {
				for (k = 0; k < pheader3->meshes[i].num_skins; k++)
					mod->skins[i] = GL_FindImage(pheader3->meshes[i].skins[k].name, it_skin);
			}
			mod->numframes = pheader3->num_frames;
		}
		/* Harven-- MD3 */
		else if (mod->type == mod_brush) {
			for (i = 0; i < mod->numtexinfo; i++)
				mod->texinfo[i].image->registration_sequence = registration_sequence;
		}
	}
	return mod;
}


/*
 * @@@@@@@@@@@@@@@@@@@@@ R_EndRegistration
 *
 * @@@@@@@@@@@@@@@@@@@@@
 */
void
R_EndRegistration(void)
{
	int		i;
	model_t        *mod;

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++) {
		if (!mod->name[0])
			continue;
		if (mod->registration_sequence != registration_sequence) {	/* don't need this model */
			Mod_Free(mod);
		}
	}

	GL_FreeUnusedImages();
}


/*
 * ===========================================================================
 * ==
 */


/*
 * ================ Mod_Free ================
 */
void
Mod_Free(model_t * mod)
{
	Hunk_Free(mod->extradata);
	memset(mod, 0, sizeof(*mod));
}

/*
 * ================ Mod_FreeAll ================
 */
void
Mod_FreeAll(void)
{
	int		i;

	for (i = 0; i < mod_numknown; i++) {
		if (mod_known[i].extradatasize)
			Mod_Free(&mod_known[i]);
	}
	GL_ClearDecals();	/* Decals  */
}

void
GL_FindPolyCenters(msurface_t * surf)
{
	int		i, j;
	float          *v;
	vec3_t		poly_avg;
	vec3_t		surf_avg;
	glpoly_t       *poly;

	VectorClear(surf_avg);

	for (poly = surf->polys, i = 0; poly; poly = poly->next, i++) {
		VectorClear(poly_avg);

		for (j = 0, v = poly->verts[0]; j < poly->numverts; j++, v += VERTEXSIZE)
			VectorAdd(poly_avg, v, poly_avg);

		VectorScale(poly_avg, 1.0 / (float)poly->numverts, poly->center);
		VectorAdd(poly->center, surf_avg, surf_avg);
	}

	if (i > 0)
		VectorScale(surf_avg, 1.0 / (float)i, surf->center);
	else
		VectorClear(surf->center);
}

// vis.c

#include <windows.h>
#include "vis.h"
#include "threads.h"
#include "stdlib.h"
#include "pacifier.h"
#include "vmpi.h"
#include "mpivis.h"
#include "vstdlib/strtools.h"
#include "ivvisdll.h"
#include "collisionutils.h"
#include "vstdlib/icommandline.h"
#include "vmpi_tools_shared.h"

int			g_numportals;
int			portalclusters;

char		inbase[32];
char		outbase[32];

portal_t	*portals;
leaf_t		*leafs;

int			c_portaltest, c_portalpass, c_portalcheck;

byte		*uncompressedvis;

byte		*vismap, *vismap_p, *vismap_end;	// past visfile
int			originalvismapsize;

int			leafbytes;				// (portalclusters+63)>>3
int			leaflongs;

int			portalbytes, portallongs;

bool		fastvis;
bool		nosort;

int			totalvis;

portal_t	*sorted_portals[MAX_MAP_PORTALS*2];

bool		g_bUseRadius = false;
double		g_VisRadius = 4096.0f * 4096.0f;

bool		g_bLowPriority = false;

int			g_nDXLevel = 90;

//=============================================================================

void PlaneFromWinding (winding_t *w, plane_t *plane)
{
	Vector		v1, v2;

// calc plane
	VectorSubtract (w->points[2], w->points[1], v1);
	VectorSubtract (w->points[0], w->points[1], v2);
	CrossProduct (v2, v1, plane->normal);
	VectorNormalize (plane->normal);
	plane->dist = DotProduct (w->points[0], plane->normal);
}


/*
==================
NewWinding
==================
*/
winding_t *NewWinding (int points)
{
	winding_t	*w;
	int			size;
	
	if (points > MAX_POINTS_ON_WINDING)
		Error ("NewWinding: %i points", points);
	
	size = (int)(&((winding_t *)0)->points[points]);
	w = (winding_t*)malloc (size);
	memset (w, 0, size);
	
	return w;
}

void pw(winding_t *w)
{
	int		i;
	for (i=0 ; i<w->numpoints ; i++)
		Msg ("(%5.1f, %5.1f, %5.1f)\n",w->points[i][0], w->points[i][1],w->points[i][2]);
}

void prl(leaf_t *l)
{
	int			i;
	portal_t	*p;
	plane_t		pl;
	
	for (i=0 ; i<l->numportals ; i++)
	{
		p = l->portals[i];
		pl = p->plane;
		Msg ("portal %4i to leaf %4i : %7.1f : (%4.1f, %4.1f, %4.1f)\n",(int)(p-portals),p->leaf,pl.dist, pl.normal[0], pl.normal[1], pl.normal[2]);
	}
}


//=============================================================================

/*
=============
SortPortals

Sorts the portals from the least complex, so the later ones can reuse
the earlier information.
=============
*/
int PComp (const void *a, const void *b)
{
	if ( (*(portal_t **)a)->nummightsee == (*(portal_t **)b)->nummightsee)
		return 0;
	if ( (*(portal_t **)a)->nummightsee < (*(portal_t **)b)->nummightsee)
		return -1;

	return 1;
}

void SortPortals (void)
{
	int		i;
	
	for (i=0 ; i<g_numportals*2 ; i++)
		sorted_portals[i] = &portals[i];

	if (nosort)
		return;
	qsort (sorted_portals, g_numportals*2, sizeof(sorted_portals[0]), PComp);
}


/*
==============
LeafVectorFromPortalVector
==============
*/
int LeafVectorFromPortalVector (byte *portalbits, byte *leafbits)
{
	int			i;
	portal_t	*p;
	int			c_leafs;


	memset (leafbits, 0, leafbytes);

	for (i=0 ; i<g_numportals*2 ; i++)
	{
		if ( CheckBit( portalbits, i ) )
		{
			p = portals+i;
			SetBit( leafbits, p->leaf );
		}
	}

	c_leafs = CountBits (leafbits, portalclusters);

	return c_leafs;
}


/*
===============
ClusterMerge

Merges the portal visibility for a leaf
===============
*/
void ClusterMerge (int clusternum)
{
	leaf_t		*leaf;
//	byte		portalvector[MAX_PORTALS/8];
	byte		portalvector[MAX_PORTALS/4];      // 4 because portal bytes is * 2
	byte		uncompressed[MAX_MAP_LEAFS/8];
	int			i, j;
	int			numvis;
	portal_t	*p;
	int			pnum;

	// OR together all the portalvis bits

	memset (portalvector, 0, portalbytes);
	leaf = &leafs[clusternum];
	for (i=0 ; i<leaf->numportals ; i++)
	{
		p = leaf->portals[i];
		if (p->status != stat_done)
			Error ("portal not done %d %d %d\n", i, p, portals);
		for (j=0 ; j<portallongs ; j++)
			((long *)portalvector)[j] |= ((long *)p->portalvis)[j];
		pnum = p - portals;
		SetBit( portalvector, pnum );
	}

	// convert portal bits to leaf bits
	numvis = LeafVectorFromPortalVector (portalvector, uncompressed);

	if ( CheckBit( uncompressed, clusternum ) )
		Warning("WARNING: Cluster portals saw into cluster\n");
		
	SetBit( uncompressed, clusternum );
	numvis++;		// count the leaf itself

	// save uncompressed for PHS calculation
	memcpy (uncompressedvis + clusternum*leafbytes, uncompressed, leafbytes);

	qprintf ("cluster %4i : %4i visible\n", clusternum, numvis);
	totalvis += numvis;
}

static int CompressAndCrosscheckClusterVis( int clusternum )
{
	int		optimized = 0;
	byte	compressed[MAX_MAP_LEAFS/8];
//
// compress the bit string
//
	byte *uncompressed = uncompressedvis + clusternum*leafbytes;
	for ( int i = 0; i < portalclusters; i++ )
	{
		if ( i == clusternum )
			continue;

		if ( CheckBit( uncompressed, i ) )
		{
			byte *other = uncompressedvis + i*leafbytes;
			if ( !CheckBit( other, clusternum ) )
			{
				ClearBit( uncompressed, i );
				optimized++;
			}
		}
	}
	int numbytes = CompressVis( uncompressed, compressed );

	byte *dest = vismap_p;
	vismap_p += numbytes;
	
	if (vismap_p > vismap_end)
		Error ("Vismap expansion overflow");

	dvis->bitofs[clusternum][DVIS_PVS] = dest-vismap;

	memcpy( dest, compressed, numbytes );

	return optimized;
}


/*
==================
CalcPortalVis
==================
*/
void CalcPortalVis (void)
{
	int		i;

	// fastvis just uses mightsee for a very loose bound
	if( fastvis )
	{
		for (i=0 ; i<g_numportals*2 ; i++)
		{
			portals[i].portalvis = portals[i].portalflood;
			portals[i].status = stat_done;
		}
		return;
	}


    if (g_bUseMPI) 
	{
 		RunMPIPortalFlow();
	}
	else 
	{
		RunThreadsOnIndividual (g_numportals*2, true, PortalFlow);
	}
}


/*
==================
CalcVis
==================
*/
void CalcVis (void)
{
	int		i;

	if (g_bUseMPI) 
	{
		RunMPIBasePortalVis();
	}
	else 
	{
	    RunThreadsOnIndividual (g_numportals*2, true, BasePortalVis);
	}

	SortPortals ();

	CalcPortalVis ();

	//
	// assemble the leaf vis lists by oring the portal lists
	//
	for ( i = 0; i < portalclusters; i++ )
	{
		ClusterMerge( i );
	}

	int count = 0;
	// Now crosscheck each leaf's vis and compress
	for ( i = 0; i < portalclusters; i++ )
	{
		count += CompressAndCrosscheckClusterVis( i );
	}

		
	Msg ("Optimized: %d visible clusters (%.2f%%)\n", count, totalvis, count*100/totalvis);
	Msg ("Total clusters visible: %i\n", totalvis);
	Msg ("Average clusters visible: %i\n", totalvis / portalclusters);
}


void SetPortalSphere (portal_t *p)
{
	int		i;
	Vector	total, dist;
	winding_t	*w;
	float	r, bestr;

	w = p->winding;
	VectorCopy (vec3_origin, total);
	for (i=0 ; i<w->numpoints ; i++)
	{
		VectorAdd (total, w->points[i], total);
	}
	
	for (i=0 ; i<3 ; i++)
		total[i] /= w->numpoints;

	bestr = 0;		
	for (i=0 ; i<w->numpoints ; i++)
	{
		VectorSubtract (w->points[i], total, dist);
		r = VectorLength (dist);
		if (r > bestr)
			bestr = r;
	}
	VectorCopy (total, p->origin);
	p->radius = bestr;
}

/*
============
LoadPortals
============
*/
void LoadPortals (char *name)
{
	int			i, j;
	portal_t	*p;
	leaf_t		*l;
	char		magic[80];
	int			numpoints;
	winding_t	*w;
	int			leafnums[2];
	plane_t		plane;


	FILE *f;

	// Open the portal file.
	if ( g_bUseMPI )
	{
		// If we're using MPI, copy off the file to a temporary first. This will download the file
		// from the MPI master, then we get to use nice functions like fscanf on it.
		char tempPath[MAX_PATH], tempFile[MAX_PATH];
		if ( GetTempPath( sizeof( tempPath ), tempPath ) == 0 )
		{
			Error( "LoadPortals: GetTempPath failed.\n" );
		}

		if ( GetTempFileName( tempPath, "vvis_portal_", 0, tempFile ) == 0 )
		{
			Error( "LoadPortals: GetTempFileName failed.\n" );
		}

		// Read all the data from the network file into memory.
		FileHandle_t hFile = g_pFileSystem->Open(name, "r");
		if ( hFile == FILESYSTEM_INVALID_HANDLE )
			Error( "LoadPortals( %s ): couldn't get file from master.\n", name );

		CUtlVector<char> data;
		data.SetSize( g_pFileSystem->Size( hFile ) );
		g_pFileSystem->Read( data.Base(), data.Count(), hFile );
		g_pFileSystem->Close( hFile );

		// Dump it into a temp file.
		f = fopen( tempFile, "wt" );
		fwrite( data.Base(), 1, data.Count(), f );
		fclose( f );

		// Open the temp file up.
		f = fopen( tempFile, "r" );
	}
	else
	{
		f = fopen( name, "r" );
	}

	if ( !f )
		Error ("LoadPortals: couldn't read %s\n",name);

	if (fscanf (f,"%79s\n%i\n%i\n",magic, &portalclusters, &g_numportals) != 3)
		Error ("LoadPortals: failed to read header");
	if (strcmp(magic,PORTALFILE))
		Error ("LoadPortals: not a portal file");

	Msg ("%4i portalclusters\n", portalclusters);
	Msg ("%4i numportals\n", g_numportals);

	if (g_numportals * 2 >= MAX_PORTALS)
	{
		Error("The map overflows the max portal count (%d of max %d)!\n", g_numportals, MAX_PORTALS / 2 );
	}

	// these counts should take advantage of 64 bit systems automatically
	leafbytes = ((portalclusters+63)&~63)>>3;
	leaflongs = leafbytes/sizeof(long);
	
	portalbytes = ((g_numportals*2+63)&~63)>>3;
	portallongs = portalbytes/sizeof(long);

// each file portal is split into two memory portals
	portals = (portal_t*)malloc(2*g_numportals*sizeof(portal_t));
	memset (portals, 0, 2*g_numportals*sizeof(portal_t));
	
	leafs = (leaf_t*)malloc(portalclusters*sizeof(leaf_t));
	memset (leafs, 0, portalclusters*sizeof(leaf_t));

	originalvismapsize = portalclusters*leafbytes;
	uncompressedvis = (byte*)malloc(originalvismapsize);

	vismap = vismap_p = dvisdata;
	dvis->numclusters = portalclusters;
	vismap_p = (byte *)&dvis->bitofs[portalclusters];

	vismap_end = vismap + MAX_MAP_VISIBILITY;
		
	for (i=0, p=portals ; i<g_numportals ; i++)
	{
		if (fscanf (f, "%i %i %i ", &numpoints, &leafnums[0], &leafnums[1])
			!= 3)
			Error ("LoadPortals: reading portal %i", i);
		if (numpoints > MAX_POINTS_ON_WINDING)
			Error ("LoadPortals: portal %i has too many points", i);
		if ( (unsigned)leafnums[0] > portalclusters
		|| (unsigned)leafnums[1] > portalclusters)
			Error ("LoadPortals: reading portal %i", i);
		
		w = p->winding = NewWinding (numpoints);
		w->original = true;
		w->numpoints = numpoints;
		
		for (j=0 ; j<numpoints ; j++)
		{
			double	v[3];
			int		k;

			// scanf into double, then assign to vec_t
			// so we don't care what size vec_t is
			if (fscanf (f, "(%lf %lf %lf ) "
			, &v[0], &v[1], &v[2]) != 3)
				Error ("LoadPortals: reading portal %i", i);
			for (k=0 ; k<3 ; k++)
				w->points[j][k] = v[k];
		}
		fscanf (f, "\n");
		
	// calc plane
		PlaneFromWinding (w, &plane);

	// create forward portal
		l = &leafs[leafnums[0]];
		if (l->numportals == MAX_PORTALS_ON_LEAF)
			Error ("Leaf with too many portals");
		l->portals[l->numportals] = p;
		l->numportals++;
		
		p->winding = w;
		VectorSubtract (vec3_origin, plane.normal, p->plane.normal);
		p->plane.dist = -plane.dist;
		p->leaf = leafnums[1];
		SetPortalSphere (p);
		p++;
		
	// create backwards portal
		l = &leafs[leafnums[1]];
		if (l->numportals == MAX_PORTALS_ON_LEAF)
			Error ("Leaf with too many portals");
		l->portals[l->numportals] = p;
		l->numportals++;
		
		p->winding = NewWinding(w->numpoints);
		p->winding->numpoints = w->numpoints;
		for (j=0 ; j<w->numpoints ; j++)
		{
			VectorCopy (w->points[w->numpoints-1-j], p->winding->points[j]);
		}

		p->plane = plane;
		p->leaf = leafnums[0];
		SetPortalSphere (p);
		p++;

	}
	
	fclose (f);
}


/*
================
CalcPAS

Calculate the PAS (Potentially Audible Set)
by ORing together all the PVS visible from a leaf
================
*/
void CalcPAS (void)
{
	int		i, j, k, l, index;
	int		bitbyte;
	long	*dest, *src;
	byte	*scan;
	int		count;
	byte	uncompressed[MAX_MAP_LEAFS/8];
	byte	compressed[MAX_MAP_LEAFS/8];

	Msg ("Building PAS...\n");

	count = 0;
	for (i=0 ; i<portalclusters ; i++)
	{
		scan = uncompressedvis + i*leafbytes;
		memcpy (uncompressed, scan, leafbytes);
		for (j=0 ; j<leafbytes ; j++)
		{
			bitbyte = scan[j];
			if (!bitbyte)
				continue;
			for (k=0 ; k<8 ; k++)
			{
				if (! (bitbyte & (1<<k)) )
					continue;
				// OR this pvs row into the phs
				index = ((j<<3)+k);
				if (index >= portalclusters)
					Error ("Bad bit in PVS");	// pad bits should be 0
				src = (long *)(uncompressedvis + index*leafbytes);
				dest = (long *)uncompressed;
				for (l=0 ; l<leaflongs ; l++)
					((long *)uncompressed)[l] |= src[l];
			}
		}
		for (j=0 ; j<portalclusters ; j++)
		{
			if ( CheckBit( uncompressed, j ) )
			{
				count++;
			}
		}

	//
	// compress the bit string
	//
		j = CompressVis (uncompressed, compressed);

		dest = (long *)vismap_p;
		vismap_p += j;
		
		if (vismap_p > vismap_end)
			Error ("Vismap expansion overflow");

		dvis->bitofs[i][DVIS_PAS] = (byte *)dest-vismap;

		memcpy (dest, compressed, j);	
	}

	Msg ("Average clusters audible: %i\n", count/portalclusters);
}



static void GetBoundsForFace( int faceID, Vector &faceMin, Vector &faceMax )
{
	ClearBounds( faceMin, faceMax );
	dface_t *pFace = &dfaces[faceID];
	int i;
	for( i = pFace->firstedge; i < pFace->firstedge + pFace->numedges; i++ )
	{
		int edgeID = dsurfedges[i];
		if( edgeID < 0 )
		{
			edgeID = -edgeID;
		}
		dedge_t *pEdge = &dedges[edgeID];
		dvertex_t *pVert0 = &dvertexes[pEdge->v[0]];
		dvertex_t *pVert1 = &dvertexes[pEdge->v[1]];
		AddPointToBounds( pVert0->point, faceMin, faceMax );	
		AddPointToBounds( pVert1->point, faceMin, faceMax );	
	}
}

// FIXME: should stick this in mathlib
static float GetMinDistanceBetweenBoundingBoxes( const Vector &min1, const Vector &max1, 
												 const Vector &min2, const Vector &max2 )
{
	if( IsBoxIntersectingBox( min1, max1, min2, max2 ) )
	{
		return 0.0f;
	}

	Vector axisDist;
	int i;
	for( i = 0; i < 3; i++ )
	{
		if( min1[i] <= max2[i] && max1[i] >= min2[i] )
		{
			// the intersection in this dimension.
			axisDist[i] = 0.0f;
		}
		else
		{
			float dist1, dist2;
			dist1 = min1[i] - max2[i];
			dist2 = min2[i] - max1[i];
			axisDist[i] = dist1 > dist2 ? dist1 : dist2;
			Assert( axisDist[i] > 0.0f );
		}
	}

	float mag = axisDist.Length();
	Assert( mag > 0.0f );
	return mag;
}

static float CalcDistanceFromLeafToWater( int leafNum )
{
	byte	uncompressed[MAX_MAP_LEAFS/8];

	int j, k;

	// If we know that this one doesn't see a water surface then don't bother doing anything.
	if( !( dleafs[leafNum].contents & CONTENTS_TESTFOGVOLUME ) )
	{
		return 65535; // FIXME: make a define for this.
	}
	
	// First get the vis data..
	int cluster = dleafs[leafNum].cluster;
	if (cluster < 0)
	{
		return 65535; // FIXME: make a define for this.
	}
	
	DecompressVis( &dvisdata[dvis->bitofs[cluster][DVIS_PVS]], uncompressed );
	
	float minDist = 65535.0f; // FIXME: make a define for this.
	
	Vector leafMin, leafMax;
	
	leafMin[0] = ( float )dleafs[leafNum].mins[0];
	leafMin[1] = ( float )dleafs[leafNum].mins[1];
	leafMin[2] = ( float )dleafs[leafNum].mins[2];
	leafMax[0] = ( float )dleafs[leafNum].maxs[0];
	leafMax[1] = ( float )dleafs[leafNum].maxs[1];
	leafMax[2] = ( float )dleafs[leafNum].maxs[2];

/*
	CUtlVector<listplane_t> temp;
	
	// build a convex solid out of the planes so that we can get at the triangles.
	for( j = dleafs[i].firstleafbrush; j < dleafs[i].firstleafbrush + dleafs[i].numleafbrushes; j++ )
	{
		dbrush_t *pBrush = &dbrushes[j];
		for( k = pBrush->firstside; k < pBrush->firstside + pBrush->numsides; k++ )
		{
			dbrushside_t *pside = dbrushsides + k;
			dplane_t *pplane = dplanes + pside->planenum;
			AddListPlane( &temp, pplane->normal[0], pplane->normal[1], pplane->normal[2], pplane->dist );
		}
		CPhysConvex *pConvex = physcollision->ConvexFromPlanes( (float *)temp.Base(), temp.Count(), VPHYSICS_MERGE );
		ConvertConvexToCollide(  &pConvex, 
			temp.RemoveAll();
	}
*/
	
	// Iterate over all potentially visible clusters from this leaf
	for (j = 0; j < dvis->numclusters; ++j)
	{
		// Don't need to bother if this is the same as the current cluster
		if (j == cluster)
			continue;
		
		// If the cluster isn't in our current pvs, then get out of here.
		if ( !CheckBit( uncompressed, j ) )
			continue;
		
		// Found a visible cluster, now iterate over all leaves
		// inside that cluster
		for (k = 0; k < g_ClusterLeaves[j].leafCount; ++k)
		{
			int nClusterLeaf = g_ClusterLeaves[j].leafs[k];
			
			// Don't bother testing the ones that don't see a water boundary.
			if (!( dleafs[nClusterLeaf].contents & CONTENTS_TESTFOGVOLUME) )
			{
				continue;	
			}
			// Find the minimum distance between each surface on the boundary of the leaf 
			// that we have the pvs for and each water surface in the leaf that we are testing.
			int leafFaceID;
			for( leafFaceID = dleafs[nClusterLeaf].firstleafface; 
			     leafFaceID < dleafs[nClusterLeaf].firstleafface + dleafs[nClusterLeaf].numleaffaces; 
			     leafFaceID++ )
			{
				int faceID = dleaffaces[leafFaceID];
				dface_t *pFace = &dfaces[faceID];
				if( pFace->texinfo == -1 )
				{
					continue;
				}
				texinfo_t *pTexInfo = &texinfo[pFace->texinfo];
				if( pTexInfo->flags & SURF_WARP )
				{
					// Woo hoo!!!  We found a water face.
					// compare the bounding box of the face with the bounding
					// box of the leaf that we are looking from and see
					// what the closest distance is.
					// FIXME: this could be a face/face distance between the water
					// face and the bounding volume of the leaf.
					
					// Get the bounding box of the face
					Vector faceMin, faceMax;
					GetBoundsForFace( faceID, faceMin, faceMax );
					float dist = GetMinDistanceBetweenBoundingBoxes( leafMin, leafMax, faceMin, faceMax );
					if( dist < minDist )
					{
						minDist = dist;
					}
				}
			}
		}
	}
	return minDist;
}

static void CalcDistanceFromLeavesToWater( void )
{
	int i;
	for( i = 0; i < numleafs; i++ )
	{
		g_LeafMinDistToWater[i] = ( unsigned short )CalcDistanceFromLeafToWater( i );
	}
}

//-----------------------------------------------------------------------------
// Using the PVS, compute the visible fog volumes from each leaf
//-----------------------------------------------------------------------------
static void CalcVisibleFogVolumes()
{
	byte	uncompressed[MAX_MAP_LEAFS/8];

	int i, j, k;

	// Clear the contents flags for water testing
	for (i = 0; i < numleafs; ++i)
	{
		dleafs[i].contents &= ~CONTENTS_TESTFOGVOLUME;
		g_LeafMinDistToWater[i] = 65535;
	}

	for (i = 0; i < numleafs; ++i)
	{
		// If we've already discovered that this leaf needs testing,
		// no need to go through the work again...
		if (dleafs[i].contents & CONTENTS_TESTFOGVOLUME)
			continue;

		// Don't bother checking fog volumes from solid leaves
		if (dleafs[i].contents & CONTENTS_SOLID)
			continue;

		// First get the vis data..
		int cluster = dleafs[i].cluster;
		if (cluster < 0)
			continue;

		DecompressVis( &dvisdata[dvis->bitofs[cluster][DVIS_PVS]], uncompressed );

		// Iterate over all potentially visible clusters from this leaf
		for (j = 0; j < dvis->numclusters; ++j)
		{
			// Don't need to bother if this is the same as the current cluster
			if (j == cluster)
				continue;

			if ( !CheckBit( uncompressed, j ) )
				continue;

			// Found a visible cluster, now iterate over all leaves
			// inside that cluster
			for (k = 0; k < g_ClusterLeaves[j].leafCount; ++k)
			{
				int nClusterLeaf = g_ClusterLeaves[j].leafs[k];

				// Don't bother checking fog volumes from solid leaves
				if (dleafs[nClusterLeaf].contents & CONTENTS_SOLID)
					continue;

				// If any of these leaves have a different leaf water data
				// than this leaf, then we'll have to do an expensive
				// test during rendering....
				if (dleafs[nClusterLeaf].leafWaterDataID != dleafs[i].leafWaterDataID)
				{
					dleafs[i].contents |= CONTENTS_TESTFOGVOLUME;
					dleafs[nClusterLeaf].contents |= CONTENTS_TESTFOGVOLUME;
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Compute the bounding box, excluding 3D skybox + skybox, add it to keyvalues
//-----------------------------------------------------------------------------
float DetermineVisRadius( )
{
	float flRadius = -1;

	// Check the max vis range to determine the vis radius
	for (int i = 0; i < num_entities; ++i)
	{
		char* pEntity = ValueForKey(&entities[i], "classname");
		if (!strcmp(pEntity, "env_fog_parameters"))
		{
			flRadius = FloatForKey (&entities[i], "farz");
			if (flRadius == 0.0f)
				flRadius = -1.0f;
			break;
		}
	}

	return flRadius;
}


int RunVVis( int argc, char **argv )
{
	char	portalfile[1024];
	char		source[1024];
	int		i;
	double		start, end;


	Msg( "Valve Software - vvis.exe (%s)\n", __DATE__ );

	verbose = false;
	for (i=1 ; i<argc ; i++)
	{
		if (!strcmp(argv[i],"-threads"))
		{
			numthreads = atoi (argv[i+1]);
			i++;
		}
		else if (!strcmp(argv[i], "-fast"))
		{
			Msg ("fastvis = true\n");
			fastvis = true;
		}
		else if (!strcmp(argv[i], "-v"))
		{
			Msg ("verbose = true\n");
			verbose = true;
		}
		else if( !strcmp( argv[i], "-radius_override" ) )
		{
			g_bUseRadius = true;
			g_VisRadius = atof( argv[i+1] );
			i++;
			Msg( "Vis Radius = %4.2f\n", g_VisRadius );
			g_VisRadius = g_VisRadius * g_VisRadius;   // so distance check can be squared
		}
		else if (!strcmp (argv[i],"-nosort"))
		{
			Msg ("nosort = true\n");
			nosort = true;
		}
		else if (!strcmp (argv[i],"-tmpin"))
			strcpy (inbase, "/tmp");
		else if (!strcmp (argv[i],"-tmpout"))
			strcpy (outbase, "/tmp");
		else if( !stricmp( argv[i], "-low" ) )
		{
			g_bLowPriority = true;
		}
		else if( !strcmp( argv[i], "-dxlevel" ) )
		{
			g_nDXLevel = atoi( argv[i+1] );
			i++;
			Msg( "DXLevel = %d\n", g_nDXLevel );
		}
		else if ( !Q_strncasecmp( argv[i], "-mpi", 4 ) || !Q_strncasecmp( argv[i-1], "-mpi", 4 ) )
		{
			if ( stricmp( argv[i], "-mpi" ) == 0 )
				g_bUseMPI = true;
		
			// Any other args that start with -mpi are ok too.
			if ( i == argc - 1 )
				break;
		}
		else if (argv[i][0] == '-')
			Error ("Unknown option \"%s\"", argv[i]);
		else
			break;
	}

	if (i != argc - 1)
		Error ("usage: vvis [-mpi] [-mpi_updates] [-fast] [-v] [-radius_override] [-lowpriority] bspfile");

	start = I_FloatTime ();


	strcpy (source, argv[i]);
	StripExtension( source );
	CmdLib_InitFileSystem( argv[i] );

	// This part is just for VMPI. VMPI's file system needs the basedir in front of all filenames,
	// so we strip off the filename and prepend qdir here.
	ExtractFileBase( source, source, sizeof( source ) );
	strcpy( source, ExpandPath( source ) );

	if (!g_bUseMPI)
	{
		// Setup the logfile.
		char logFile[512];
		_snprintf( logFile, sizeof(logFile), "%s.log", source );
		SetSpewFunctionLogFile( logFile );
	}

	// Run in the background?
	if( g_bLowPriority )
	{
		SetLowPriority();
	}
	
	ThreadSetDefault ();

	char	targetPath[1024];
	GetPlatformMapPath( source, targetPath, g_nDXLevel, 1024 );
	Msg ("reading %s\n", targetPath);
	LoadBSPFile (targetPath);
	if (numnodes == 0 || numfaces == 0)
		Error ("Empty map");
	ParseEntities ();

	// Check the VMF for a vis radius
	if (!g_bUseRadius)
	{
		float flRadius = DetermineVisRadius( );
		if (flRadius > 0.0f)
		{
			g_bUseRadius = true;
			g_VisRadius = flRadius * flRadius;
		}
	}

	sprintf ( portalfile, "%s%s", inbase, argv[i] );
	StripExtension (portalfile);
	strcat (portalfile, ".prt");
	
	Msg ("reading %s\n", portalfile);
	LoadPortals (portalfile);

	CalcVis ();

	CalcPAS ();

	// We need a mapping from cluster to leaves, since the PVS
	// deals with clusters for both CalcVisibleFogVolumes and 
	BuildClusterTable();

	CalcVisibleFogVolumes();
	CalcDistanceFromLeavesToWater();

	visdatasize = vismap_p - dvisdata;	
	Msg ("visdatasize:%i  compressed from %i\n", visdatasize, originalvismapsize*2);

	Msg ("writing %s\n", targetPath);
	WriteBSPFile (targetPath);	
	
	end = I_FloatTime ();
	Msg("%5.1f seconds elapsed\n", end-start);

	CmdLib_Cleanup();
	return 0;
}


/*
===========
main
===========
*/
int main (int argc, char **argv)
{
	CommandLine()->CreateCmdLine( argc, argv );

	MathLib_Init( 2.2f, 2.2f, 0.0f, 1.0f,false, false, false, false );
	InstallSpewFunction();

	VVIS_SetupMPI( argc, argv );


	if ( g_bUseMPI && !g_bMPIMaster )
	{
		// VMPI workers should catch crashes and asserts and suchlike and fail gracefully instead 
		// of pestering the person with dialogs.
		try
		{
			return RunVVis( argc, argv );
		}
		catch( ... )
		{
			VMPI_HandleCrash( "Crash", true );
			return 0;
		}
	}
	else
	{
		return RunVVis( argc, argv );
	}
}


// When VVIS is used as a DLL (makes debugging vmpi vvis a lot easier), this is used to
// get it going.
class CVVisDLL : public IVVisDLL
{
public:
	virtual int main( int argc, char **argv )
	{
		return ::main( argc, argv );
	}
};

EXPOSE_SINGLE_INTERFACE( CVVisDLL, IVVisDLL, VVIS_INTERFACE_VERSION );

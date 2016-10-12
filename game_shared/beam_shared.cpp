//========= Copyright � 1996-2001, Valve LLC, All rights reserved. ============
//
// Purpose: Implements visual effects entities: sprites, beams, bubbles, etc.
//
// $NoKeywords: $
//=============================================================================

#include "cbase.h"
#include "beam_shared.h"
#include "customentity.h"
#include "decals.h"
#include "model_types.h"
#include "IEffects.h"

#if !defined( CLIENT_DLL )
#include "ndebugoverlay.h"
#include "sendproxy.h"
#else
#include "iviewrender_beams.h"
#endif

#define BEAM_DEFAULT_HALO_SCALE		10

#if !defined( CLIENT_DLL )
// Lightning target, just alias landmark
LINK_ENTITY_TO_CLASS( info_target, CPointEntity );
#endif


//-----------------------------------------------------------------------------
// Purpose: Returns true if the given entity is a fixed target for lightning.
//-----------------------------------------------------------------------------
bool IsStaticPointEntity( CBaseEntity *pEnt )
{
	if ( pEnt->GetMoveParent() )
		return false;

	if ( !pEnt->GetModelIndex() )
		return 1;

	if ( FClassnameIs( pEnt, "info_target" ) || FClassnameIs( pEnt, "info_landmark" ) || 
		FClassnameIs( pEnt, "path_corner" ) )
		return true;

	return false;
}

#if defined( CLIENT_DLL )
extern bool ComputeBeamEntPosition( CBaseEntity *pEnt, int nAttachment, Vector& pt );

void RecvProxy_Beam_ScrollSpeed( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	C_Beam *beam;
	float	val;

	// Unpack the data.
	val	= pData->m_Value.m_Float;
	val *= 0.1;

	beam = ( C_Beam * )pStruct;
	Assert( pOut == &beam->m_fSpeed );

	beam->m_fSpeed = val;
}
#else
static void* SendProxy_SendPredictableId( const void *pStruct, const void *pVarData, CSendProxyRecipients *pRecipients, int objectID )
{
	CBaseEntity *pEntity = (CBaseEntity *)pStruct;
	if ( !pEntity || !pEntity->m_PredictableID->IsActive() )
		return NULL;

	if ( !pEntity->GetOwnerEntity() )
		return NULL;

	CBaseEntity *owner = pEntity->GetOwnerEntity()->GetBaseEntity();
	if ( !owner || !owner->IsPlayer() )
		return NULL;

	CBasePlayer *pOwner = static_cast< CBasePlayer * >( owner );
	if ( !pOwner )
		return NULL;

	int id_player_index = pEntity->m_PredictableID->GetPlayer();
	int owner_player_index = pOwner->entindex() - 1;
	// Only send to owner player
	// FIXME:  Is this ever not the case due to the SetOnly call?
	if ( id_player_index != owner_player_index )
		return NULL;

	pRecipients->SetOnly( owner_player_index );
	return ( void * )pVarData;
}
#endif

LINK_ENTITY_TO_CLASS( beam, CBeam );

// This table encodes the CBeam data.
IMPLEMENT_NETWORKCLASS_ALIASED( Beam, DT_Beam )

BEGIN_NETWORK_TABLE_NOBASE( CBeam, DT_BeamPredictableId )
#if !defined( CLIENT_DLL )
	SendPropPredictableId( SENDINFO( m_PredictableID ) ),
	SendPropInt( SENDINFO( m_bIsPlayerSimulated ), 1, SPROP_UNSIGNED ),
#else
	RecvPropPredictableId( RECVINFO( m_PredictableID ) ),
	RecvPropInt( RECVINFO( m_bIsPlayerSimulated ) ),
#endif
END_NETWORK_TABLE()

BEGIN_NETWORK_TABLE_NOBASE( CBeam, DT_Beam )
#if !defined( CLIENT_DLL )
	SendPropInt		(SENDINFO(m_nBeamType),		8,	SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_nNumBeamEnts ),			5,	SPROP_UNSIGNED ),
	SendPropArray
	(
		SendPropEHandle( SENDINFO_ARRAY(m_hAttachEntity) ),
		m_hAttachEntity
	),
	SendPropArray
	(
		SendPropInt( SENDINFO_ARRAY(m_nAttachIndex), ATTACHMENT_INDEX_BITS, SPROP_UNSIGNED),
		m_nAttachIndex
	),
	SendPropInt		(SENDINFO(m_nHaloIndex),	16, SPROP_UNSIGNED ),
	SendPropFloat	(SENDINFO(m_fHaloScale),	0,	SPROP_NOSCALE ),
	SendPropFloat	(SENDINFO(m_fWidth),		8,	SPROP_ROUNDUP,	0.0f, MAX_BEAM_WIDTH ),
	SendPropFloat	(SENDINFO(m_fEndWidth),		8,	SPROP_ROUNDUP,	0.0f, MAX_BEAM_WIDTH ),
	SendPropFloat	(SENDINFO(m_fFadeLength),	0,	SPROP_NOSCALE ),
	SendPropFloat	(SENDINFO(m_fAmplitude),	8,	SPROP_ROUNDDOWN,	0.0f, MAX_BEAM_NOISEAMPLITUDE ),
	SendPropFloat	(SENDINFO(m_fStartFrame),	8,	SPROP_ROUNDDOWN,	0.0f,   256.0f),
	SendPropFloat	(SENDINFO(m_fSpeed),		8,	SPROP_ROUNDUP,	0.0f,	MAX_BEAM_SCROLLSPEED),
	SendPropInt		(SENDINFO(m_nRenderFX),		8,	SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_nRenderMode),	8,	SPROP_UNSIGNED ),
	SendPropFloat	(SENDINFO(m_flFrameRate),	10, SPROP_ROUNDUP, -25.0f, 25.0f ),
	SendPropFloat	(SENDINFO(m_flFrame),		20, SPROP_ROUNDDOWN,	0.0f,   256.0f),
	SendPropInt		(SENDINFO(m_clrRender),		32,	SPROP_UNSIGNED ),
		SendPropVector	(SENDINFO(m_vecEndPos),		-1,	SPROP_COORD ),
	SendPropModelIndex(SENDINFO(m_nModelIndex) ),
	SendPropVector (SENDINFO(m_vecOrigin), 19, 0,	MIN_COORD_INTEGER, MAX_COORD_INTEGER),
	SendPropEHandle(SENDINFO_NAME(m_hMoveParent, moveparent) ),
	SendPropDataTable( "beampredictable_id", 0, &REFERENCE_SEND_TABLE( DT_BeamPredictableId ), SendProxy_SendPredictableId ),

#else
	RecvPropInt		(RECVINFO(m_nBeamType)),
	RecvPropInt		(RECVINFO(m_nNumBeamEnts)),
	RecvPropArray	
	(
		RecvPropEHandle (RECVINFO(m_hAttachEntity[0])), 
		m_hAttachEntity
	),
	RecvPropArray	
	(
		RecvPropInt (RECVINFO(m_nAttachIndex[0])), 
		m_nAttachIndex
	),
	RecvPropInt		(RECVINFO(m_nHaloIndex)),
	RecvPropFloat	(RECVINFO(m_fHaloScale)),
	RecvPropFloat	(RECVINFO(m_fWidth)),
	RecvPropFloat	(RECVINFO(m_fEndWidth)),
	RecvPropFloat	(RECVINFO(m_fFadeLength)),
	RecvPropFloat	(RECVINFO(m_fAmplitude)),
	RecvPropFloat	(RECVINFO(m_fStartFrame)),
	RecvPropFloat	(RECVINFO(m_fSpeed), 0, RecvProxy_Beam_ScrollSpeed ),
	RecvPropFloat(RECVINFO(m_flFrameRate)),
	RecvPropInt(RECVINFO(m_clrRender)),
	RecvPropInt(RECVINFO(m_nRenderFX)),
	RecvPropInt(RECVINFO(m_nRenderMode)),
	RecvPropFloat(RECVINFO(m_flFrame)),
	RecvPropVector(RECVINFO(m_vecEndPos)),
	RecvPropInt(RECVINFO(m_nModelIndex)),

	RecvPropVector(RECVINFO_NAME(m_vecNetworkOrigin, m_vecOrigin)),
	RecvPropInt( RECVINFO_NAME(m_hNetworkMoveParent, moveparent), 0, RecvProxy_IntToMoveParent ),
	RecvPropDataTable( "beampredictable_id", 0, 0, &REFERENCE_RECV_TABLE( DT_BeamPredictableId ) ),
#endif
END_NETWORK_TABLE()

#if !defined( CLIENT_DLL )
BEGIN_DATADESC( CBeam )
	DEFINE_FIELD( CBeam, m_nHaloIndex, FIELD_INTEGER ),
	DEFINE_FIELD( CBeam, m_nBeamType, FIELD_INTEGER ),
	DEFINE_FIELD( CBeam, m_nNumBeamEnts, FIELD_INTEGER ),
	DEFINE_ARRAY( CBeam, m_hAttachEntity, FIELD_EHANDLE, MAX_BEAM_ENTS ),
	DEFINE_ARRAY( CBeam, m_nAttachIndex, FIELD_INTEGER, MAX_BEAM_ENTS ),

	DEFINE_FIELD( CBeam, m_fWidth, FIELD_FLOAT ),
	DEFINE_FIELD( CBeam, m_fEndWidth, FIELD_FLOAT ),
	DEFINE_FIELD( CBeam, m_fFadeLength, FIELD_FLOAT ),
	DEFINE_FIELD( CBeam, m_fHaloScale, FIELD_FLOAT ),
	DEFINE_FIELD( CBeam, m_fAmplitude, FIELD_FLOAT ),
	DEFINE_FIELD( CBeam, m_fStartFrame, FIELD_FLOAT ),
	DEFINE_FIELD( CBeam, m_fSpeed, FIELD_FLOAT ),

	DEFINE_FIELD( CBeam, m_flFrameRate, FIELD_FLOAT ),
	DEFINE_FIELD( CBeam, m_flFrame, FIELD_FLOAT ),

	DEFINE_KEYFIELD( CBeam, m_flDamage, FIELD_FLOAT, "damage" ),
	DEFINE_FIELD( CBeam, m_flFireTime, FIELD_TIME ),

	DEFINE_FIELD( CBeam, m_vecEndPos, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( CBeam, m_hEndEntity, FIELD_EHANDLE ),

	// Inputs
	DEFINE_INPUTFUNC( CBeam, FIELD_FLOAT, "Width", InputWidth ),
	DEFINE_INPUTFUNC( CBeam, FIELD_FLOAT, "Noise", InputNoise ),
	DEFINE_INPUT( CBeam, m_fSpeed, FIELD_FLOAT, "ScrollSpeed" ),

END_DATADESC()
#endif

BEGIN_PREDICTION_DATA( CBeam )

	DEFINE_PRED_FIELD( CBeam, m_nBeamType, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	
	DEFINE_PRED_FIELD( CBeam, m_nNumBeamEnts, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_ARRAY( CBeam, m_hAttachEntity, FIELD_EHANDLE, MAX_BEAM_ENTS, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_ARRAY( CBeam, m_nAttachIndex, FIELD_INTEGER, MAX_BEAM_ENTS, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_nHaloIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_fHaloScale, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_fWidth, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_fEndWidth, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_fFadeLength, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_fAmplitude, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_fStartFrame, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_fSpeed, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_nRenderFX, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_nRenderMode, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_flFrameRate, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_flFrame, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( CBeam, m_clrRender, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD_TOL( CBeam, m_vecEndPos, FIELD_VECTOR, FTYPEDESC_INSENDTABLE, 0.125f ),
	DEFINE_PRED_FIELD( CBeam, m_nModelIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX ),
	DEFINE_PRED_FIELD_TOL( CBeam, m_vecOrigin, FIELD_VECTOR, FTYPEDESC_INSENDTABLE, 0.125f ),
	
	//DEFINE_PRED_FIELD( CBeam, m_pMoveParent, SendProxy_MoveParent ),

END_PREDICTION_DATA()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBeam::CBeam( void )
{
#ifdef _DEBUG
	// necessary since in debug, we initialize vectors to NAN for debugging
	m_vecEndPos.Init();
#endif
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *szModelName - 
//-----------------------------------------------------------------------------
void CBeam::SetModel( const char *szModelName )
{
	int modelIndex = modelinfo->GetModelIndex( szModelName );
	const model_t *model = modelinfo->GetModel( modelIndex );
	if ( model && modelinfo->GetModelType( model ) != mod_sprite )
	{
		Msg( "Setting CBeam to non-sprite model %s\n", szModelName );
	}
#if !defined( CLIENT_DLL )
	UTIL_SetModel( this, szModelName );
#else
	BaseClass::SetModel( szModelName );
#endif
}


void CBeam::Spawn( void )
{
	SetMoveType( MOVETYPE_NONE );
	SetSolid( SOLID_NONE );							// Remove model & collisions
	m_nRenderMode = kRenderTransTexture;
	Relink();
	Precache( );
}


void CBeam::Precache( void )
{
	if ( GetOwnerEntity() )
	{
		SetStartEntity( GetOwnerEntity() );
	}
	
	if ( m_hEndEntity.Get() )
	{
		SetEndEntity( m_hEndEntity );
	}
}

void CBeam::SetStartEntity( CBaseEntity *pEntity )
{ 
	Assert( m_nNumBeamEnts >= 2 );
	m_hAttachEntity.Set( 0, pEntity );
	SetOwnerEntity( pEntity );
}

void CBeam::SetEndEntity( CBaseEntity *pEntity ) 
{ 
	Assert( m_nNumBeamEnts >= 2 );
	m_hAttachEntity.Set( m_nNumBeamEnts-1, pEntity );
	m_hEndEntity = pEntity;
}


//-----------------------------------------------------------------------------
// This will change things so the abs position matches the requested spot
//-----------------------------------------------------------------------------
void CBeam::SetAbsStartPos( const Vector &pos )
{
	if (!GetMoveParent())
	{
		SetStartPos( pos );
		return;
	}

	Vector vecLocalPos;
	matrix3x4_t worldToBeam;
	MatrixInvert( EntityToWorldTransform(), worldToBeam );
	VectorTransform( pos, worldToBeam, vecLocalPos );
	SetStartPos( vecLocalPos );
}

void CBeam::SetAbsEndPos( const Vector &pos )
{
	if (!GetMoveParent())
	{
		SetEndPos( pos );
		return;
	}

	Vector vecLocalPos;
	matrix3x4_t worldToBeam;
	MatrixInvert( EntityToWorldTransform(), worldToBeam );
	VectorTransform( pos, worldToBeam, vecLocalPos );
	SetEndPos( vecLocalPos );
}

#if !defined( CLIENT_DLL )

// These don't take attachments into account
const Vector &CBeam::GetAbsStartPos( void ) const
{
	if ( GetType() == BEAM_ENTS && GetStartEntity() )
	{
		edict_t *pent =  engine->PEntityOfEntIndex( GetStartEntity() );
		CBaseEntity *ent = CBaseEntity::Instance( pent );
		if ( !ent )
		{
			return GetAbsOrigin();
		}
		return ent->GetAbsOrigin();
	}
	return GetAbsOrigin();
}


const Vector &CBeam::GetAbsEndPos( void ) const
{
	if ( GetType() != BEAM_POINTS && GetType() != BEAM_HOSE && GetEndEntity() ) 
	{
		edict_t *pent =  engine->PEntityOfEntIndex( GetEndEntity() );
		CBaseEntity *ent = CBaseEntity::Instance( pent );
		if ( ent )
			return ent->GetAbsOrigin();
	}

	if (!const_cast<CBeam*>(this)->GetMoveParent())
		return m_vecEndPos.Get();

	// FIXME: Cache this off?
	static Vector vecAbsPos;
	VectorTransform( m_vecEndPos, EntityToWorldTransform(), vecAbsPos );
	return vecAbsPos;
}

#else

//-----------------------------------------------------------------------------
// Unlike the server, these take attachments into account
//-----------------------------------------------------------------------------
const Vector &C_Beam::GetAbsStartPos( void ) const
{
	static Vector vecStartAbsPosition;
	if ( GetType() != BEAM_POINTS && GetType() != BEAM_HOSE ) 
	{
		if (ComputeBeamEntPosition( m_hAttachEntity[0], m_nAttachIndex[0], vecStartAbsPosition ))
			return vecStartAbsPosition;
	}

	return GetAbsOrigin();
}


const Vector &C_Beam::GetAbsEndPos( void ) const
{
	static Vector vecEndAbsPosition;
	if ( GetType() != BEAM_POINTS && GetType() != BEAM_HOSE ) 
	{
		if (ComputeBeamEntPosition( m_hAttachEntity[m_nNumBeamEnts-1], m_nAttachIndex[m_nNumBeamEnts-1], vecEndAbsPosition ))
			return vecEndAbsPosition;
	}

	if (!const_cast<C_Beam*>(this)->GetMoveParent())
		return m_vecEndPos.Get();

	// FIXME: Cache this off?
	VectorTransform( m_vecEndPos, EntityToWorldTransform(), vecEndAbsPosition );
	return vecEndAbsPosition;
}
#endif

CBeam *CBeam::BeamCreate( const char *pSpriteName, float width )
{
	// Create a new entity with CBeam private data
	CBeam *pBeam  = CREATE_ENTITY( CBeam, "beam" );
	pBeam->BeamInit( pSpriteName, width );

	return pBeam;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pSpriteName - 
//			&origin - 
//			animate - 
// Output : CSprite
//-----------------------------------------------------------------------------
CBeam *CBeam::BeamCreatePredictable( const char *module, int line, bool persist, const char *pSpriteName, float width, CBasePlayer *pOwner )
{
	CBeam *pBeam = ( CBeam * )CBaseEntity::CreatePredictedEntityByName( "beam", module, line, persist );
	if ( pBeam )
	{
		pBeam->BeamInit( pSpriteName, width );
		pBeam->SetOwnerEntity( pOwner );
		pBeam->SetPlayerSimulated( pOwner );
	}

	return pBeam;
}

void CBeam::BeamInit( const char *pSpriteName, float width )
{
	SetColor( 255, 255, 255 );
	SetBrightness( 255 );
	SetNoise( 0 );
	SetFrame( 0 );
	SetScrollRate( 0 );
	SetModelName( MAKE_STRING( pSpriteName ) );
	m_nRenderMode = kRenderTransTexture;
	SetTexture( PrecacheModel( pSpriteName ) );
	SetWidth( width );
	SetEndWidth( width );
	SetFadeLength( 0 );			// No fade
	for (int i=0;i<MAX_BEAM_ENTS;i++)
	{
		m_hAttachEntity.Set( i, NULL );
		m_nAttachIndex.Set( i, 0 );
	}
	m_nHaloIndex	= 0;
	m_fHaloScale	= BEAM_DEFAULT_HALO_SCALE;
	m_nBeamType		= 0;
}


void CBeam::PointsInit( const Vector &start, const Vector &end )
{
	SetType( BEAM_POINTS );
	m_nNumBeamEnts = 2;
	SetStartPos( start );
	SetEndPos( end );
	SetStartAttachment( 0 );
	SetEndAttachment( 0 );
	RelinkBeam();
}


void CBeam::HoseInit( const Vector &start, const Vector &direction )
{
	SetType( BEAM_HOSE );
	m_nNumBeamEnts = 2;
	SetStartPos( start );
	SetEndPos( direction );
	SetStartAttachment( 0 );
	SetEndAttachment( 0 );
	RelinkBeam();
}


void CBeam::PointEntInit( const Vector &start, CBaseEntity *pEndEntity )
{
	SetType( BEAM_ENTPOINT );
	m_nNumBeamEnts = 2;
	SetStartPos( start );
	SetEndEntity( pEndEntity );
	SetStartAttachment( 0 );
	SetEndAttachment( 0 );
	RelinkBeam();
}

void CBeam::EntsInit( CBaseEntity *pStartEntity, CBaseEntity *pEndEntity )
{
	SetType( BEAM_ENTS );
	m_nNumBeamEnts = 2;
	SetStartEntity( pStartEntity );
	SetEndEntity( pEndEntity );
	SetStartAttachment( 0 );
	SetEndAttachment( 0 );
	RelinkBeam();
}

void CBeam::LaserInit( CBaseEntity *pStartEntity, CBaseEntity *pEndEntity )
{
	SetType( BEAM_LASER );
	m_nNumBeamEnts = 2;
	SetStartEntity( pStartEntity );
	SetEndEntity( pEndEntity );
	SetStartAttachment( 0 );
	SetEndAttachment( 0 );
	RelinkBeam();
}

void CBeam::SplineInit( int nNumEnts, CBaseEntity** pEntList, int *attachment )
{
	if (nNumEnts < 2)
	{
		Msg("ERROR: Min of 2 ents required for spline beam.\n");
	}
	else if (nNumEnts > MAX_BEAM_ENTS)
	{
		Msg("ERROR: Max of %i ents allowed for spline beam.\n",MAX_BEAM_ENTS);
	}
	SetType( BEAM_SPLINE );

	for (int i=0;i<nNumEnts;i++)
	{
		m_hAttachEntity.Set( i, pEntList[i] ); 
		m_nAttachIndex.Set( i, attachment[i] );
	}
	m_nNumBeamEnts = nNumEnts;
	RelinkBeam();
}


void CBeam::RelinkBeam( void )
{
	// FIXME: Why doesn't this just define the absbox too?
	// It seems that we don't need to recompute the absbox
	// in CBaseEntity::SetObjectCollisionBox, in fact the absbox
	// computed there seems way too big
	const Vector &startPos = GetAbsStartPos(), &endPos = GetAbsEndPos();

	Vector vecBeamMin, vecBeamMax;
	VectorMin( startPos, endPos, vecBeamMin );
	VectorMax( startPos, endPos, vecBeamMax );

	SetCollisionBounds( vecBeamMin - GetAbsOrigin(), vecBeamMax - GetAbsOrigin() );
	SetSize( WorldAlignMins(), WorldAlignMaxs() );
	Relink();
}

#if 0
void CBeam::SetObjectCollisionBox( void )
{
	const Vector &startPos = GetAbsStartPos(), &endPos = GetAbsEndPos();

	Vector vecBeamMin, vecBeamMax;
	VectorMin( startPos, endPos, vecBeamMin );
	VectorMax( startPos, endPos, vecBeamMax );

	SetAbsMins( vecBeamMin );
	SetAbsMaxs( vecBeamMax );
}
#endif


CBaseEntity *CBeam::RandomTargetname( const char *szName )
{
#if !defined( CLIENT_DLL )
	int total = 0;

	CBaseEntity *pEntity = NULL;
	CBaseEntity *pNewEntity = NULL;
	while ((pNewEntity = gEntList.FindEntityByName( pNewEntity, szName, NULL )) != NULL)
	{
		total++;
		if (random->RandomInt(0,total-1) < 1)
			pEntity = pNewEntity;
	}
	return pEntity;
#else
	return NULL;
#endif
}


void CBeam::DoSparks( const Vector &start, const Vector &end )
{
#if !defined( CLIENT_DLL )
	if ( HasSpawnFlags(SF_BEAM_SPARKSTART|SF_BEAM_SPARKEND) )
	{
		if ( HasSpawnFlags( SF_BEAM_SPARKSTART ) )
		{
			g_pEffects->Sparks( start );
		}
		if ( HasSpawnFlags( SF_BEAM_SPARKEND ) )
		{
			g_pEffects->Sparks( end );
		}
	}
#endif
}


//-----------------------------------------------------------------------------
// Purpose: Damages anything in the beam.
// Input  : ptr - 
//-----------------------------------------------------------------------------
void CBeam::BeamDamage( trace_t *ptr )
{
	RelinkBeam();
#if !defined( CLIENT_DLL )
	if ( ptr->fraction != 1.0 && ptr->m_pEnt != NULL )
	{
		CBaseEntity *pHit = ptr->m_pEnt;
		if ( pHit )
		{
			ClearMultiDamage();
			Vector dir = ptr->endpos - GetAbsOrigin();
			VectorNormalize( dir );
			CTakeDamageInfo info( this, this, m_flDamage * (gpGlobals->curtime - m_flFireTime), DMG_ENERGYBEAM );
			CalculateMeleeDamageForce( &info, dir, ptr->endpos );
			pHit->DispatchTraceAttack( info, dir, ptr );
			ApplyMultiDamage();
			if ( HasSpawnFlags( SF_BEAM_DECALS ) )
			{
				if ( pHit->IsBSPModel() )
				{
					UTIL_DecalTrace( ptr, "BigShot" );
				}
			}
		}
	}
#endif
	m_flFireTime = gpGlobals->curtime;
}

#if !defined( CLIENT_DLL )
//-----------------------------------------------------------------------------
// Purpose: Input handler for the beam width. Sets the end width based on the
//			beam width.
// Input  : Beam width in tenths of world units.
//-----------------------------------------------------------------------------
void CBeam::InputWidth( inputdata_t &inputdata )
{
	SetWidth( inputdata.value.Float() );
	SetEndWidth( inputdata.value.Float() );
}

void CBeam::InputNoise( inputdata_t &inputdata )
{
	SetNoise( inputdata.value.Float() );
}

void CBeam::SetTransmit( CCheckTransmitInfo *pInfo )
{
	// Are we already marked for transmission?
	if ( pInfo->WillTransmit( entindex() ) )
		return;

	BaseClass::SetTransmit( pInfo );
	
	// Force our attached entities to go too...
	for ( int i=0; i < MAX_BEAM_ENTS; ++i )
	{
		if ( m_hAttachEntity[i].Get() )
		{
			m_hAttachEntity[i]->SetTransmit( pInfo );
		}
	}
}

#endif

//-----------------------------------------------------------------------------
// Purpose: Draw any debug text overlays.
// Output : Returns the new text offset from the top.
//-----------------------------------------------------------------------------
int CBeam::DrawDebugTextOverlays(void)
{
#if !defined( CLIENT_DLL )
	int text_offset = BaseClass::DrawDebugTextOverlays();
	if (m_debugOverlays & OVERLAY_TEXT_BIT) 
	{
		// Print state
		char tempstr[512];
		Q_snprintf(tempstr, sizeof(tempstr), "start: (%.2f,%.2f,%.2f)", GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z);
		NDebugOverlay::EntityText(entindex(),text_offset,tempstr,0);
		text_offset++;

		Q_snprintf(tempstr, sizeof(tempstr), "end  : (%.2f,%.2f,%.2f)", m_vecEndPos.GetX(), m_vecEndPos.GetY(), m_vecEndPos.GetZ());
		NDebugOverlay::EntityText(entindex(),text_offset,tempstr,0);
		text_offset++;
	}
	
	return text_offset;
#else
	return 0;
#endif
}

#if defined( CLIENT_DLL )

// Purpose: 
// Input  : isbeingremoved - 
//			*predicted - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBeam::OnPredictedEntityRemove( bool isbeingremoved, C_BaseEntity *predicted )
{
	BaseClass::OnPredictedEntityRemove( isbeingremoved, predicted );

	CBeam *beam = dynamic_cast< CBeam * >( predicted );
	if ( !beam )
	{
		// Hrm, we didn't link up to correct type!!!
		Assert( 0 );
		// Delete right away since it's fucked up
		return true;
	}

	if ( beam->IsEFlagSet( EFL_KILLME ) )
	{
		// Don't delete right away
		AddEFlags( EFL_KILLME );
		return false;
	}

	// Go ahead and delete if it's not short-lived
	return true;
}

int CBeam::DrawModel( int flags )
{
	if ( !m_bReadyToDraw )
		return 0;

	if ( IsMarkedForDeletion() )
		return 0;

	beams->DrawBeam( this );
	return 0;
}

void CBeam::OnDataChanged( DataUpdateType_t updateType )
{
	MarkMessageReceived();

	// Make sure that the correct model is referenced for this entity
	SetModelPointer( modelinfo->GetModel( GetModelIndex() ) );

	// Convert weapon world models to viewmodels if they're weapons being carried by the local player
	for (int i=0;i<MAX_BEAM_ENTS;i++)
	{
		C_BaseEntity *pEnt = m_hAttachEntity[i].Get();
		if ( pEnt )
		{
			C_BaseCombatWeapon *pWpn = dynamic_cast<C_BaseCombatWeapon *>(pEnt);
			if ( pWpn && pWpn->IsCarriedByLocalPlayer() )
			{
				C_BasePlayer *player = ToBasePlayer( pWpn->GetOwner() );

				C_BaseViewModel *pViewModel = player ? player->GetViewModel( 0 ) : NULL;
				if ( pViewModel )
				{
					// Get the viewmodel and use it instead
					m_hAttachEntity.Set( i, pViewModel );
				}
			}
		}
	}

	// Compute the bounds here...
	Vector mins, maxs;
	ComputeBounds( mins, maxs );
	SetCollisionBounds( mins, maxs );
}

bool CBeam::IsTransparent( void )
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Adds to beam entity list
//-----------------------------------------------------------------------------
void CBeam::AddEntity( void )
{
	// If set to invisible, skip. Do this before resetting the entity pointer so it has 
	// valid data to decide whether it's visible.
	if ( !ShouldDraw() )
	{
		return;
	}

	MoveToLastReceivedPosition();
}

//-----------------------------------------------------------------------------
// Computes the bounding box of a beam local to the origin of the beam
//-----------------------------------------------------------------------------
void CBeam::ComputeBounds( Vector& mins, Vector& maxs )
{
	switch( GetType() )
	{
	case BEAM_LASER:
	case BEAM_ENTS:
	case BEAM_SPLINE:
	case BEAM_ENTPOINT:
		{
			// Compute the bounds here...
			Vector attachmentPoint( 0, 0, 0 );
			mins.Init( 99999, 99999, 99999 );
			maxs.Init( -99999, -99999, -99999 );
			for (int i = 0; i < m_nNumBeamEnts; ++i )
			{
				if ( m_hAttachEntity[i].Get() )
				{
					if (!ComputeBeamEntPosition( m_hAttachEntity[i], m_nAttachIndex[i], attachmentPoint ))
						continue;
				}
				else
				{
					if (i == 0)
					{
						VectorCopy( GetAbsStartPos(), attachmentPoint );
					}
					else if (i == 1)
					{
						VectorCopy( GetAbsEndPos(), attachmentPoint );
					}
					else
					{
						Assert(0);
					}
				}

				mins = mins.Min( attachmentPoint );
				maxs = maxs.Max( attachmentPoint );
			}
		}
		break;

	case BEAM_POINTS:
	default:
		{
			Vector vecAbsStart = GetAbsStartPos();
			Vector vecAbsEnd = GetAbsEndPos();

			for (int i = 0; i < 3; ++i)
			{
				if (vecAbsStart[i] < vecAbsEnd[i])
				{
					mins[i] = vecAbsStart[i];
					maxs[i] = vecAbsEnd[i];
				}
				else
				{
					mins[i] = vecAbsEnd[i];
					maxs[i] = vecAbsStart[i];
				}
			}
		}
		break;
	}

	// Make sure the bounds are measured in *relative coords*
	Vector vecAbsOrigin = GetAbsOrigin();
	mins -= vecAbsOrigin;
	maxs -= vecAbsOrigin;
}
#endif
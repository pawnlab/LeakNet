//========= Copyright � 1996-2001, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#include "cbase.h"
#include "AI_BaseNPC.h"
#include "AI_Senses.h"
#include "AI_Memory.h"
#include "engine/IEngineSound.h"
#include "ammodef.h"
#include "sprite.h"
#include "hl2_dll/hl2_player.h"
#include "soundenvelope.h"
#include "explode.h"
#include "IEffects.h"
#include "animation.h"

#include "iservervehicle.h"

//Debug visualization
ConVar	g_debug_turret_ceiling( "g_debug_turret_ceiling", "0" );

#define	CEILING_TURRET_MODEL		"models/combine_turrets/ceiling_turret.mdl"
#define CEILING_TURRET_GLOW_SPRITE	"sprites/glow1.vmt"
#define CEILING_TURRET_BC_YAW		"aim_yaw"
#define CEILING_TURRET_BC_PITCH		"aim_pitch"
#define	CEILING_TURRET_RANGE		1500
#define CEILING_TURRET_SPREAD		VECTOR_CONE_2DEGREES
#define	CEILING_TURRET_MAX_WAIT		5
#define	CEILING_TURRET_PING_TIME	1.0f	//LPB!!

#define	CEILING_TURRET_VOICE_PITCH_LOW	45
#define	CEILING_TURRET_VOICE_PITCH_HIGH	100

//Aiming variables
#define	CEILING_TURRET_MAX_NOHARM_PERIOD	0.0f
#define	CEILING_TURRET_MAX_GRACE_PERIOD		3.0f

//Spawnflags
#define SF_CEILING_TURRET_AUTOACTIVATE		0x00000020
#define SF_CEILING_TURRET_STARTINACTIVE		0x00000040

//Heights
#define	CEILING_TURRET_RETRACT_HEIGHT	24
#define	CEILING_TURRET_DEPLOY_HEIGHT	64

//Activities
int ACT_CEILING_TURRET_OPEN;
int ACT_CEILING_TURRET_CLOSE;
int ACT_CEILING_TURRET_OPEN_IDLE;
int ACT_CEILING_TURRET_CLOSED_IDLE;
int ACT_CEILING_TURRET_FIRE;

//Turret states
enum turretState_e
{
	TURRET_SEARCHING,
	TURRET_AUTO_SEARCHING,
	TURRET_ACTIVE,
	TURRET_DEPLOYING,
	TURRET_RETIRING,
	TURRET_DEAD,
};

//Eye states
enum eyeState_t
{
	TURRET_EYE_SEE_TARGET,			//Sees the target, bright and big
	TURRET_EYE_SEEKING_TARGET,		//Looking for a target, blinking (bright)
	TURRET_EYE_DORMANT,				//Not active
	TURRET_EYE_DEAD,				//Completely invisible
	TURRET_EYE_DISABLED,			//Turned off, must be reactivated before it'll deploy again (completely invisible)
};

//
// Ceiling Turret
//

class CNPC_CeilingTurret : public CAI_BaseNPC
{
	DECLARE_CLASS( CNPC_CeilingTurret, CAI_BaseNPC );
public:
	
	CNPC_CeilingTurret( void );
	~CNPC_CeilingTurret( void );

	void	Precache( void );
	void	Spawn( void );

	// Think functions
	void	Retire( void );
	void	Deploy( void );
	void	ActiveThink( void );
	void	SearchThink( void );
	void	AutoSearchThink( void );
	void	DeathThink( void );

	// Inputs
	void	InputToggle( inputdata_t &inputdata );
	void	InputEnable( inputdata_t &inputdata );
	void	InputDisable( inputdata_t &inputdata );
	
	float	MaxYawSpeed( void );

	int		OnTakeDamage( const CTakeDamageInfo &inputInfo );

	Class_T	Classify( void ) 
	{
		if( m_bEnabled ) 
			return CLASS_MILITARY;

		return CLASS_NONE;
	}
	
	bool	FVisible( CBaseEntity *pEntity, int traceMask = MASK_OPAQUE, CBaseEntity **ppBlocker = NULL );

	Vector	EyeOffset( Activity nActivity ) 
	{
		Vector vecEyeOffset(0,0,-64);
		GetEyePosition( GetModelPtr(), vecEyeOffset );
		return vecEyeOffset;
	}

	Vector	EyePosition( void )
	{
		return GetAbsOrigin() + EyeOffset(GetActivity());
	}

protected:
	
	bool	PreThink( turretState_e state );
	void	Shoot( const Vector &vecSrc, const Vector &vecDirToEnemy );
	void	SetEyeState( eyeState_t state );
	void	Ping( void );	
	void	Toggle( void );
	void	Enable( void );
	void	Disable( void );
	void	SpinUp( void );
	void	SpinDown( void );
	void	SetHeight( float height );

	bool	UpdateFacing( void );

	int		m_iAmmoType;
	int		m_iMinHealthDmg;

	bool	m_bAutoStart;
	bool	m_bActive;		//Denotes the turret is deployed and looking for targets
	bool	m_bBlinkState;
	bool	m_bEnabled;		//Denotes whether the turret is able to deploy or not
	
	float	m_flShotTime;
	float	m_flLastSight;
	float	m_flPingTime;

	QAngle	m_vecGoalAngles;

	CSprite	*m_pEyeGlow;

	COutputEvent m_OnDeploy;
	COutputEvent m_OnRetire;
	COutputEvent m_OnTipped;

	DECLARE_DATADESC();
};

//Datatable
BEGIN_DATADESC( CNPC_CeilingTurret )

	DEFINE_FIELD( CNPC_CeilingTurret, m_iAmmoType,		FIELD_INTEGER ),
	DEFINE_KEYFIELD( CNPC_CeilingTurret, m_iMinHealthDmg, FIELD_INTEGER, "minhealthdmg" ),
	DEFINE_FIELD( CNPC_CeilingTurret, m_bAutoStart,		FIELD_BOOLEAN ),
	DEFINE_FIELD( CNPC_CeilingTurret, m_bActive,		FIELD_BOOLEAN ),
	DEFINE_FIELD( CNPC_CeilingTurret, m_bBlinkState,	FIELD_BOOLEAN ),
	DEFINE_FIELD( CNPC_CeilingTurret, m_bEnabled,		FIELD_BOOLEAN ),
	DEFINE_FIELD( CNPC_CeilingTurret, m_flShotTime,		FIELD_TIME ),
	DEFINE_FIELD( CNPC_CeilingTurret, m_flLastSight,	FIELD_TIME ),
	DEFINE_FIELD( CNPC_CeilingTurret, m_flPingTime,		FIELD_TIME ),
	DEFINE_FIELD( CNPC_CeilingTurret, m_vecGoalAngles,	FIELD_VECTOR ),
	DEFINE_FIELD( CNPC_CeilingTurret, m_pEyeGlow,		FIELD_CLASSPTR ),

	DEFINE_THINKFUNC( CNPC_CeilingTurret, Retire ),
	DEFINE_THINKFUNC( CNPC_CeilingTurret, Deploy ),
	DEFINE_THINKFUNC( CNPC_CeilingTurret, ActiveThink ),
	DEFINE_THINKFUNC( CNPC_CeilingTurret, SearchThink ),
	DEFINE_THINKFUNC( CNPC_CeilingTurret, AutoSearchThink ),
	DEFINE_THINKFUNC( CNPC_CeilingTurret, DeathThink ),

	// Inputs
	DEFINE_INPUTFUNC( CNPC_CeilingTurret, FIELD_VOID, "Toggle", InputToggle ),
	DEFINE_INPUTFUNC( CNPC_CeilingTurret, FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( CNPC_CeilingTurret, FIELD_VOID, "Disable", InputDisable ),

	DEFINE_OUTPUT( CNPC_CeilingTurret, m_OnDeploy, "OnDeploy" ),
	DEFINE_OUTPUT( CNPC_CeilingTurret, m_OnRetire, "OnRetire" ),
	DEFINE_OUTPUT( CNPC_CeilingTurret, m_OnTipped, "OnTipped" ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( npc_turret_ceiling, CNPC_CeilingTurret );

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CNPC_CeilingTurret::CNPC_CeilingTurret( void )
{
	m_bActive			= false;
	m_pEyeGlow			= NULL;
	m_iAmmoType			= -1;
	m_iMinHealthDmg		= 0;
	m_bAutoStart		= false;
	m_flPingTime		= 0;
	m_flShotTime		= 0;
	m_flLastSight		= 0;
	m_bBlinkState		= false;
	m_bEnabled			= false;

	m_vecGoalAngles.Init();
}

CNPC_CeilingTurret::~CNPC_CeilingTurret( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: Precache
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::Precache( void )
{
	engine->PrecacheModel( CEILING_TURRET_MODEL );	
	engine->PrecacheModel( CEILING_TURRET_GLOW_SPRITE );

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: Spawn the entity
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::Spawn( void )
{ 
	Precache();

	SetModel( CEILING_TURRET_MODEL );
	
	BaseClass::Spawn();

	m_HackedGunPos	= Vector( 0, 0, 12.75 );
	SetViewOffset( EyeOffset( ACT_IDLE ) );
	m_flFieldOfView	= 0.0f;
	m_takedamage	= DAMAGE_YES;
	m_iHealth		= 1000;
	m_bloodColor	= BLOOD_COLOR_MECH;
	
	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_NOT_STANDABLE );

	SetHeight( CEILING_TURRET_RETRACT_HEIGHT );

	AddFlag( FL_AIMTARGET );

	SetPoseParameter( CEILING_TURRET_BC_YAW, 0 );
	SetPoseParameter( CEILING_TURRET_BC_PITCH, 0 );

	m_iAmmoType = GetAmmoDef()->Index( "MediumRound" );

	//Create our eye sprite
	m_pEyeGlow = CSprite::SpriteCreate( CEILING_TURRET_GLOW_SPRITE, GetLocalOrigin(), false );
	m_pEyeGlow->SetTransparency( kRenderTransAdd, 255, 0, 0, 128, kRenderFxNoDissipation );
	m_pEyeGlow->SetAttachment( this, 2 );

	//Set our autostart state
	m_bAutoStart = !!( m_spawnflags & SF_CEILING_TURRET_AUTOACTIVATE );
	m_bEnabled	 = ( ( m_spawnflags & SF_CEILING_TURRET_STARTINACTIVE ) == false );

	//Do we start active?
	if ( m_bAutoStart && m_bEnabled )
	{
		SetThink( AutoSearchThink );
		SetEyeState( TURRET_EYE_DORMANT );
	}
	else
	{
		SetEyeState( TURRET_EYE_DISABLED );
	}

	//Stagger our starting times
	SetNextThink( gpGlobals->curtime + random->RandomFloat( 0.1f, 0.3f ) );

	// Activities
	ADD_CUSTOM_ACTIVITY( CNPC_CeilingTurret, ACT_CEILING_TURRET_OPEN );
	ADD_CUSTOM_ACTIVITY( CNPC_CeilingTurret, ACT_CEILING_TURRET_CLOSE );
	ADD_CUSTOM_ACTIVITY( CNPC_CeilingTurret, ACT_CEILING_TURRET_CLOSED_IDLE );
	ADD_CUSTOM_ACTIVITY( CNPC_CeilingTurret, ACT_CEILING_TURRET_OPEN_IDLE );
	ADD_CUSTOM_ACTIVITY( CNPC_CeilingTurret, ACT_CEILING_TURRET_FIRE );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CNPC_CeilingTurret::OnTakeDamage( const CTakeDamageInfo &inputInfo )
{
	if ( !m_takedamage )
		return 0;

	CTakeDamageInfo info = inputInfo;

	if ( m_bActive == false )
		info.ScaleDamage( 0.1f );

	// If attacker can't do at least the min required damage to us, don't take any damage from them
	if ( info.GetDamage() < m_iMinHealthDmg )
		return 0;

	m_iHealth -= info.GetDamage();

	if ( m_iHealth <= 0 )
	{
		m_iHealth = 0;
		m_takedamage = DAMAGE_NO;

		RemoveFlag( FL_NPC ); // why are they set in the first place???

		//FIXME: This needs to throw a ragdoll gib or something other than animating the retraction -- jdw

		ExplosionCreate( GetAbsOrigin(), GetLocalAngles(), this, 100, 100, false );
		SetThink( DeathThink );

		StopSound( "NPC_CeilingTurret.Alert" );

		m_OnDamaged.FireOutput( info.GetInflictor(), this );

		SetNextThink( gpGlobals->curtime + 0.1f );

		return 0;
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: Retract and stop attacking
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::Retire( void )
{
	if ( PreThink( TURRET_RETIRING ) )
		return;

	//Level out the turret
	m_vecGoalAngles = GetAbsAngles();
	SetNextThink( gpGlobals->curtime );

	//Set ourselves to close
	if ( GetActivity() != ACT_CEILING_TURRET_CLOSE )
	{
		//Set our visible state to dormant
		SetEyeState( TURRET_EYE_DORMANT );

		SetActivity( (Activity) ACT_CEILING_TURRET_OPEN_IDLE );
		
		//If we're done moving to our desired facing, close up
		if ( UpdateFacing() == false )
		{
			SetActivity( (Activity) ACT_CEILING_TURRET_CLOSE );
			EmitSound( "NPC_CeilingTurret.Retire" );

			//Notify of the retraction
			m_OnRetire.FireOutput( NULL, this );
		}
	}
	else if ( IsActivityFinished() )
	{	
		SetHeight( CEILING_TURRET_RETRACT_HEIGHT );

		m_bActive		= false;
		m_flLastSight	= 0;

		SetActivity( (Activity) ACT_CEILING_TURRET_CLOSED_IDLE );

		//Go back to auto searching
		if ( m_bAutoStart )
		{
			SetThink( AutoSearchThink );
			SetNextThink( gpGlobals->curtime + 0.05f );
		}
		else
		{
			//Set our visible state to dormant
			SetEyeState( TURRET_EYE_DISABLED );
			SetThink( SUB_DoNothing );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Deploy and start attacking
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::Deploy( void )
{
	if ( PreThink( TURRET_DEPLOYING ) )
		return;

	m_vecGoalAngles = GetAbsAngles();

	SetNextThink( gpGlobals->curtime );

	//Show we've seen a target
	SetEyeState( TURRET_EYE_SEE_TARGET );

	//Open if we're not already
	if ( GetActivity() != ACT_CEILING_TURRET_OPEN )
	{
		m_bActive = true;
		SetActivity( (Activity) ACT_CEILING_TURRET_OPEN );
		EmitSound( "NPC_CeilingTurret.Deploy" );

		//Notify we're deploying
		m_OnDeploy.FireOutput( NULL, this );
	}

	//If we're done, then start searching
	if ( IsActivityFinished() )
	{
		SetHeight( CEILING_TURRET_DEPLOY_HEIGHT );

		SetActivity( (Activity) ACT_CEILING_TURRET_OPEN_IDLE );

		m_flShotTime  = gpGlobals->curtime + 1.0f;

		m_flPlaybackRate = 0;
		SetThink( SearchThink );

		EmitSound( "NPC_CeilingTurret.Move" );
	}

	m_flLastSight = gpGlobals->curtime + CEILING_TURRET_MAX_WAIT;	
}

//-----------------------------------------------------------------------------
// Purpose: Returns the speed at which the turret can face a target
//-----------------------------------------------------------------------------
float CNPC_CeilingTurret::MaxYawSpeed( void )
{
	//TODO: Scale by difficulty?
	return 360.0f;
}

//-----------------------------------------------------------------------------
// Purpose: Causes the turret to face its desired angles
//-----------------------------------------------------------------------------
bool CNPC_CeilingTurret::UpdateFacing( void )
{
	bool  bMoved = false;
	matrix3x4_t localToWorld;
	
	GetAttachment( LookupAttachment( "eyes" ), localToWorld );

	Vector vecGoalDir;
	AngleVectors( m_vecGoalAngles, &vecGoalDir );

	Vector vecGoalLocalDir;
	VectorIRotate( vecGoalDir, localToWorld, vecGoalLocalDir );

	if ( g_debug_turret_ceiling.GetBool() )
	{
		Vector	vecMuzzle, vecMuzzleDir;
		QAngle	vecMuzzleAng;

		GetAttachment( "eyes", vecMuzzle, vecMuzzleAng );
		AngleVectors( vecMuzzleAng, &vecMuzzleDir );

		NDebugOverlay::Cross3D( vecMuzzle, -Vector(2,2,2), Vector(2,2,2), 255, 255, 0, false, 0.05 );
		NDebugOverlay::Cross3D( vecMuzzle+(vecMuzzleDir*256), -Vector(2,2,2), Vector(2,2,2), 255, 255, 0, false, 0.05 );
		NDebugOverlay::Line( vecMuzzle, vecMuzzle+(vecMuzzleDir*256), 255, 255, 0, false, 0.05 );
		
		NDebugOverlay::Cross3D( vecMuzzle, -Vector(2,2,2), Vector(2,2,2), 255, 0, 0, false, 0.05 );
		NDebugOverlay::Cross3D( vecMuzzle+(vecGoalDir*256), -Vector(2,2,2), Vector(2,2,2), 255, 0, 0, false, 0.05 );
		NDebugOverlay::Line( vecMuzzle, vecMuzzle+(vecGoalDir*256), 255, 0, 0, false, 0.05 );
	}

	QAngle vecGoalLocalAngles;
	VectorAngles( vecGoalLocalDir, vecGoalLocalAngles );

	// Update pitch
	float flDiff = AngleNormalize( UTIL_ApproachAngle(  vecGoalLocalAngles.x, 0.0, 0.1f * MaxYawSpeed() ) );
	
	int iPose = LookupPoseParameter( CEILING_TURRET_BC_PITCH );
	SetPoseParameter( iPose, GetPoseParameter( iPose ) + ( flDiff / 1.5f ) );

	if ( fabs( flDiff ) > 0.1f )
	{
		bMoved = true;
	}

	// Update yaw
	flDiff = AngleNormalize( UTIL_ApproachAngle(  vecGoalLocalAngles.y, 0.0, 0.1f * MaxYawSpeed() ) );

	iPose = LookupPoseParameter( CEILING_TURRET_BC_YAW );
	SetPoseParameter( iPose, GetPoseParameter( iPose ) + ( flDiff / 1.5f ) );

	if ( fabs( flDiff ) > 0.1f )
	{
		bMoved = true;
	}

	return bMoved;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_CeilingTurret::FVisible( CBaseEntity *pEntity, int traceMask, CBaseEntity **ppBlocker )
{
	if ( pEntity->GetFlags() & FL_NOTARGET )
		return false;

	Vector vecLookerOrigin = EyePosition();
	Vector vecTargetOrigin = pEntity->EyePosition();

	trace_t tr;
	AI_TraceLine( vecLookerOrigin, vecTargetOrigin, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );
	
	//Succeeded
	if ( tr.fraction == 1.0f )
		return true;

	CBaseEntity	*pHitEntity = tr.m_pEnt;
	
	// if player is in a vehicle, that's an acceptible thing to hit
	CBasePlayer *pPlayer = ToBasePlayer( UTIL_PlayerByIndex( 1 ) );
	// is player in a vehicle? if so, verify vehicle is target and return if so (so npc shoots at vehicle)
	if ( pPlayer && pPlayer->IsInAVehicle() )
	{
		// ok, player in vehicle, check if vehicle is target we're looking at, fire if it is
		CBaseEntity	*pVehicle  = pPlayer->GetVehicle()->GetVehicleEnt();
		if ( pHitEntity == pVehicle )
			return true;
	}

	//Hit our target
	if ( pHitEntity == pEntity )
		return true;

	//If we hit something that's okay to hit anyway, still fire
	if ( pHitEntity && pHitEntity->MyCombatCharacterPointer() )
	{
		if ( IRelationType( pHitEntity ) == D_HT )
			return true;
	}

	if (ppBlocker)
	{
		*ppBlocker = pHitEntity;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Allows the turret to fire on targets if they're visible
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::ActiveThink( void )
{
	//Allow descended classes a chance to do something before the think function
	if ( PreThink( TURRET_ACTIVE ) )
		return;

	//Update our think time
	SetNextThink( gpGlobals->curtime + 0.1f );

	//If we've become inactive, go back to searching
	if ( ( m_bActive == false ) || ( GetEnemy() == NULL ) )
	{
		SetEnemy( NULL );
		m_flLastSight = gpGlobals->curtime + CEILING_TURRET_MAX_WAIT;
		SetThink( SearchThink );
		m_vecGoalAngles = GetAbsAngles();
		return;
	}
	
	//Get our shot positions
	Vector vecMid = EyePosition();
	Vector vecMidEnemy = GetEnemy()->BodyTarget( vecMid );

	//Look for our current enemy
	bool bEnemyVisible = FInViewCone( GetEnemy() ) && FVisible( GetEnemy() ) && GetEnemy()->IsAlive();

	//Calculate dir and dist to enemy
	Vector	vecDirToEnemy = vecMidEnemy - vecMid;	
	float	flDistToEnemy = VectorNormalize( vecDirToEnemy );

	//We want to look at the enemy's eyes so we don't jitter
	Vector	vecDirToEnemyEyes = GetEnemy()->WorldSpaceCenter() - vecMid;
	VectorNormalize( vecDirToEnemyEyes );

	QAngle vecAnglesToEnemy;
	VectorAngles( vecDirToEnemyEyes, vecAnglesToEnemy );

	//Draw debug info
	if ( g_debug_turret_ceiling.GetBool() )
	{
		NDebugOverlay::Cross3D( vecMid, -Vector(2,2,2), Vector(2,2,2), 0, 255, 0, false, 0.05 );
		NDebugOverlay::Cross3D( GetEnemy()->WorldSpaceCenter(), -Vector(2,2,2), Vector(2,2,2), 0, 255, 0, false, 0.05 );
		NDebugOverlay::Line( vecMid, GetEnemy()->WorldSpaceCenter(), 0, 255, 0, false, 0.05 );

		NDebugOverlay::Cross3D( vecMid, -Vector(2,2,2), Vector(2,2,2), 0, 255, 0, false, 0.05 );
		NDebugOverlay::Cross3D( vecMidEnemy, -Vector(2,2,2), Vector(2,2,2), 0, 255, 0, false, 0.05 );
		NDebugOverlay::Line( vecMid, vecMidEnemy, 0, 255, 0, false, 0.05f );
	}

	//Current enemy is not visible
	if ( ( bEnemyVisible == false ) || ( flDistToEnemy > CEILING_TURRET_RANGE ))
	{
		if ( m_flLastSight )
		{
			m_flLastSight = gpGlobals->curtime + 0.5f;
		}
		else if ( gpGlobals->curtime > m_flLastSight )
		{
			// Should we look for a new target?
			ClearEnemyMemory();
			SetEnemy( NULL );
			m_flLastSight = gpGlobals->curtime + CEILING_TURRET_MAX_WAIT;
			SetThink( SearchThink );
			m_vecGoalAngles = GetAbsAngles();
			
			SpinDown();

			return;
		}

		bEnemyVisible = false;
	}

	Vector vecMuzzle, vecMuzzleDir;
	QAngle vecMuzzleAng;
	
	GetAttachment( "eyes", vecMuzzle, vecMuzzleAng );
	AngleVectors( vecMuzzleAng, &vecMuzzleDir );
	
	if ( m_flShotTime < gpGlobals->curtime )
	{
		//Fire the gun
		if ( DotProduct( vecDirToEnemy, vecMuzzleDir ) >= 0.9848 ) // 10 degree slop
		{
			SetActivity( ACT_RESET );
			SetActivity( (Activity) ACT_CEILING_TURRET_FIRE );
			
			//Fire the weapon
			Shoot( vecMuzzle, vecMuzzleDir );
		} 
	}
	else
	{
		SetActivity( (Activity) ACT_CEILING_TURRET_OPEN_IDLE );
	}

	//If we can see our enemy, face it
	if ( bEnemyVisible )
	{
		m_vecGoalAngles.y = vecAnglesToEnemy.y;
		m_vecGoalAngles.x = vecAnglesToEnemy.x;
	}

	//Turn to face
	UpdateFacing();
}

//-----------------------------------------------------------------------------
// Purpose: Target doesn't exist or has eluded us, so search for one
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::SearchThink( void )
{
	//Allow descended classes a chance to do something before the think function
	if ( PreThink( TURRET_SEARCHING ) )
		return;

	SetNextThink( gpGlobals->curtime + 0.05f );

	SetActivity( (Activity) ACT_CEILING_TURRET_OPEN_IDLE );

	//If our enemy has died, pick a new enemy
	if ( ( GetEnemy() != NULL ) && ( GetEnemy()->IsAlive() == false ) )
	{
		SetEnemy( NULL );
	}

	//Acquire the target
	if ( GetEnemy() == NULL )
	{
		GetSenses()->Look( CEILING_TURRET_RANGE );
		SetEnemy( BestEnemy() );
	}

	//If we've found a target, spin up the barrel and start to attack
	if ( GetEnemy() != NULL )
	{
		//Give players a grace period
		if ( GetEnemy()->IsPlayer() )
		{
			m_flShotTime  = gpGlobals->curtime + 0.5f;
		}
		else
		{
			m_flShotTime  = gpGlobals->curtime + 0.1f;
		}

		m_flLastSight = 0;
		SetThink( ActiveThink );
		SetEyeState( TURRET_EYE_SEE_TARGET );

		SpinUp();
		EmitSound( "NPC_CeilingTurret.Active" );
		return;
	}

	//Are we out of time and need to retract?
 	if ( gpGlobals->curtime > m_flLastSight )
	{
		//Before we retrace, make sure that we are spun down.
		m_flLastSight = 0;
		SetThink( Retire );
		return;
	}
	
	//Display that we're scanning
	m_vecGoalAngles.x = 15.0f;
	m_vecGoalAngles.y = GetAbsAngles().y + ( sin( gpGlobals->curtime * 2.0f ) * 45.0f );

	//Turn and ping
	UpdateFacing();
	Ping();
}

//-----------------------------------------------------------------------------
// Purpose: Watch for a target to wander into our view
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::AutoSearchThink( void )
{
	//Allow descended classes a chance to do something before the think function
	if ( PreThink( TURRET_AUTO_SEARCHING ) )
		return;

	//Spread out our thinking
	SetNextThink( gpGlobals->curtime + random->RandomFloat( 0.2f, 0.4f ) );

	//If the enemy is dead, find a new one
	if ( ( GetEnemy() != NULL ) && ( GetEnemy()->IsAlive() == false ) )
	{
		SetEnemy( NULL );
	}

	//Acquire Target
	if ( GetEnemy() == NULL )
	{
		GetSenses()->Look( CEILING_TURRET_RANGE );
		SetEnemy( BestEnemy() );
	}

	//Deploy if we've got an active target
	if ( GetEnemy() != NULL )
	{
		SetThink( Deploy );
		EmitSound( "NPC_CeilingTurret.Alert" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Fire!
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::Shoot( const Vector &vecSrc, const Vector &vecDirToEnemy )
{
	if ( GetEnemy() != NULL )
	{
		Vector vecDir = GetActualShootTrajectory( vecSrc );

		FireBullets( 1, vecSrc, vecDir, VECTOR_CONE_PRECALCULATED, MAX_COORD_RANGE, m_iAmmoType, 1, -1, -1, 5, NULL );
		EmitSound( "NPC_CeilingTurret.Shoot" );
		m_fEffects |= EF_MUZZLEFLASH;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Allows a generic think function before the others are called
// Input  : state - which state the turret is currently in
//-----------------------------------------------------------------------------
bool CNPC_CeilingTurret::PreThink( turretState_e state )
{
	//Animate
	StudioFrameAdvance();

	//Do not interrupt current think function
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Sets the state of the glowing eye attached to the turret
// Input  : state - state the eye should be in
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::SetEyeState( eyeState_t state )
{
	//Must have a valid eye to affect
	if ( m_pEyeGlow == NULL )
		return;

	//Set the state
	switch( state )
	{
	default:
	case TURRET_EYE_SEE_TARGET: //Fade in and scale up
		m_pEyeGlow->SetColor( 255, 0, 0 );
		m_pEyeGlow->SetBrightness( 164, 0.1f );
		m_pEyeGlow->SetScale( 0.4f, 0.1f );
		break;

	case TURRET_EYE_SEEKING_TARGET: //Ping-pongs
		
		//Toggle our state
		m_bBlinkState = !m_bBlinkState;
		m_pEyeGlow->SetColor( 255, 128, 0 );

		if ( m_bBlinkState )
		{
			//Fade up and scale up
			m_pEyeGlow->SetScale( 0.25f, 0.1f );
			m_pEyeGlow->SetBrightness( 164, 0.1f );
		}
		else
		{
			//Fade down and scale down
			m_pEyeGlow->SetScale( 0.2f, 0.1f );
			m_pEyeGlow->SetBrightness( 64, 0.1f );
		}

		break;

	case TURRET_EYE_DORMANT: //Fade out and scale down
		m_pEyeGlow->SetColor( 0, 255, 0 );
		m_pEyeGlow->SetScale( 0.1f, 0.5f );
		m_pEyeGlow->SetBrightness( 64, 0.5f );
		break;

	case TURRET_EYE_DEAD: //Fade out slowly
		m_pEyeGlow->SetColor( 255, 0, 0 );
		m_pEyeGlow->SetScale( 0.1f, 3.0f );
		m_pEyeGlow->SetBrightness( 0, 3.0f );
		break;

	case TURRET_EYE_DISABLED:
		m_pEyeGlow->SetColor( 0, 255, 0 );
		m_pEyeGlow->SetScale( 0.1f, 1.0f );
		m_pEyeGlow->SetBrightness( 0, 1.0f );
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Make a pinging noise so the player knows where we are
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::Ping( void )
{
	//See if it's time to ping again
	if ( m_flPingTime > gpGlobals->curtime )
		return;

	//Ping!
	EmitSound( "NPC_CeilingTurret.Ping" );

	SetEyeState( TURRET_EYE_SEEKING_TARGET );

	m_flPingTime = gpGlobals->curtime + CEILING_TURRET_PING_TIME;
}

//-----------------------------------------------------------------------------
// Purpose: Toggle the turret's state
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::Toggle( void )
{
	//Toggle the state
	if ( m_bEnabled )
	{
		Disable();
	}
	else 
	{
		Enable();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Enable the turret and deploy
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::Enable( void )
{
	m_bEnabled = true;

	// if the turret is flagged as an autoactivate turret, re-enable its ability open self.
	if ( m_spawnflags & SF_CEILING_TURRET_AUTOACTIVATE )
	{
		m_bAutoStart = true;
	}

	SetThink( Deploy );
	SetNextThink( gpGlobals->curtime + 0.05f );
}

//-----------------------------------------------------------------------------
// Purpose: Retire the turret until enabled again
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::Disable( void )
{
	m_bEnabled = false;
	m_bAutoStart = false;

	SetEnemy( NULL );
	SetThink( Retire );
	SetNextThink( gpGlobals->curtime + 0.1f );
}

//-----------------------------------------------------------------------------
// Purpose: Toggle the turret's state via input function
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::InputToggle( inputdata_t &inputdata )
{
	Toggle();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::InputEnable( inputdata_t &inputdata )
{
	Enable();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::InputDisable( inputdata_t &inputdata )
{
	Disable();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::SpinUp( void )
{
}

#define	CEILING_TURRET_MIN_SPIN_DOWN	1.0f

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::SpinDown( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::DeathThink( void )
{
	if ( PreThink( TURRET_DEAD ) )
		return;

	//Level out our angles
	m_vecGoalAngles = GetAbsAngles();
	SetNextThink( gpGlobals->curtime );

	if ( m_lifeState != LIFE_DEAD )
	{
		m_lifeState = LIFE_DEAD;

		EmitSound( "NPC_CeilingTurret.Die" );

		SetActivity( (Activity) ACT_CEILING_TURRET_CLOSE );
	}

	// lots of smoke
	Vector pos(	random->RandomFloat( GetAbsMins().x, GetAbsMaxs().x ),
				random->RandomFloat( GetAbsMins().y, GetAbsMaxs().y ),
				random->RandomFloat( GetAbsMins().z, GetAbsMaxs().z ) );
	
	CBroadcastRecipientFilter filter;
	
	te->Smoke( filter, 0.0, &pos, g_sModelIndexSmoke, 2.5, 10 );
	
	g_pEffects->Sparks( pos );

	if ( IsActivityFinished() && ( UpdateFacing() == false ) )
	{
		SetHeight( CEILING_TURRET_RETRACT_HEIGHT );

		m_flPlaybackRate = 0;
		SetThink( NULL );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : height - 
//-----------------------------------------------------------------------------
void CNPC_CeilingTurret::SetHeight( float height )
{
	Vector forward, right, up;
	AngleVectors( GetLocalAngles(), &forward, &right, &up );

	Vector mins = ( forward * -16.0f ) + ( right * -16.0f );
	Vector maxs = ( forward *  16.0f ) + ( right *  16.0f ) + ( up * -height );

	if ( mins.x > maxs.x )
	{
		swap( mins.x, maxs.x );
	}

	if ( mins.y > maxs.y )
	{
		swap( mins.y, maxs.y );
	}

	if ( mins.z > maxs.z )
	{
		swap( mins.z, maxs.z );
	}

	SetCollisionBounds( mins, maxs );
	Relink();
}

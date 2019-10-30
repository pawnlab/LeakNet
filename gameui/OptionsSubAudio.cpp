//========= Copyright � 1996-2003, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
//=============================================================================

#include "OptionsSubAudio.h"

#include "CvarToggleCheckButton.h"
#include "CvarSlider.h"
#include "EngineInterface.h"
#include "LabeledCommandComboBox.h"
#include "ModInfo.h"

#include <KeyValues.h>
// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

using namespace vgui;

COptionsSubAudio::COptionsSubAudio(vgui::Panel *parent) : PropertyPage(parent, NULL)
{
	m_pSFXSlider = new CCvarSlider( this, "SFX Slider", "#GameUI_SoundEffectVolume",
		0.0f, 1.0f, "volume");

	m_pHEVSlider = new CCvarSlider( this, "Suit Slider", "#GameUI_HEVSuitVolume",
		0.0f, 1.0f, "suitvolume");

	m_pSoundQualityCombo = new CLabeledCommandComboBox( this, "Sound Quality" );

	//!! bug no command given
	m_pSoundQualityCombo->AddItem( "#GameUI_High", "hisound 1" );
	m_pSoundQualityCombo->AddItem( "#GameUI_Low", "hisound 0" );
//	m_pSoundQualityCombo->SetInitialItem( engine->pfnGetCvarFloat( "hisound" ) != 0.0f ? 0 : 1 );
	ConVar *var = (ConVar *)cvar->FindVar( "hisound" );
	if ( var )
	{
		m_pSoundQualityCombo->SetInitialItem( var->GetBool() != 0 ? 0: 1 );
	}
		


	LoadControlSettings("Resource\\OptionsSubAudio.res");

	// override, hide the HEV suit volume when not in half-life
	if (ModInfo().IsMultiplayerOnly())
	{
		Panel *child = FindChildByName("suit label");
		if (child)
		{
			child->SetVisible(false);
		}
		child = FindChildByName("Suit Slider");
		if (child)
		{
			child->SetVisible(false);
		}
	}
}

COptionsSubAudio::~COptionsSubAudio()
{
}

void COptionsSubAudio::OnResetData()
{
	m_pSFXSlider->Reset();
	m_pHEVSlider->Reset();
	m_pSoundQualityCombo->Reset();
}

void COptionsSubAudio::OnApplyChanges()
{
	m_pSFXSlider->ApplyChanges();
	m_pHEVSlider->ApplyChanges();
	m_pSoundQualityCombo->ApplyChanges();
}

void COptionsSubAudio::OnControlModified()
{
	PostActionSignal(new KeyValues("ApplyButtonEnable"));
}

//-----------------------------------------------------------------------------
// Purpose: Message map
//-----------------------------------------------------------------------------
MessageMapItem_t COptionsSubAudio::m_MessageMap[] =
{
	MAP_MESSAGE( COptionsSubAudio, "ControlModified", OnControlModified),
};

IMPLEMENT_PANELMAP(COptionsSubAudio, BaseClass);
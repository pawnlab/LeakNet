//========= Copyright � 1996-2003, Valve LLC, All rights reserved. ============
//
// The copyright to the contents herein is the property of Valve, L.L.C.
// The contents may be used and/or copied only with the written permission of
// Valve, L.L.C., or in accordance with the terms and conditions stipulated in
// the agreement/contract under which the contents have been supplied.
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#ifndef MENUITEM_H
#define MENUITEM_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui/VGUI.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/Menu.h>

namespace vgui
{

class IBorder;
class TextImage;
class Menu;
class Image;

//-----------------------------------------------------------------------------
// Purpose: The items in a menu
// MenuItems MUST have the Menu class as parents.
//-----------------------------------------------------------------------------
class MenuItem : public Button
{
public:
	MenuItem(Panel *parent, const char *panelName, const char *text, Menu *cascadeMenu = NULL, bool checkable = false);
	MenuItem(Panel *parent, const char *panelName, const wchar_t *wszText, Menu *cascadeMenu = NULL, bool checkable = false);
	~MenuItem();

	// Activate the menu item as if it had been selected by the user
	virtual void FireActionSignal();

    virtual bool CanBeDefaultButton(void);

	// Handle mouse cursor entering a MenuItem.
	void OnCursorEntered();
	// Handle mouse cursor exiting a MenuItem. 
	void OnCursorExited();

	// Close the cascading menu if we have one.
	void CloseCascadeMenu();

	// Pass kill focus events up to parent on loss of focus
	virtual void OnKillFocus();

	// Return true if this item triggers a cascading menu
	bool HasMenu();

	// Set the size of the text portion of the label.
	void SetTextImageSize(int wide, int tall);

	//Return the size of the text portion of the label.
	void GetTextImageSize(int &wide, int &tall);

	// Return the size of the arrow portion of the label.
	void GetArrowImageSize(int &wide, int &tall);

	// Return the size of the check portion of the label.
	void GetCheckImageSize(int &wide, int &tall);

	// Return the menu that this menuItem contains
	Menu *GetMenu();

	virtual void PerformLayout();

	// Respond to cursor movement
	void OnCursorMoved(int x, int y);

	// Highlight item
	void ArmItem();
	// Unhighlight item.
	void DisarmItem();

	// is the item highlighted?
	bool IsItemArmed();

	// Open cascading menu if there is one.
	void OpenCascadeMenu();

	bool IsCheckable();
	bool IsChecked();

	// Set a checkable menuItem checked or unchecked.
	void SetChecked(bool state);

	KeyValues *GetUserData();
	void SetUserData(KeyValues *kv);

	int GetActiveItem() { if ( m_pCascadeMenu ) { return m_pCascadeMenu->GetActiveItem(); } else { return 0; }} 

protected:
	void OnKeyCodeReleased(KeyCode code);
	void OnMenuClose();
	void OnKeyModeSet();

	// vgui overrides
	virtual void Init( void );
	virtual void ApplySchemeSettings(IScheme *pScheme);
	virtual IBorder *GetBorder(bool depressed, bool armed, bool selected, bool keyfocus);

	DECLARE_PANELMAP();

private:
	enum { CHECK_INSET = 6 };
	Menu *m_pCascadeMenu;  // menu triggered to open upon selecting this menu item
 	bool m_bCheckable;     // can this menu item have a little check to the left of it when you select it?
	bool m_bChecked;       // whether item is checked or not.
	TextImage *m_pCascadeArrow; // little arrow that appears to the right of menuitems that open a menu
	Image *m_pCheck;  // the check that appears to the left of checked menu items
	TextImage *m_pBlankCheck;  // a blank image same size as the check for when items are not checked.

	KeyValues *m_pUserData;

};

} // namespace vgui

#endif // MENUITEM_H

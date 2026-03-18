#ifndef _3DSMENU_H_
#define _3DSMENU_H_

#include <functional>
#include <string>
#include <vector>

#include "3dsthemes.h"


#define MENU_PREFIX_FILE "  "
#define MENU_PREFIX_CHILD_DIRECTORY "  \x01 "
#define MENU_PREFIX_PARENT_DIRECTORY ""

#define MENU_HEIGHT             (14)
#define DIALOG_HEIGHT           (5)

typedef struct 
{
    const char* label;
    const char* icon;
    uint32 color;
} MenuButton;

// currently used for save states
typedef enum
{
    RADIO_INACTIVE = 0,
    RADIO_INACTIVE_CHECKED = 1,
    RADIO_ACTIVE = 2,
	RADIO_ACTIVE_CHECKED = 3,
}radio_state;

enum class FileMenuOption {
    None,
    SetDefaultDir,
    ResetDefaultDir,
    RandomGame,
    RescanDir,
    DeleteGame
};

enum class MenuItemType {
    Disabled,
    Header1,
    Header2,
    Textarea, // for now this shouldn't be used when other menu items are following (menuStartY value has to be adjusted afterwards)
    Action,
    Checkbox,
    Radio,
    Gauge,
    Picker
};

class SMenuItem {
public:
    MenuItemType Type;

    std::string Text;

    std::string Description;

    int     Value;              
                                // Type = Gauge:
                                //   Value = Gauge Value
                                // Type = Checkbox:
                                //   0, unchecked
                                //   1, checked
                                // Type = Radio: (see enum radio_state)
                                //   0, unchecked and inactive
                                //   1, checked and inactive
                                //   2, unchecked and active
                                //   3, checked and active
                                // Type = Picker:
                                //   Selected ID of Picker

    // workaround: we also use GaugeMinValue to determine if a picker should show its selected option in the menu or not.
    int     GaugeMinValue;
    // workaround: we also use GaugeMaxValue to provide picker id
    int     GaugeMaxValue;      // Set GaugeMaxValue to GAUGE_DISABLED_VALUE to make gauge disabled.

    // All these fields are used if this is a picker.
    // (ID = 100000)
    //
    std::string PickerDescription;
    std::vector<SMenuItem> PickerItems;
    int     PickerDialogType;

protected:
    std::function<void(int)> ValueChangedCallback;

public:
    SMenuItem(
        std::function<void(int)> callback,
        MenuItemType type, const std::string& text, const std::string& description, int value = 0,
        int min = 0, int max = 0,
        const std::string& pickerDesc = std::string(), const std::vector<SMenuItem>& pickerItems = std::vector<SMenuItem>(), int pickerDialogType = 0
    ) : ValueChangedCallback(callback), Type(type), Text(text), Description(description), Value(value),
        GaugeMinValue(min), GaugeMaxValue(max),
        PickerDescription(pickerDesc), PickerItems(pickerItems), PickerDialogType(pickerDialogType) {}

    void SetValue(int value) {
        this->Value = value;
        if (this->ValueChangedCallback) {
            this->ValueChangedCallback(value);
        }
    }

    bool IsHighlightable() const {
        return !( Type == MenuItemType::Disabled || Type == MenuItemType::Header1 || Type == MenuItemType::Header2 || Type == MenuItemType::Textarea );
    }
};

class SMenuTab {
public:
    std::vector<SMenuItem> MenuItems;
    std::string SubTitle;
    std::string Title;
    std::string DialogText;
    int         FirstItemIndex;
    int         SelectedItemIndex;

    void SetTitle(const std::string& title) {
        // Left trim the dialog title
        size_t offs = title.find_first_not_of(' ');
        Title.assign(offs != title.npos ? title.substr(offs) : title);
    }

    void MakeSureSelectionIsOnScreen(int maxItems, int spacing) {
        int offs = spacing;
        // the visible item count must fit at least two spacings and one item in the middle for sensible scrolling logic
        if (offs * 2 + 1 >= maxItems) {
            offs = ( maxItems - 1 ) / 2;
        }
        if (SelectedItemIndex < FirstItemIndex + offs) {
            FirstItemIndex = SelectedItemIndex < offs ? 0 : ( SelectedItemIndex - offs );
        } else if (SelectedItemIndex >= FirstItemIndex + maxItems - offs) {
            int top = SelectedItemIndex - maxItems + 1;
            int itemsBelow = static_cast<int>(MenuItems.size()) - SelectedItemIndex - 1;
            FirstItemIndex = itemsBelow < offs ? ( top + itemsBelow ) : ( top + offs );
        }
        
        // FirstItemIndex should not be negative, otherwise it causes a missing item list on scroll
        // (happens e.g. when Load game tab has 12 menu items)
        if (FirstItemIndex < 0) {
            FirstItemIndex = 0;
        }
    }
};

void menu3dsAddTab(std::vector<SMenuTab>& menuTabs, char *title, const std::vector<SMenuItem>& menuItems);

void menu3dsDrawEverything(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, int menuFrame = 0, int menuItemsFrame = 0, int dialogFrame = 0);
void menu3dsDrawEverything(int& currentMenuTab, std::vector<SMenuTab>& menuTabs);
void menu3dsSwapBuffersAndWaitForVBlank();

int menu3dsMenuSelectItem(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs);
void menu3dsHideMenu(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs);

int menu3dsShowDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, const std::string& title, const std::string& dialogText, int dialogBackColor, const std::vector<SMenuItem>& menuItems, int selectedID = -1, bool fadeIn = true);
void menu3dsShowRomLoadingDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, const std::string& title, const std::string& text, int dialogColor);
void menu3dsHideDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, bool fadeOut = true);

int menu3dsGetLastSelectedTabIndex();
void menu3dsSetLastSelectedTabIndex(int index);
void menu3dsSelectRandomGameIndex(SMenuTab& currentTab, int min, int max, int lastSelected);
void menu3dsUpdateGaugeVisibility(SMenuTab *currentTab, int id, int value);

void menu3dsSetScreenDirty(bool gameScreen = true, bool secondScreen = false);
void menu3dsSetRomInfo();
void menu3dsSetHotkeysData(char* hotkeysData[][3]);

void menu3dsSetCheatsCount(SMenuItem& item, int active, int total);

void menu3dsShowSplashMessage(const char *message);

#endif

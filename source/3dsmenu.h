#ifndef _3DSMENU_H_
#define _3DSMENU_H_

#include <functional>
#include <string>
#include <vector>

#include "3dsthemes.h"
#include "3dssettings.h"
#include "3dsglyphs.h"


#define MENU_PREFIX_FILE "  "
#define MENU_PREFIX_PARENT_DIRECTORY ""

#define MENU_ITEM_HEIGHT        (14)
#define MENU_LIST_H_PADDING     (20)
#define ANIMATE_TAB_STEPS       3

#define DIALOG_DEFAULT_ITEMS    5
#define DIALOG_DEFAULT_MAX_LINES 13

enum { TAB_EMULATOR, TAB_SETTINGS, TAB_CONTROLS, TAB_CHEATS, TAB_DIRTY_COUNT };

// Temporary in-tab pages.
enum { 
    SUBPAGE_NONE = 0,
    SUBPAGE_SAVE_LOAD,
    SUBPAGE_ONLINE_UPDATER,
    SUBPAGE_RETRO_ACHIEVEMENTS,
    SUBPAGE_ADVANCED_SETTINGS,
};

enum ButtonVisibility {
    BTN_SHOW_ALWAYS,
    BTN_SHOW_OPTIONS,
};

typedef struct
{
    const char* label;
    UiIcon icon;
    uint32 color;
    ButtonVisibility visibility;
} MenuButton;

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
    Note, // non-selectable info row, for unavailable actions use Disabled
    BodyText, // non-selectable copy
    Header1,
    Header2,
    Action,
    Checkbox,
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
                                // Type = Picker:
                                //   Selected ID of Picker

    // workaround: we also use GaugeMinValue to determine if a picker should show its selected option in the menu or not.
    int     GaugeMinValue;
    int     GaugeMaxValue;

    // All these fields are used if this is a picker.
    // (ID = 100000)
    //
    std::string PickerDescription;
    std::vector<SMenuItem> PickerItems;
    int     PickerDialogType;
    bool    PickerFitDescription = false;
    int     PickerDialogItems = DIALOG_DEFAULT_ITEMS;

    // Drawn in the left gutter while the row is selected; 0 = none.
    char    SelectedMarker = 0;

    bool    Dimmed = false;

protected:
    std::function<void(int)> ValueChangedCallback;

public:
    SMenuItem(
        std::function<void(int)> callback,
        MenuItemType type, const std::string& text, const std::string& description, int value = 0,
        int min = 0, int max = 0,
        const std::string& pickerDesc = std::string(), const std::vector<SMenuItem>& pickerItems = std::vector<SMenuItem>(), int pickerDialogType = 0
    ) : Type(type), Text(text), Description(description), Value(value),
        GaugeMinValue(min), GaugeMaxValue(max),
        PickerDescription(pickerDesc), PickerItems(pickerItems), PickerDialogType(pickerDialogType),
        ValueChangedCallback(callback) {}

    void SetValue(int value) {
        this->Value = value;
        if (this->ValueChangedCallback) {
            this->ValueChangedCallback(value);
        }
    }

    bool IsHighlightable() const {
        return !( Type == MenuItemType::Disabled || Type == MenuItemType::Note || Type == MenuItemType::BodyText || Type == MenuItemType::Header1 || Type == MenuItemType::Header2 );
    }
};

class SMenuTab {
public:
    std::vector<SMenuItem> MenuItems;
    std::string SubTitle;
    std::string SubTitleRight;
    std::string Title;
    std::string DialogText;
    int         FirstItemIndex;
    int         SelectedItemIndex;

    // Optional temporary page state for this tab.
    struct SubPage {
        int  id = SUBPAGE_NONE;
        int  footerHeight = 0;
        std::function<void(int selectedIndex, int footerTop,
                           int footerHeight, int menuItemFrame, int menuBackColor)> drawFooter;
        int parentSelectedIndex = 0;
        int parentFirstItemIndex = 0;
        bool active() const { return id != SUBPAGE_NONE; }
    };
    SubPage     subPage;
    bool        IsSubPage() const { return subPage.active(); }

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

// The file tab is always the last tab. Its index is dynamic.
// 1 with no ROM loaded (Emulator, Load Game), 4 in-game (Emulator, Settings, Controls, Cheats, Load Game).
inline bool menu3dsIsFileTab(int tabIndex, const std::vector<SMenuTab>& menuTabs) {
    return tabIndex == (static_cast<int>(menuTabs.size()) - 1);
}

int menu3dsGetListVisibleItems(const SMenuTab& tab);

void menu3dsDrawEverything(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, int menuItemsFrame = 0, int dialogFrame = 0, bool animationFinished = true);
void menu3dsDrawEverything(int& currentMenuTab, std::vector<SMenuTab>& menuTabs);
void menu3dsSwapBuffersAndWaitForVBlank();
void menu3dsDrawSplash(float fade = 1.0f);

int menu3dsMenuSelectItem(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs);
void menu3dsHideMenu(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs);

// Width of the dialog body text, in pixels.
int menu3dsGetDialogTextWidth();

// Text lines a dialog body needs at the dialog's text width, clamped to maxLines.
int menu3dsGetDialogTextLines(const char *text, int maxLines = DIALOG_DEFAULT_MAX_LINES);

// Dialog options valued by index.
std::vector<SMenuItem> menu3dsMakePickerOptions(const std::vector<std::string>& options, const std::vector<std::string>& descriptions = {});
const std::vector<SMenuItem>& menu3dsMakeOptionsForOk();

const char *menu3dsSubPageBackLabel();

int menu3dsShowDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, const std::string& title, const std::string& dialogText, int dialogBackColor, const std::vector<SMenuItem>& menuItems, int selectedID = -1, bool fadeIn = true, int textLines = -1, int dialogItems = DIALOG_DEFAULT_ITEMS);

void menu3dsRunRomLoadingDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, const std::string& title, const std::string& text, int dialogColor, const char* romName = nullptr);

// Keeps the UI live while waiting for the pending RA request.
void menu3dsWaitForPendingRaRequest(const std::function<void()>& onFrame);
void menu3dsRunBadgeCache(SMenuTab& dialogTab, int currentMenuTab, std::vector<SMenuTab>& menuTabs, const char* romName = nullptr);
// Runs badge caching while the caller renders progress.
void menu3dsRunBadgeDownload(const std::function<void(bool isDownloading, int downloadedCount, int downloadCount)>& onStatus);
void menu3dsHideDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, bool fadeOut = true);

int menu3dsGetLastSelectedTabIndex();
void menu3dsSetLastSelectedTabIndex(int index);
void menu3dsSelectRandomGameIndex(SMenuTab& currentTab, int min, int max, int lastSelected);

void menu3dsSetScreenDirty(bool gameScreen = true, bool secondScreen = false);
void menu3dsSetSnesScreenDirty();

void menu3dsMarkTabDirty(int tab);
void menu3dsMarkAllTabsDirty();
bool menu3dsHasDirtyTabs();

std::string menu3dsGetRomInfo();
void menu3dsSetHotkeysData(const char* hotkeysData[HOTKEYS_COUNT][3]);

void menu3dsUpdateCheatsCount(SMenuTab& tab);
int menu3dsGetCheatsActive();

void menu3dsShowSplashMessage(const char *message);

#endif

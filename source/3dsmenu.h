#ifndef _3DSMENU_H_
#define _3DSMENU_H_

#include <functional>
#include <string>
#include <vector>

#define DIALOGCOLOR_RED     0xEC407A
#define DIALOGCOLOR_GREEN   0x4CAF50
#define DIALOGCOLOR_CYAN    0x0097A7

// currently used for save states
typedef enum
{
    RADIO_INACTIVE = 0,
    RADIO_INACTIVE_CHECKED = 1,
    RADIO_ACTIVE = 2,
	RADIO_ACTIVE_CHECKED = 3,
}radio_state;

enum class MenuItemType {
    Disabled,
    Header1,
    Header2,
    Textarea, // for now this shouldn't be used when other menu items are following (menuStartY value has to be adjusted afterwards)
    Action,
    Checkbox,
    Radio,
    Gauge,
    Picker,
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
    int     PickerBackColor;

protected:
    std::function<void(int)> ValueChangedCallback;

public:
    SMenuItem(
        std::function<void(int)> callback,
        MenuItemType type, const std::string& text, const std::string& description, int value = 0,
        int min = 0, int max = 0,
        const std::string& pickerDesc = std::string(), const std::vector<SMenuItem>& pickerItems = std::vector<SMenuItem>(), int pickerColor = 0
    ) : ValueChangedCallback(callback), Type(type), Text(text), Description(description), Value(value),
        GaugeMinValue(min), GaugeMaxValue(max),
        PickerDescription(pickerDesc), PickerItems(pickerItems), PickerBackColor(pickerColor) {}

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
        // (happens e.g. when SELECT ROM tab has 12 menu items)
        if (FirstItemIndex < 0) {
            FirstItemIndex = 0;
        }
    }
};

void menu3dsSetTransferGameScreen(bool transfer);

void menu3dsAddTab(std::vector<SMenuTab>& menuTab, char *title, const std::vector<SMenuItem>& menuItems);
void menu3dsSetSelectedItemByIndex(SMenuTab& tab, int index);

void menu3dsDrawBlackScreen(float opacity = 1.0f);

int menu3dsShowMenu(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab);
void menu3dsHideMenu(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab);

int menu3dsShowDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab, const std::string& title, const std::string& dialogText, int dialogBackColor, const std::vector<SMenuItem>& menuItems, int selectedID = -1);
void menu3dsHideDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab);

void menu3dsSetLastTabPosition(int currentMenuTab, int index);
void menu3dsGetLastTabPosition(int& currentMenuTab, int& lastItemIndex);
void menu3dsUpdateGaugeVisibility(SMenuTab *currentTab, int id, int value);

bool menu3dsTakeScreenshot(const char *path);
void menu3dsSetFpsInfo(int color, float alpha, char *message);
void menu3dsSetRomInfo();
void menu3dsSetHotkeysData(char* hotkeysData[][3]);

void menu3dsSetCheatsIndicator(std::vector<SMenuItem>& cheatMenu);
void menu3dsSetCurrentPercent(int current, int total);
int menu3dsGetCurrentPercent();
int menu3dsGetLastRomItemIndex();

void menu3dsSetSecondScreenContent(const char *dialogMessage, int dialogBackgroundColor = DIALOGCOLOR_GREEN, float dialogAlpha = 0.85f);


#endif

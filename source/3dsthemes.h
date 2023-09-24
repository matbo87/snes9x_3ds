#ifndef _3DSTHEMES_H_
#define _3DSTHEMES_H_

#include "snes9x.h"

typedef struct 
{
    char *Name;
    uint32 menuTopBarColor;
    uint32 selectedTabTextColor;
    uint32 tabTextColor;
    uint32 selectedTabIndicatorColor;
    uint32 menuBottomBarColor;
    uint32 menuBottomBarTextColor;
    uint32 menuBackColor;
    uint32 selectedItemBackColor;
    uint32 selectedItemTextColor;
    uint32 selectedItemDescriptionTextColor;
    uint32 normalItemTextColor;
    uint32 normalItemDescriptionTextColor;
    uint32 disabledItemTextColor;
    uint32 headerItemTextColor;
    uint32 subtitleTextColor;
    uint32 accentColor;
    uint32 accentUnselectedColor;
    uint32 dialogColorInfo;
    uint32 dialogColorWarn;
    uint32 dialogColorSuccess;
    float dialogTextAlpha;
    float dialogSelectedItemBackAlpha;
} Theme3ds;

#define THEME_DARK_MODE 0
#define THEME_RETROARCH 1
#define THEME_ORIGINAL 2

#define TOTALTHEMECOUNT 3
extern Theme3ds Themes[TOTALTHEMECOUNT];

#endif
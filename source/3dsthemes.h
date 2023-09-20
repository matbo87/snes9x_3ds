#ifndef _3DSTHEMES_H_
#define _3DSTHEMES_H_

#include "snes9x.h"

typedef struct 
{
    char *Name;
    uint32 menuBarColor;
    uint32 menuBottomBarColor;
    uint32 menuBackColor;
    uint32 menuTxtColor;
    uint32 menuTxtUnselectedColor;
    uint32 selectedTabColor;
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

#define TOTALTHEMECOUNT 2
extern Theme3ds Themes[TOTALTHEMECOUNT];

#endif
# Known issues

## Old 3DS performance

[Super FX and SA-1](https://en.wikipedia.org/wiki/List_of_Super_NES_enhancement_chips#List_of_Super_NES_games_with_enhancement_chips) titles are generally too demanding for full speed.
They are only listed below when there is another issue or a relevant New 3DS note.

Games with heavy in-frame palette changes can also struggle, typically racing games with scrolling road effects and/or gradient skies.

## In-frame palette changes

Set `In-Frame Palette Changes` to `Enabled` for the most accurate visuals.
Disabling it improves performance, but some games will show visible glitches. New 3DS uses `Enabled` by default.<br>
The examples below cover only some affected games.

* Top Gear 1 & 2 - bugged road textures (representative of several racers using the per-scanline road palette effect)
* Super Turrican - missing background gradients (representative of several games using HDMA sky/background gradients)
* Mortal Kombat 2 - glitched small character portraits
* Timon & Pumbaa's Jungle Games - GFX glitches on the menu and during gameplay


## Flickering horizontal line
Some games show a thin flickering horizontal line, often at the very top of the screen, sometimes further down. Noticeable in games like Super Metroid or SMW2. This is probably a timing issue. These games are considered fully playable, so they are not listed individually in the table below.

## Audio accuracy
This core is based on **snes9x 1.43**, which uses the older, less accurate SNES APU rather than Blargg's APU, so a few games have inaccurate or glitchy audio.

## Game-specific issues

The tables are based on the [Snes9x for 3DS Compatibility List](http://wiki.gbatemp.net/wiki/Snes9x_for_3DS), narrowed to titles flagged with problems (entries reported as running fine were removed). The original list is partly outdated and not all entries have been re-verified for this fork, so treat it as a starting point, not a guarantee.

Issues can have different causes, including emulator bugs, unsupported SNES accessories, and missing broadcast or CD-era services.

Regions and statuses come from the original list. The notes have been reworded and shortened, with updates where behavior has since changed. Regional duplicates are merged into a single row where the issue is the same.

Testing notes refer to Old 3DS and New 3DS. "Old 3DS" also covers the original 2DS and "New 3DS" covers the New 2DS XL.

Some entries originally reported as "flicker" were also dropped, where the cause is the 3DS display's motion blur - pixel art can smear while the image scrolls (e.g. Super Mario World, also visible on Virtual Console), not an emulation issue. Scaling up with a smooth filter (e.g. 4:3 Fit) makes it less noticeable.

Status follows the original list's definitions:

🟡&nbsp;**Issues** - games that mostly work but with more serious yet playable bugs<br>
🔴&nbsp;**Broken** - games that do not load and have major problems and/or are unplayable, or have consistent lockups.

| Game | Region | Status | Notes |
| --- | --- | --- | --- |
| Accele Bird | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Freezes during the first level. Unplayable. |
| Appleseed | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Stage 1 has heavy background flickering. Full speed, good audio. Playable. |
| Arcade's Greatest Hits - Williams | 🇪🇺 🇺🇸 | 🟡&nbsp;Issues | O3DS & N3DS: All games run at full speed but with no SFX. Playable. |
| Asahi Shinbun Rensai - Katou Ichi-Ni-San Shougi - Shingiryuu | 🇯🇵 | 🟡&nbsp;Issues | O3DS: ~32-35 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Battle Clash | 🇪🇺 🇺🇸 | 🔴&nbsp;Broken | O3DS & N3DS: No Super Scope support. Unplayable. |
| Bazooka Blitzkrieg | 🇺🇸 | 🔴&nbsp;Broken | O3DS & N3DS: No Super Scope support. Unplayable. |
| Clay Fighter - Tournament Edition | 🇺🇸 | 🟡&nbsp;Issues | O3DS: Freezes after a battle despite full speed and good audio. Unplayable. N3DS: full speed, good audio, very playable. |
| Clay Fighter 2: Judgement Clay | 🇪🇺 🇺🇸 | 🔴&nbsp;Broken | O3DS & N3DS: Freezes at the end of a battle. Unplayable. |
| Clue | 🇺🇸 | 🟡&nbsp;Issues | O3DS: ~46-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| College Football USA 97 - The Road to New Orleans | 🇺🇸 | 🟡&nbsp;Issues | O3DS: Exhibition mode ~52-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Daikaijuu Monogatari 2 | 🇯🇵 | 🟡&nbsp;Issues | O3DS: ~41-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Dirt Racer | 🇪🇺 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen after the licence screen. Unplayable. |
| Dirt Trax FX | 🇺🇸 | 🟡&nbsp;Issues | N3DS: 125CC Normal race ~28-33 FPS without frameskip, good audio. Playable. |
| Doom | 🇪🇺 🇯🇵 🇺🇸 | 🟡&nbsp;Issues | N3DS: First stage ~31-43 FPS without frameskip with some audio stuttering. Playable. |
| Dragon Quest 5 - Tenkuu no Hanayome | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Walking around the ship ~55-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Dragonball Z - La Legende Saien | 🇪🇺 | 🟡&nbsp;Issues | O3DS: Battles run full speed with heavy flickering and some glitches, good audio. Playable. N3DS: full speed, good audio, very playable. |
| Dragonball Z - Super Butouden 2 (V1.1) | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Battle ~52-56 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Earth Light - Luna Strike | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen after pressing start on the title screen. Unplayable. |
| Earthworm Jim 2 | 🇪🇺 🇺🇸 | 🟡&nbsp;Issues | O3DS & N3DS: First stage runs full speed with minor audio glitches. Playable. |
| EMIT Vol.1 - Toki no Maigo | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Full speed but no audio (audio CD required). Playable. |
| EMIT Vol.2 - Inochigake no Tabi | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Full speed but no audio (audio CD required). Playable. |
| EMIT Vol.3 - Watashi ni Sayonara wo | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Full speed but no audio (audio CD required). Playable. |
| ESPN Speedworld | 🇺🇸 | 🟡&nbsp;Issues | O3DS: Single race ~44-47 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| ESPN Sunday Night NFL | 🇺🇸 | 🟡&nbsp;Issues | O3DS: ~57-59 FPS without frameskip with many GFX glitches on the field, good audio, playable. N3DS: full speed, many GFX glitches on the field, good audio, very playable. |
| F1 Grand Prix | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Severe speed problems, ~8-59 FPS without frameskip. Unplayable. |
| F1 Pole Position | 🇺🇸 | 🟡&nbsp;Issues | O3DS: World Grand Prix mode ~48-59 FPS without frameskip with some flickering, good audio, playable. N3DS: full speed, some flickering, good audio, playable. |
| Final Fight | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Many glitches on the Capcom logo screen. Stage 1 runs full speed, good audio. Playable. |
| Final Knockout | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Exhibition match ~55-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Final Stretch | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Grand Prix mode ~49-53 FPS without frameskip during races, good audio, very playable. N3DS: full speed, good audio, very playable. |
| First Queen - Ornic Senki | 🇯🇵 | 🟡&nbsp;Issues | O3DS: First level ~56-60 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Front Mission - Gun Hazard | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Intro stage ~55-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| GT Racing | 🇯🇵 | 🟡&nbsp;Issues | O3DS: ~52-53 FPS without frameskip during races, good audio, playable. N3DS: full speed, good audio, very playable. |
| Hayazashi Nidan Morita Shougi 2 | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Freezes after starting a game. Unplayable. |
| Home Improvement | 🇺🇸 | 🟡&nbsp;Issues | O3DS: Set 1-1 ~47-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, playable. |
| Honkaku Mahjong - Tetsuman 2 | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Some glitches when switching menus. Full speed, good audio. Playable. |
| Human Grand Prix 4 - F1 Dream Battle | 🇯🇵 | 🟡&nbsp;Issues | O3DS: World Grand Prix mode ~54-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Irem Major Title | 🇯🇵 | 🟡&nbsp;Issues | O3DS: ~52-59 FPS without frameskip, good audio, very playable. N3DS: full speed, good audio, very playable. |
| Irem Skins Game | 🇺🇸 | 🟡&nbsp;Issues | O3DS: ~52-59 FPS without frameskip, good audio, very playable. N3DS: full speed, good audio, very playable. |
| Jack Nicklaus Golf | 🇺🇸 | 🟡&nbsp;Issues | O3DS: ~43-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Jikkyou Oshaberi Parodius | 🇯🇵 | 🟡&nbsp;Issues | O3DS: First level ~54-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Kishin Douji Zenki - Tenchi Meidou | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen, won't boot. Unplayable. |
| Laser Birdie - Get in the Hole | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Stuck on the yellow caution screen. Unplayable. |
| Last Fighter Twin | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Slight flickering below the status bar while attacking. Full speed, good audio. Playable. |
| Life Fitness Mega Cart | 🇪🇺 | 🔴&nbsp;Broken | O3DS & N3DS: Requires a missing hardware add-on. Unplayable. |
| M.A.C.S. Basic Rifle Simulator | 🇺🇸 | 🔴&nbsp;Broken | O3DS & N3DS: Requires lightgun support. Unplayable. |
| Magic Boy | 🇪🇺 🇺🇸 | 🟡&nbsp;Issues | O3DS & N3DS: Some glitches on the Magic Boy logo. Section 1 runs full speed, good audio. Very playable. |
| Mario & Wario | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Requires a joypad patch. Unplayable. |
| Mario Paint | 🇪🇺 🇺🇸 | 🔴&nbsp;Broken | O3DS & N3DS: Requires a joypad patch. Unplayable. |
| Marko's Magic Football | 🇪🇺 | 🔴&nbsp;Broken | O3DS: Black screen after the Acclaim logo. Unplayable. |
| Mecarobot Golf | 🇺🇸 | 🟡&nbsp;Issues | O3DS & N3DS: Heavy flickering on the golf course. Full speed, good audio. |
| Metal Combat - Falcon's Revenge | 🇪🇺 🇺🇸 | 🔴&nbsp;Broken | O3DS & N3DS: No Super Scope support. Unplayable. |
| Miracle Piano Teaching System | 🇺🇸 | 🔴&nbsp;Broken | O3DS & N3DS: Requires the piano peripheral. Unplayable. |
| Momotarou Dentetsu Happy | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Uses the SPC7110 chip; ~36-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Monopoly (V1.1) | 🇺🇸 | 🟡&nbsp;Issues | O3DS: ~48-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Moryou Senki Madara 2 | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Glitches in the Japanese text bar. Full speed, good audio. Playable. |
| NFL Quarterback Club | 🇪🇺 🇯🇵 🇺🇸 | 🟡&nbsp;Issues | O3DS & N3DS: Title screen sometimes glitched. Matches run full speed, good audio. Playable. |
| Outlander | 🇺🇸 | 🟡&nbsp;Issues | O3DS: ~55-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Pit Fighter | 🇺🇸 | 🟡&nbsp;Issues | O3DS: Battle ~50-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Rocky Rodent | 🇪🇺 🇺🇸 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen, won't boot. Unplayable. |
| Romancing SaGa 3 (V1.1) | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Garbled text on the character select screen. Overworld battles run full speed, good audio. Playable. |
| Secret of Evermore | 🇪🇺 🇺🇸 | 🟡&nbsp;Issues | O3DS & N3DS: Laboratory battle runs full speed (rare dips on the name-select screen) with minor audio noise. Playable. |
| Seijuu Maden - Beasts & Blades | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Heavy slowdown in the intro (~35-59 FPS without frameskip), full speed afterwards, good audio, playable. N3DS: full speed, good audio, very playable. |
| Shin Nippon Pro Wrestling - Chou Senshi in Tokyo Dome | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Glitched title screen. Matches run full speed, good audio. Playable. |
| Sink or Swim | 🇪🇺 🇺🇸 | 🟡&nbsp;Issues | O3DS & N3DS: First level runs full speed with sound issues when jumping. Playable. |
| Star Fox | 🇯🇵 🇺🇸 | 🟡&nbsp;Issues | N3DS: First level almost always full speed without frameskip, good audio. Playable. |
| Star Fox 2 (Snes Mini Dump) | 🇺🇸 | 🟡&nbsp;Issues | N3DS: Planet Titania mission (Normal) ~45-59 FPS without frameskip, good audio. Playable. |
| Star Ocean | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Intro battle ~51-59 FPS without frameskip, good audio with some missing sounds, playable. N3DS: full speed with rare dips, good audio with some missing sounds, playable. |
| Star Ocean no S-DD1/96Mbit [T+Eng 100% 1.0_DeJap] [Hack] | 🇯🇵 | 🔴&nbsp;Broken | Black screen on boot. |
| Stealth | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: First mission runs full speed; the text box flickers heavily, good audio. Playable. |
| Street Fighter Alpha 2 | 🇺🇸 | 🟡&nbsp;Issues | O3DS: Battle ~55-59 FPS without frameskip, good audio, playable. N3DS: full speed with rare dips before battles start, good audio, playable. |
| Stunt Race FX | 🇺🇸 | 🟡&nbsp;Issues | N3DS: Speed Trax race ~42-49 FPS without frameskip during races, good audio. Playable. |
| Sugoro Quest ++ Dicenics | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Heavy flickering on the Mode 7 world map. Full speed, good audio. |
| Super 4WD - The Baja | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Race ~56-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Super Air Diver DSP Loader | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen after boot. Unplayable. |
| Super Black Bass | 🇺🇸 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen after boot. Unplayable. |
| Super Final Match Tennis | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Heavy slowdown on the mode-select screen, full speed during play, good audio, playable. N3DS: full speed, good audio, very playable. |
| Super Formation Soccer 94 | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Exhibition match runs full speed; some field background graphics missing, good audio. Playable. |
| Super Formation Soccer 94 - World Cup Final Data | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Exhibition match ~57-59 FPS without frameskip; some field background graphics missing, good audio, playable. N3DS: full speed, same missing graphics, good audio, playable. |
| Super Formation Soccer 95 - della Serie A | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Exhibition match runs full speed; some field background graphics missing, good audio. Playable. |
| Super Formation Soccer 95 - della Serie A - UCC Club Edition | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Exhibition match runs full speed; some field background graphics missing, good audio. Playable. |
| Super Formation Soccer 96 - World Club Edition | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Exhibition match runs full speed; some field background graphics missing, good audio. Playable. |
| Super Mario Kart | 🇯🇵 🇺🇸 | 🟡&nbsp;Issues | O3DS: 50cc race ~55-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Super Mario World 2 - Yoshi's Island (V1.1) | 🇪🇺 🇺🇸 | 🟡&nbsp;Issues | N3DS: First level mostly full speed, good audio. Playable. |
| Super Off Road - The Baja | 🇺🇸 | 🟡&nbsp;Issues | O3DS: Race ~56-59 FPS without frameskip, good audio, playable. N3DS: full speed, good audio, very playable. |
| Super Power League 4 | 🇯🇵 | 🟡&nbsp;Issues | O3DS: ~44-59 FPS without frameskip due to SPC7110 chip emulation, good audio, playable. N3DS: ~56-59 FPS without frameskip, good audio, playable. |
| Tengai Makyou Zero | 🇯🇵 | 🟡&nbsp;Issues | O3DS: ~50-59 FPS without frameskip, good audio, playable. N3DS: full speed with rare dips, good audio, playable. |
| Tengai Makyou Zero - Shounen Jump no Shou | 🇯🇵 | 🟡&nbsp;Issues | O3DS: ~50-59 FPS without frameskip, good audio, playable. N3DS: full speed with rare dips, good audio, playable. |
| Tokimeki Memorial - Densetsu no Ki no Shita de (V1.1) | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Garbled text in the text bar. Full speed, good audio. Playable. |
| Top Gear 3000 | 🇺🇸 | 🟡&nbsp;Issues | N3DS: Championship race runs full speed; road textures bugged and the game is unstable (crashes possible), good audio. Playable. |
| Traverse - Starlight & Prairie | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Some flickering in the bar while moving around town. Full speed, good audio. Playable. |
| Treasure Hunter G | 🇯🇵 | 🟡&nbsp;Issues | O3DS: Overworld ~52-59 FPS without frameskip, full speed in the first town and dungeon, good audio, playable. N3DS: full speed, good audio, very playable. |
| Ultimate Zombies Ate my Neighbors [Hack] | 🇺🇸 | 🔴&nbsp;Broken | Black screen on boot. |
| Vortex | 🇪🇺 🇯🇵 🇺🇸 | 🟡&nbsp;Issues | N3DS: First mission ~30-49 FPS without frameskip, good audio. Playable. |
| Wakataka Oozumou - Yume no Kyodai Taiketsu | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Battles run full speed with some line flickering, good audio. Very playable. |
| Wicked 18 Golf | 🇺🇸 | 🟡&nbsp;Issues | O3DS & N3DS: Very slow to reach the title screen. Gameplay runs full speed, good audio. Playable. |
| World Class Rugby | 🇪🇺 | 🟡&nbsp;Issues | O3DS & N3DS: Glitched stadium. Matches run full speed, good audio. |
| X Zone | 🇪🇺 🇺🇸 | 🔴&nbsp;Broken | O3DS & N3DS: Unsupported Super Scope. Full speed, good audio. Unplayable. |
| Xardion | 🇪🇺 | 🔴&nbsp;Broken | O3DS & N3DS: Heavily glitched. Unplayable. |
| Yamato Takeru | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Colours change while moving around the village. Full speed, good audio. Playable. |
| Yoshi no Road Hunting | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Unsupported Super Scope. Full speed, good audio. Unplayable. |
| Yoshi's Safari | 🇪🇺 🇺🇸 | 🔴&nbsp;Broken | O3DS & N3DS: Unsupported Super Scope. Full speed, good audio. Unplayable. |
| Yuu Yuu Hakusho | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Battles run full speed with some glitches on character faces, good audio. Playable. |
| Yuu Yuu Hakusho - Tokubetsu Hen | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Battles run full speed with some glitches on character faces, good audio. Playable. |
| Zennihon Pro Wrestling | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Glitches on screen after boot. Battles run full speed, good audio. Playable. |

### Satellaview (BS-X) games

Place the BIOS file at `3ds/snes9x/BS-X.bin` with that exact filename and casing - some titles need it, some don't.
There are SoundLink games whose music was streamed live, so offline that audio is gone.
Several titles also hang on broadcast "wait" or "time-check" screens.
Some can be skipped via _fast forward_ or a community patch, others won't get past them at all.

The table below lists problematic games on this emulator.
For general Satellaview background - including compatibility categories and available patches - see this [community games list](https://forums.launchbox-app.com/topic/90630-nintendo-satellaview-organised-functional-games-list-no-more-guessing/).

| Game | Region | Status | Notes |
| --- | --- | --- | --- |
| BS Cu-On-Pa SFC | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Crashes in the menu. Unplayable. |
| BS Dragon Quest | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Crashes after the main menu. Unplayable. |
| BS Excitebike - Bunbun Mario Battle Stadium 1-4 | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: SoundLink game with forced timed wait screens. Skip via fast forward (slow) or a community patch (d4s). Playable. |
| BS F-Zero Grand Prix - Knight/Queen/King/Ace League (Dai-1 to Dai-4) | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: SoundLink game with forced timed wait screens. Skip via fast forward (slow) or a community patch (e.g. romhacking.net). Corrupted colours on some tracks. |
| BS Fenek - 6 Getsu | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen. Unplayable. |
| BS Fire Emblem - Akaneia Senki Hen - Dai-1 to Dai-4 | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen (Dai-1: red screen). Unplayable. |
| BS Game Tora no Taikoban 5-17 / 5-31 | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen. Unplayable. |
| BS Goods Press - 6 Gatsugou | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen. Unplayable. |
| BS Kodomo Chousadan Mighty Pockets Chousa 3 - Kyakusen Queen Patra no Nazo | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen. Unplayable. |
| BS Koubo Kenshou Magazine | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen. Unplayable. |
| BS Marvelous Time Athletic Course - Dai-1 to Dai-4 | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Freezes after boot. Unplayable. |
| BS Nintendo Power Magazine 107 | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen. Unplayable. |
| BS Parlor! Parlor! - Dai-2-shuu | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Freezes after the title screen. Unplayable. |
| BS Satella Walker 2 - Sate Bou o Sukuidase! | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen. Unplayable. |
| BS Shin Onigashima - Kataribe no Koya - Dai-1 to Dai-4 | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Freezes after the title screen. Unplayable. |
| BS Spriggan Powered - BS Version | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Full speed. No music, SFX only. Playable. |
| BS Super Mahjong Taikai | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Glitches on the KOEI logo. Full speed, good audio. Playable. |
| BS Super Mario Collection - Dai-3-shuu | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Full speed. No music, SFX only. Playable. |
| BS Super Mario USA - Dai-1 to Dai-4 | 🇯🇵 | 🟡&nbsp;Issues | O3DS & N3DS: Full speed. No music, SFX only. Playable. |
| BS Tantei Club - Yuki ni Kieta Kako - Zenhen/Chuuhen/Kouhen | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen. Unplayable. |
| BS Waiwai Check 3-7 | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Glitched. Unplayable. |
| BS Zelda no Densetsu - Dai-3-wa | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Black screen. Unplayable. |
| BS Zelda no Densetsu - Kodai no Sekiban - Dai-1 to Dai-4 | 🇯🇵 | 🔴&nbsp;Broken | O3DS & N3DS: Stuck early on. Unplayable. |
| Sutte Hakkun (Event Version, Winter Event Version, BS Version 2) | 🇯🇵 | 🔴&nbsp;Broken | Boots, but the game always believes a save exists: a file can be selected and the map loads, but no level can be chosen. Returning to the file-select menu leaves the cursor misplaced, and deleting the save files (and the .srm) repeats the same loop. |

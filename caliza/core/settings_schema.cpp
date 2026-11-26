// Caliza PS4: canonical settings/action schema implementation.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_schema.h"

#include "legacy_compat.h"

#include <algorithm>
#include <cstdlib>

namespace caliza::settings
{
static const std::vector<GroupDef> kGroups = {
	{"folders","Folders",UiTab::Settings,ScopeMask::Global,"",0},
	{"shelf-game","Shelf",UiTab::Settings,ScopeMask::Game,"",1},
	{"shelf-global","Shelf",UiTab::Settings,ScopeMask::Global,"",2},
	{"discs","Discs",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),"",3},
	{"display","Display",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),"",4},
	{"graphics","Graphics",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),"",5},
	{"performance","Performance",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),"",6},
	{"game","Game",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),"",7},
	{"on-screen","On screen",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),"",8},
	{"hardware-fixes","Hardware fixes",UiTab::Settings,ScopeMask::Game,"For upscaling glitches: lines, ghosting, effects out of place. The rows count while Manual hardware fixes is on, and changing one turns it on, starting from PCSX2's own fixes for this game (shown here while it's off), so nothing changes until a row does. Most do nothing at 1x.",9},
	{"crop","Crop",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),"",10},
	{"buttons","Buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),"Save and load: hold the two buttons together (slot 1). L3 + R3 is both sticks pressed in; Touchpad left or right is a finger on that side. Fast forward (experimental, for skipping videos): hold its two buttons to speed the game up, and again for full speed; Nothing on both turns it off. Change disc: hold its two buttons for the game's next disc (\"(Disc 2)\" beside it, or an .m3u list); any image goes in from the Disc section while you play. Below: what each button presses in games (Light press: the other buttons press at half strength, for Metal Gear Solid 2 and 3). Caliza's own combos always use the real buttons.",11},
	{"button-strength","Button strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),"How hard each button presses what it's set to press. The PS2's face buttons, D-pad and shoulders read pressure, and some games act on a light press: SOCOM II crouches at 20% on Triangle, Combined Assault at 30% (a full press goes prone). Set the touchpad click to Triangle at 20% for a crouch button. L2 and R2 press at most this hard.",12},
	{"sticks","Sticks",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),"",13},
	{"controller","Controller",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),"",14},
	{"keyboard-and-mouse","Keyboard and mouse",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),"",15},
};

static const std::vector<SettingDef> kExtractedSettings = {
	{"folders.more-game-folders","Caliza/GameFolders","More game folders","folders",UiTab::Settings,ScopeMask::Global,Inheritance::Fixed,ValueType::PathList,Presentation::Text,ApplyTiming::NextApplicationStart,"",{},"Folders to list games from, separated by ; (and the folders inside each, one level down). Caliza already looks in /data/caliza/games, /data/caliza and every USB, extended storage and M.2 drive (their root, games/, PS2/... and PCSX2/games/). Read when Caliza starts.","","the game folders","/mnt/ext1/PS2;/mnt/usb0/My games",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"folders.bezel-dir","Caliza/BezelDir","Overlays folder","folders",UiTab::Settings,ScopeMask::Global,Inheritance::Fixed,ValueType::Text,Presentation::Text,ApplyTiming::Live,"",{},"Optional: where the overlay pictures are looked for first (<game serial>.png, default.png), before /data/caliza/overlays.","","","/mnt/usb0/PS2/overlays",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"folders.nfs-shares","Caliza/NfsShares","NFS shares","folders",UiTab::Settings,ScopeMask::Global,Inheritance::Fixed,ValueType::PathList,Presentation::Text,ApplyTiming::NextApplicationStart,"",{},"Games on a PC or NAS shared over NFS: nfs:// addresses separated by ; (the server, then the shared folder or a folder in it). NFS v3 (Windows: WinNFSd or haneWIN; Linux, Synology, QNAP, TrueNAS), else v4. Add ?version=4, ?nfsport=2049&mountport=1058 or ?uid=1000&gid=1000 when the server needs them. On PS4 use plain nfs://server/export (NFS v3); query options are not supported. Read-only. Mounted when Caliza starts; boot.log says how it went.","","the NFS shares","nfs://192.168.1.10/volume1/PS2",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"folders.bios-folder","Caliza/BiosFolder","BIOS folder","folders",UiTab::Settings,ScopeMask::Global,Inheritance::Fixed,ValueType::Text,Presentation::Text,ApplyTiming::NextApplicationStart,"",{},"Where to look for the BIOS first. Without it: /data/caliza/bios, then /data/caliza and the drives' bios/ and PCSX2/bios/ folders. Read when Caliza starts.","","the BIOS folder","/mnt/ext1/PS2/bios",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"shelf-game.hide-game","Caliza/HideGame","Hide from the shelf","shelf-game",UiTab::Settings,ScopeMask::Game,Inheritance::Fixed,ValueType::Boolean,Presentation::Toggle,ApplyTiming::NextMenuOpen,"false",{{"false","Off"},{"true","On"}},"Takes this game off the shelf. Show hidden games (All games) puts hidden games back on the shelf, dimmed, to turn this off again. Set here, it applies the next time the shelf opens; on the shelf's own sheet, when the sheet closes.","Hidden %","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"shelf-global.show-hidden-games","Caliza/ShowHiddenGames","Show hidden games","shelf-global",UiTab::Settings,ScopeMask::Global,Inheritance::Fixed,ValueType::Boolean,Presentation::Toggle,ApplyTiming::NextMenuOpen,"false",{{"false","Off"},{"true","On"}},"Puts the games hidden from the shelf back on it, dimmed, so you can open one and turn Hide from the shelf off. Set here, it applies the next time the shelf opens.","Hidden games %","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"discs.start-from-cheat-disc","Caliza/CheatDiscStart","Start from the cheat disc","discs",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::NextLaunch,"false",{{"false","Off"},{"true","On"}},"Starts the game from the cheat disc (a GameShark, say: Cheat disc, in the settings for all games), as disc 1, with the game's own discs after it. When it asks for the game's disc, hold the change disc buttons (Controls; L3 + R3 + D-pad right by default) or pick the disc under Now playing. Its codes stay on the memory card.","Cheat disc %","the cheat disc","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"display.upscale-multiplier","upscale_multiplier","Resolution","display",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"1",{{"1","1x"},{"2","2x"},{"3","3x"},{"4","4x"},{"5","5x"},{"6","6x"},{"8","8x (heavy)"}},"Internal resolution. 6x is about 4K; 8x draws more than the screen shows (about 5K, made smaller to fit): sharpest, and the heaviest on the GPU.","%","","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,"On PS4, ApplyRecommended filters upscale_multiplier preset lines out because the inherited high-power 4x/6x recommendations are too costly; user files are not filtered."},
	{"display.aspect-ratio","AspectRatio","Aspect ratio","display",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"Auto 4:3/3:2",{{"Auto 4:3/3:2","Auto"},{"4:3","4:3"},{"16:9","16:9"},{"Stretch","Stretch"}},"","Aspect %","","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"display.enable-wide-screen-patches","EmuCore/EnableWideScreenPatches","Widescreen patches","display",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"false",{{"false","Off"},{"true","On"}},"Uses the game's 16:9 patch when there is one.","Widescreen %","","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"display.tvshader","TVShader","Display filter","display",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"6","FSR"},{"7","FSR soft"},{"0","Classic"},{"5","CRT"}},"","%","","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"display.frame-generation","Caliza/FrameGeneration","Frame generation","display",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::NextLaunch,"false",{{"false","Off"},{"true","On"}},"Shows a frame made between two of the game's: a 60 fps game at 120 on a TV that takes 120 Hz (the TV switches to 120 Hz for the game), a 30 fps game at 60. Only while the game holds its frame rate (an uneven one shows the game's own frames); the FPS box then counts what the TV gets. A little more input lag, and small smears at the edges of fast movement. Experimental. Takes effect when the game starts.","Frame gen %","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"graphics.renderer","Caliza/Renderer","Renderer","graphics",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::NextLaunch,"Hardware",{{"Hardware","Vulkan"},{"paraLLEl","paraLLEl-GS"},{"Software","Software"}},"Vulkan uses the GPU. paraLLEl-GS is the optional Vulkan compute renderer in PGS builds: try it when the GS thread is CPU-bound (boot.log says which renderer ran). Software draws on the CPU, as a PS2 does it: slow, but right in games whose effects the hardware renderer gets wrong. Takes effect when the game starts.","%","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"graphics.filter","filter","Texture filtering","graphics",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"2",{{"0","Nearest"},{"2","PS2"},{"1","Bilinear"},{"3","Not sprites"}},"","% filtering","","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"graphics.max-anisotropy","MaxAnisotropy","Anisotropic filtering","graphics",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"2","2x"},{"4","4x"},{"8","8x"},{"16","16x"}},"","AF %","","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"graphics.accurate-blending-unit","accurate_blending_unit","Blending accuracy","graphics",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"1",{{"0","Min"},{"1","Basic"},{"2","Med"},{"3","High"}},"Higher fixes more effects and costs speed. Full and Max are left out for now: Max hung the GPU in Ratchet & Clank.","Blending %","","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"graphics.hw-mipmap","hw_mipmap","Mipmapping","graphics",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"true",{{"false","Off"},{"true","On"}},"","Mipmaps %","","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"graphics.load-texture-replacements","LoadTextureReplacements","Texture replacements","graphics",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"false",{{"false","Off"},{"true","On"}},"Loads a texture pack's PNG and DDS files from <game serial>/replacements/ in a USB drive's caliza/textures (or textures) folder, else in /data/caliza/textures. DDS (DXT1 to DXT5, BC7 or uncompressed) is unpacked by the CPU as a game asks for it.","Replacements %","","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"graphics.textures-dir","Caliza/TexturesDir","Textures folder","graphics",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Text,Presentation::Text,ApplyTiming::Live,"",{},"Optional: a folder that holds <game serial>/replacements/, when the pack isn't in one of the places above.","","the textures folder","/mnt/usb0/PS2/textures",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,"Legacy UI labels this as next-launch, but the running frontend polls it through the live options reader; schema timing is the observed behavior."},
	{"performance.eecycle-rate","EmuCore/Speedhacks/EECycleRate","EE cycle rate","performance",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"-3","50%"},{"-2","60%"},{"-1","75%"},{"0","100%"},{"1","130% +CPU"},{"2","180% +CPU"},{"3","300% +CPU"}},"Above 100% can smooth out slowdown the PS2 had, but costs extra CPU on the PS4; below 100% costs the emulated game speed. Some games dislike it.","EE %","","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,"On PS4, ApplyRecommended filters EECycleRate preset lines out because the inherited high-power 130%+ recommendations are too costly; user files are not filtered."},
	{"performance.eecycle-skip","EmuCore/Speedhacks/EECycleSkip","EE cycle skip","performance",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"1","Mild"},{"2","Moderate"},{"3","Max"}},"","Cycle skip %","","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"performance.vu-thread","EmuCore/Speedhacks/vuThread","MTVU (VU1 on its own thread)","performance",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::NextLaunch,"true",{{"false","Off"},{"true","On"}},"","MTVU %","MTVU","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"performance.hwdownload-mode","HWDownloadMode","GPU readbacks","performance",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Accurate"},{"1","Whole area"},{"3","Don't wait"},{"4","Skip"}},"Some games read the picture back from the GPU, which is slow on the PS4. Accurate is PCSX2's default. Whole area reads everything drawn since the last read in one go, so there are fewer stops. Don't wait doesn't stop the game for the GPU: quicker, but effects that depend on it can flicker. Skip ignores the reads: fastest, and those effects break. A game that slows down because of them (on firmware below 10 each read can wait a vblank; elsewhere some games read so often that the waits add up to a third of the time) switches to Don't wait by itself unless this is set.","Readbacks %","","",2,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"game.language","Caliza/GameLanguage","Game language","game",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::NextLaunch,"-1",{{"-1","Auto (console)"},{"0","Japanese"},{"1","English"},{"2","French"},{"3","Spanish"},{"4","German"},{"5","Italian"},{"6","Dutch"},{"7","Portuguese"}},"The language the PS2 tells games. PAL games with several languages start in it; most US games are English only. Auto follows the console language.","Language %","the language","",3,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"game.eth-enable","DEV9/Eth/EthEnable","Network adapter","game",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::NextLaunch,"true",{{"false","Off"},{"true","On"}},"The PS2's network adapter, on the console's own network connection, for online and LAN play (on revival servers such as PS Rewired; their DNS goes in the game's network settings). It costs nothing until a game uses it. Turn it off for a game that misbehaves with it. Takes effect when the game starts.","Network %","the network adapter","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"game.host-fs","EmuCore/HostFs","Host filesystem","game",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::NextLaunch,"false",{{"false","Off"},{"true","On"}},"Lets homebrew read and write files in its own folder (host:), as PCSX2's Enable Host Filesystem: the ELF's folder, or the disc image's. Only for homebrew that asks for it.","Host files %","the host filesystem","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"game.enable-fast-boot","EmuCore/EnableFastBoot","Fast boot","game",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::NextLaunch,"true",{{"false","Off"},{"true","On"}},"Off: the PS2's own start-up first, the towers and the PlayStation 2 logo, as on a PS2 (a few games want it). On goes straight to the game.","Fast boot %","fast boot","",0,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"on-screen.overlay","Caliza/Overlay","Info box","on-screen",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"2",{{"0","Off"},{"1","FPS"},{"2","FPS + load"}},"The box in the top right corner. Load is how busy the EE, GS and VU threads are.","Info box %","","",3,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"on-screen.fps-graph","Caliza/FpsGraph","FPS graph","on-screen",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"false",{{"false","Off"},{"true","On"}},"A blue graph of the last minute's frame rate in the top right corner. It shows with the info box off too.","FPS graph %","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"on-screen.bezel","Caliza/Bezel","Overlay picture","on-screen",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"false",{{"false","Off"},{"true","On"}},"A bezel or frame drawn over the screen around the game: overlays/<game serial>.png (SLUS-21147.png), else overlays/default.png, in /data/caliza or the Overlays folder below (All games). A 1920x1080 PNG (at most; it is stretched to the screen) with a see-through middle where the game shows; the game keeps its own size (Aspect ratio).","Overlay %","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"on-screen.rachallenge-icons","Caliza/RAChallengeIcons","Challenge icons","on-screen",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"true",{{"false","Off"},{"true","On"}},"RetroAchievements: the badge of each challenge going on (an achievement that unlocks if you keep it up, such as no damage taken) in the bottom right corner, while it lasts.","Challenge icons %","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"hardware-fixes.user-hacks","UserHacks","Manual hardware fixes","hardware-fixes",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"false",{{"false","Off"},{"true","On"}},"","Manual fixes %","","",0,0.0,0.0,0.0,false,false,false,true,false,false,false,"",0,""},
	{"hardware-fixes.user-hacks-half-pixel-offset","UserHacks_HalfPixelOffset","Half-pixel offset","hardware-fixes",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"0",{{"0","Off"},{"1","Normal (vertex)"},{"2","Special (texture)"},{"3","Special (aggressive)"},{"4","Align to native"},{"5","Native + texture offset"}},"Moves the picture by half a pixel to line up effects (bloom, blur, shadows) that sit off when upscaled.","Half-pixel %","","",0,0.0,0.0,0.0,true,false,true,false,false,false,false,"",0,""},
	{"hardware-fixes.user-hacks-native-scaling","UserHacks_native_scaling","Native scaling","hardware-fixes",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"0",{{"0","Off"},{"1","Normal"},{"2","Aggressive"},{"3","Normal (keep upscale)"},{"4","Aggressive (keep upscale)"}},"Draws post-processing effects at the PS2's own size, then scales them up: for effects that break when upscaled (depth of field, glow). Keep upscale stays sharper.","Native scaling %","","",0,0.0,0.0,0.0,true,false,true,false,false,false,false,"",0,""},
	{"hardware-fixes.user-hacks-round-sprite-offset","UserHacks_round_sprite_offset","Round sprite","hardware-fixes",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"1","Half"},{"2","Full"}},"Rounds 2D sprites' texture positions: for lines and blur in 2D pictures and text when upscaled.","Round sprite %","","",0,0.0,0.0,0.0,false,false,true,false,false,false,false,"",0,""},
	{"hardware-fixes.user-hacks-align-sprite-x","UserHacks_align_sprite_X","Align sprite","hardware-fixes",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"false",{{"false","Off"},{"true","On"}},"Lines up sprites that leave vertical lines when upscaled.","Align sprite %","","",0,0.0,0.0,0.0,false,false,true,false,false,false,false,"",0,""},
	{"hardware-fixes.user-hacks-merge-pp-sprite","UserHacks_merge_pp_sprite","Merge sprite","hardware-fixes",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"false",{{"false","Off"},{"true","On"}},"Draws an effect made of many sprites as one: for lines through effects when upscaled.","Merge sprite %","","",0,0.0,0.0,0.0,false,false,true,false,false,false,false,"",0,""},
	{"hardware-fixes.user-hacks-bilinear-hack","UserHacks_BilinearHack","Bilinear upscale","hardware-fixes",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Auto"},{"1","Bilinear"},{"2","Nearest"}},"How textures the PS2 smooths are smoothed when upscaled. Auto: PCSX2 chooses.","Bilinear %","","",0,0.0,0.0,0.0,false,false,true,false,false,false,false,"",0,""},
	{"hardware-fixes.user-hacks-tcoffset-x","UserHacks_TCOffsetX","Texture offset X","hardware-fixes",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"0",{{"0","Off"},{"100","100"},{"200","200"},{"250","250"},{"300","300"},{"400","400"},{"500","500"},{"525","525"},{"550","550"},{"600","600"},{"700","700"},{"750","750"},{"800","800"},{"900","900"},{"1000","1000"}},"Moves where textures are read from, in thousandths of a texel (500 is half a texel): for lines or stray pixels at the edges of textures when upscaled. A tester's Kingdom Hearts fix: X 525, Y 0.","Texture offset X %","","",0,0.0,0.0,0.0,true,false,true,false,false,false,false,"",0,""},
	{"hardware-fixes.user-hacks-tcoffset-y","UserHacks_TCOffsetY","Texture offset Y","hardware-fixes",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"0",{{"0","Off"},{"100","100"},{"200","200"},{"250","250"},{"300","300"},{"400","400"},{"500","500"},{"525","525"},{"550","550"},{"600","600"},{"700","700"},{"750","750"},{"800","800"},{"900","900"},{"1000","1000"}},"","Texture offset Y %","","",0,0.0,0.0,0.0,true,false,true,false,false,false,false,"",0,""},
	{"hardware-fixes.user-hacks-auto-flush-level","UserHacks_AutoFlushLevel","Auto flush","hardware-fixes",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"1","Sprites"},{"2","All"}},"Ends a draw when it reads the picture it is drawing on, as the PS2 would: for some missing or broken effects. Costs speed. Sprites: for sprites only.","Auto flush %","","",0,0.0,0.0,0.0,false,false,true,false,false,false,false,"",0,""},
	{"hardware-fixes.user-hacks-texture-inside-rt","UserHacks_TextureInsideRt","Texture inside target","hardware-fixes",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"1","Inside"},{"2","Merge"}},"Lets a texture be read from inside a bigger picture the game drew earlier: for some missing or wrong effects. Merge also joins pictures that touch.","Texture in target %","","",0,0.0,0.0,0.0,false,false,true,false,false,false,false,"",0,""},
	{"crop.crop-left","CropLeft","Crop left","crop",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"2","2 px"},{"4","4 px"},{"6","6 px"},{"8","8 px"},{"10","10 px"},{"12","12 px"},{"16","16 px"},{"24","24 px"},{"32","32 px"}},"Cuts this many PS2 pixels off this side of the picture, for games with garbage at a border (Shadow of the Colossus). The picture keeps its proportions.","Crop left %","","",5,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"crop.crop-top","CropTop","Crop top","crop",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"2","2 px"},{"4","4 px"},{"6","6 px"},{"8","8 px"},{"10","10 px"},{"12","12 px"},{"16","16 px"},{"24","24 px"},{"32","32 px"}},"","Crop top %","","",5,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"crop.crop-right","CropRight","Crop right","crop",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"2","2 px"},{"4","4 px"},{"6","6 px"},{"8","8 px"},{"10","10 px"},{"12","12 px"},{"16","16 px"},{"24","24 px"},{"32","32 px"}},"","Crop right %","","",5,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"crop.crop-bottom","CropBottom","Crop bottom","crop",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"2","2 px"},{"4","4 px"},{"6","6 px"},{"8","8 px"},{"10","10 px"},{"12","12 px"},{"16","16 px"},{"24","24 px"},{"32","32 px"}},"","Crop bottom %","","",5,0.0,0.0,0.0,false,false,false,false,false,false,false,"",0,""},
	{"buttons.save-button1","Caliza/SaveButton1","Save: button 1","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"L3R3",{{"L3R3","L3 + R3"},{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Options","Options"},{"Touchpad","Touchpad click"},{"TouchLeft","Touchpad left"},{"TouchRight","Touchpad right"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"save-state",1,""},
	{"buttons.save-button2","Caliza/SaveButton2","Save: button 2","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Up",{{"L3R3","L3 + R3"},{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Options","Options"},{"Touchpad","Touchpad click"},{"TouchLeft","Touchpad left"},{"TouchRight","Touchpad right"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"save-state",2,""},
	{"buttons.load-button1","Caliza/LoadButton1","Load: button 1","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"L3R3",{{"L3R3","L3 + R3"},{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Options","Options"},{"Touchpad","Touchpad click"},{"TouchLeft","Touchpad left"},{"TouchRight","Touchpad right"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"load-state",1,""},
	{"buttons.load-button2","Caliza/LoadButton2","Load: button 2","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Down",{{"L3R3","L3 + R3"},{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Options","Options"},{"Touchpad","Touchpad click"},{"TouchLeft","Touchpad left"},{"TouchRight","Touchpad right"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"load-state",2,""},
	{"buttons.state-hold","Caliza/StateHold","Hold time","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Instant"},{"0.5","0.5 s"},{"1","1 s"},{"1.5","1.5 s"},{"2","2 s"},{"3","3 s"}},"How long the save or load buttons are held before it happens. Instant: as soon as both are down, and that last press doesn't reach the game. A second or two stops saving or loading by accident.","","","",6,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"buttons.fast-button1","Caliza/FastButton1","Fast forward: button 1","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"None",{{"L3R3","L3 + R3"},{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Options","Options"},{"Touchpad","Touchpad click"},{"TouchLeft","Touchpad left"},{"TouchRight","Touchpad right"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"fast-forward-experimental",1,""},
	{"buttons.fast-button2","Caliza/FastButton2","Fast forward: button 2","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"None",{{"L3R3","L3 + R3"},{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Options","Options"},{"Touchpad","Touchpad click"},{"TouchLeft","Touchpad left"},{"TouchRight","Touchpad right"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"fast-forward-experimental",2,""},
	{"buttons.fast-speed","Caliza/FastSpeed","Fast forward speed","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Max"},{"2","2x"},{"3","3x"},{"4","4x"}},"How fast fast forward runs. Max: as fast as the console can. Some games' videos skip only at Max.","","","",4,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"buttons.disc-button1","Caliza/DiscButton1","Change disc: button 1","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"L3R3",{{"L3R3","L3 + R3"},{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Options","Options"},{"Touchpad","Touchpad click"},{"TouchLeft","Touchpad left"},{"TouchRight","Touchpad right"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"change-disc",1,""},
	{"buttons.disc-button2","Caliza/DiscButton2","Change disc: button 2","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Right",{{"L3R3","L3 + R3"},{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Options","Options"},{"Touchpad","Touchpad click"},{"TouchLeft","Touchpad left"},{"TouchRight","Touchpad right"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"change-disc",2,""},
	{"buttons.button-cross","Caliza/ButtonCross","Cross","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Cross",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-circle","Caliza/ButtonCircle","Circle","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Circle",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-square","Caliza/ButtonSquare","Square","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Square",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-triangle","Caliza/ButtonTriangle","Triangle","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Triangle",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-l1","Caliza/ButtonL1","L1","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"L1",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-r1","Caliza/ButtonR1","R1","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"R1",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-l2","Caliza/ButtonL2","L2","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"L2",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-r2","Caliza/ButtonR2","R2","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"R2",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-l3","Caliza/ButtonL3","L3","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"L3",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-r3","Caliza/ButtonR3","R3","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"R3",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-options","Caliza/ButtonOptions","Options","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Start",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-touchpad","Caliza/ButtonTouchpad","Touchpad click","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Select",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-up","Caliza/ButtonUp","D-pad up","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Up",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-down","Caliza/ButtonDown","D-pad down","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Down",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-left","Caliza/ButtonLeft","D-pad left","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Left",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"buttons.button-right","Caliza/ButtonRight","D-pad right","buttons",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"Right",{{"Cross","Cross"},{"Circle","Circle"},{"Square","Square"},{"Triangle","Triangle"},{"L1","L1"},{"R1","R1"},{"L2","L2"},{"R2","R2"},{"L3","L3"},{"R3","R3"},{"Start","Start"},{"Select","Select"},{"Up","D-pad up"},{"Down","D-pad down"},{"Left","D-pad left"},{"Right","D-pad right"},{"Analog","Analog button"},{"Pressure","Light press"},{"None","Nothing"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-cross-pressure","Caliza/ButtonCrossPressure","Cross","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-circle-pressure","Caliza/ButtonCirclePressure","Circle","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-square-pressure","Caliza/ButtonSquarePressure","Square","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-triangle-pressure","Caliza/ButtonTrianglePressure","Triangle","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-l1-pressure","Caliza/ButtonL1Pressure","L1","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-r1-pressure","Caliza/ButtonR1Pressure","R1","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-l2-pressure","Caliza/ButtonL2Pressure","L2","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-r2-pressure","Caliza/ButtonR2Pressure","R2","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-touchpad-pressure","Caliza/ButtonTouchpadPressure","Touchpad click","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-up-pressure","Caliza/ButtonUpPressure","D-pad up","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-down-pressure","Caliza/ButtonDownPressure","D-pad down","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-left-pressure","Caliza/ButtonLeftPressure","D-pad left","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"button-strength.button-right-pressure","Caliza/ButtonRightPressure","D-pad right","button-strength",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"1","Full"},{"0.75","75%"},{"0.5","50%"},{"0.4","40%"},{"0.3","30%"},{"0.25","25%"},{"0.2","20%"},{"0.15","15%"},{"0.1","10%"}},"","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"sticks.swap-sticks","Caliza/SwapSticks","Swap sticks","sticks",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"false",{{"false","Off"},{"true","On"}},"The left stick moves the game's right stick, and the right stick its left one.","","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"sticks.invert-left","Caliza/InvertLeft","Invert left stick","sticks",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"1","Up-down"},{"2","Left-right"},{"3","Both"}},"Turns the game's left stick up-down, left-right or both the other way round (the stick the game sees, after Swap sticks).","","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"sticks.invert-right","Caliza/InvertRight","Invert right stick","sticks",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"1","Up-down"},{"2","Left-right"},{"3","Both"}},"The same for the game's right stick: the camera in most games.","","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"sticks.left-stick-dpad","Caliza/LeftStickDpad","Left stick as D-pad","sticks",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Off"},{"1","Also"},{"2","Only"}},"For games that only read the D-pad. Also: the left stick presses the D-pad as it moves. Only: it presses just the D-pad, and the game's left stick stays still.","","","",3,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"sticks.deadzone-left","Caliza/DeadzoneLeft","Left stick dead zone","sticks",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"0",{{"0","Off"},{"5","5%"},{"10","10%"},{"15","15%"},{"20","20%"},{"25","25%"},{"30","30%"}},"Inside this much of the way out, the stick reads as centred; past it, the rest of the way still reaches full. For a stick that drifts, or a game that walks or turns by itself. Off is the stick as it is.","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"sticks.deadzone-right","Caliza/DeadzoneRight","Right stick dead zone","sticks",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"0",{{"0","Off"},{"5","5%"},{"10","10%"},{"15","15%"},{"20","20%"},{"25","25%"},{"30","30%"}},"Inside this much of the way out, the stick reads as centred; past it, the rest of the way still reaches full. For a stick that drifts, or a game that walks or turns by itself. Off is the stick as it is.","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"controller.rumble","Caliza/Rumble","Rumble","controller",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"true",{{"false","Off"},{"true","On"}},"The game's vibration on the controller. In a game, hold L2 and D-pad down for 2 seconds to open this page in the companion panel; the game keeps running behind it.","Rumble %","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"controller.rumble-strength","Caliza/RumbleStrength","Rumble strength","controller",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::Live,"1",{{"0.25","25%"},{"0.5","50%"},{"0.75","75%"},{"1","100%"},{"1.25","125%"},{"1.5","150%"},{"2","200%"}},"How strongly the controller vibrates, as a share of what the game asks for. Above 100% lifts weak rumble; it never goes past the motors' full strength.","","","",0,0.0,0.0,0.0,true,false,false,false,true,false,false,"",0,""},
	{"controller.multitap","Caliza/Multitap","Multitap","controller",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::NextLaunch,"0",{{"0","Off"},{"1","Port 1"},{"2","Port 2"}},"For 4-player games. Players 2 to 4 are the other users logged in on the console when the game starts, each on their own controller. Port 1 suits most games; a few want the multitap in port 2. Off: player 2 on port 2, as before.","","","",3,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"keyboard-and-mouse.keyboard-mouse","Caliza/KeyboardMouse","Keyboard and mouse","keyboard-and-mouse",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","Auto"},{"1","Controller"},{"2","USB devices"},{"3","Off"}},"Controller: the keys and the mouse press the PS2 controller's buttons, as in PCSX2 on a PC. Arrows are the D-pad, W A S D the left stick, T F G H the right stick, Enter is Start, Backspace Select, I J K L are Triangle Square Cross Circle, Q and E the shoulder buttons, 1 and 3 the triggers, 2 and 4 the stick clicks. F1 saves and F3 loads a state; hold Esc for a second to go back to the menu. USB devices: the PS2 sees a USB keyboard and mouse, for games made for them (Half-Life, Unreal Tournament, chat in online games). Auto: the controller, until a game starts reading the PS2's USB keyboard or mouse.","Keyboard and mouse %","","",2,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"keyboard-and-mouse.mouse-aim","Caliza/MouseAim","Mouse movement","keyboard-and-mouse",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"1",{{"1","Right stick"},{"2","Left stick"},{"0","Nothing"}},"Which stick the mouse moves in the controller mode. The right stick turns the camera in most games.","Mouse movement %","","",3,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"keyboard-and-mouse.mouse-speed","Caliza/MouseSpeed","Mouse speed","keyboard-and-mouse",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"2",{{"1","Slow"},{"2","Normal"},{"3","Fast"},{"4","Very fast"}},"How far a mouse movement pushes the stick. Faster reaches a full stick with less movement.","Mouse speed %","","",4,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"keyboard-and-mouse.mouse-buttons","Caliza/MouseButtons","Mouse buttons","keyboard-and-mouse",UiTab::Controls,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Segmented,ApplyTiming::Live,"0",{{"0","R1 and L1"},{"1","R2 and L2"}},"Left click and right click. Most shooters fire with R1 or R2 and aim with L1 or L2. The wheel click is R3.","Mouse buttons %","","",2,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
};

// PS4-only rows injected into the web settings page after vuThread. They are live implementation knobs,
// player-owned for recommended settings, and absent from the legacy native sheet's static table.
static const std::vector<SettingDef> kPs4PerformanceSettings = {
	{"performance.mtvu-spin-wait","EmuCore/Speedhacks/OrbisMTVUSpin","MTVU spin wait","performance",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"true",{{"false","Off"},{"true","On"}},
		"The VU thread waits briefly for work before sleeping. On avoids kernel wakeups; off can use less CPU when work is sparse. Only matters with MTVU.","VU spin %","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"performance.mtvu-kick-batching","EmuCore/Speedhacks/OrbisMTVUBatch","MTVU kick batching","performance",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Toggle,ApplyTiming::Live,"false",{{"false","Off"},{"true","On"}},
		"Wakes the VU thread once at the end of a VIF1 transfer instead of for every packet. Can cut EE overhead; try it per game. Only matters with MTVU.","VU batch %","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,""},
	{"performance.mtvu-ring","EmuCore/Speedhacks/OrbisMTVURingKB","MTVU ring (KB)","performance",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Number,Presentation::Text,ApplyTiming::Live,"2048",{},
		"The EE-to-VU ring's length, 256 to 2048 KB on PS4. Shorter keeps packets in cache; too short can stutter or break a game.","VU ring % KB","","",0,256.0,2048.0,256.0,true,false,false,false,true,false,false,"",0,""},
};

// Additional rows and storage keys that the static GROUPS table coordinates dynamically or renders
// outside its scalar rows. They are still canonical settings state and must not be hard-coded a second time.
static const std::vector<SettingDef> kCoordinatedSettings = {
	{"discs.elf-disc-image","Caliza/ElfDisc","Disc image","discs",UiTab::Settings,ScopeMask::Game,Inheritance::Fixed,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::NextLaunch,"",{},
		"The disc image this ELF runs with: a patched or translated game's executable with its own disc, or homebrew that reads one. No disc: the ELF alone.","","","",0,0.0,0.0,0.0,true,true,false,false,true,false,false,"",0,"Rendered only for ELF catalog entries; choices are legacy basename catalog ids plus an empty No disc value."},
	{"discs.cheat-disc-image","Caliza/CheatDisc","Cheat disc","discs",UiTab::Settings,ScopeMask::Global,Inheritance::Fixed,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::NextLaunch,"",{},
		"A GameShark, Action Replay or CodeBreaker image. Games with Start from the cheat disc on start from it as disc 1, with their own discs after it.","","","",0,0.0,0.0,0.0,true,true,false,false,true,false,false,"",0,"Global-only selector; choices are legacy basename catalog ids plus an empty None value."},
	{"graphics.cas-mode","CASMode","Contrast adaptive sharpening","graphics",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::Hidden,ApplyTiming::Live,"0",{{"0","None"},{"1","Sharpen only"},{"2","Sharpen and resize"}},
		"PCSX2's CAS post-processing mode. Not an independent console/web row; Smart Tune writes Disabled or Sharpen only while tuning.","","","",0,0.0,0.0,0.0,false,false,false,false,false,true,false,"",0,"Hidden implementation key coordinated by Smart Tune."},
	{"bios.image","Filenames/BIOS","BIOS","bios",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Choice,Presentation::ChoiceList,ApplyTiming::NextLaunch,"",{},
		"The PS2 BIOS the game starts with. Automatic takes the first one PCSX2 finds.","","","",0,0.0,0.0,0.0,true,true,false,false,false,false,false,"",0,"Choices are file names in the configured BIOS directory; empty means automatic. Not currently preserved by ApplyRecommended."},
	{"memory-cards.slot-1-file","MemoryCards/Slot1_Filename","Memory card slot 1","memory-cards",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::MemoryCardFile,Presentation::Composite,ApplyTiming::Live,"Mcd001.ps2",{},
		"Which memory card is in slot 1. Cards are files in memcards/.","","","",0,0.0,0.0,0.0,true,true,false,false,true,true,false,"",0,"Rendered together with MemoryCards/Slot1_Enable as one slot selector."},
	{"memory-cards.slot-1-enable","MemoryCards/Slot1_Enable","Slot 1 enabled","memory-cards",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Hidden,ApplyTiming::Live,"true",{{"false","No card"},{"true","Card inserted"}},
		"Whether slot 1 has a card inserted.","","","",0,0.0,0.0,0.0,false,false,false,false,true,true,false,"",0,"false with no file override represents the UI's No card choice."},
	{"memory-cards.slot-2-file","MemoryCards/Slot2_Filename","Memory card slot 2","memory-cards",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::MemoryCardFile,Presentation::Composite,ApplyTiming::Live,"Mcd002.ps2",{},
		"Which memory card is in slot 2. Cards are files in memcards/.","","","",0,0.0,0.0,0.0,true,true,false,false,true,true,false,"",0,"Rendered together with MemoryCards/Slot2_Enable as one slot selector."},
	{"memory-cards.slot-2-enable","MemoryCards/Slot2_Enable","Slot 2 enabled","memory-cards",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Hidden,ApplyTiming::Live,"true",{{"false","No card"},{"true","Card inserted"}},
		"Whether slot 2 has a card inserted.","","","",0,0.0,0.0,0.0,false,false,false,false,true,true,false,"",0,"false with no file override represents the UI's No card choice."},
	{"patches.enabled","Patches/Enable","Enabled patch group","patches",UiTab::Settings,ScopeMask::Game,Inheritance::Fixed,ValueType::GroupList,Presentation::Composite,ApplyTiming::Live,"",{},
		"A patch group from the game's pnach files. Each enabled group is one repeated Patches/Enable line.","","","",0,0.0,0.0,0.0,true,true,false,false,false,false,true,"",0,"Dynamic per-game rows; removing the last line turns the group off. Patch groups can apply while the game runs."},
	{"cheats.enabled","Cheats/Enable","Enabled cheat group","cheats",UiTab::Settings,ScopeMask::Game,Inheritance::Fixed,ValueType::GroupList,Presentation::Composite,ApplyTiming::Live,"",{},
		"A cheat group from the game's cheats pnach files. Each enabled group is one repeated Cheats/Enable line.","","","",0,0.0,0.0,0.0,true,true,false,false,false,false,true,"",0,"Dynamic per-game rows coordinated with EmuCore/EnableCheats."},
	{"cheats.master-enable","EmuCore/EnableCheats","Enable cheats","cheats",UiTab::Settings,ScopeMask::Game,Inheritance::InheritGlobal,ValueType::Boolean,Presentation::Hidden,ApplyTiming::Live,"false",{{"false","Off"},{"true","On"}},
		"PCSX2's master cheat switch, set automatically while any cheat group is enabled.","","","",0,0.0,0.0,0.0,false,false,false,false,false,true,false,"",0,"Not rendered as an independent row; cheat rows set or unset it."},

	// Caliza additions required by the Estratos accessibility state. These are new keys, not renames of
	// legacy compatibility keys; integration must teach the recommended-settings player-key filter to keep them.
	{"accessibility.text-scale","Caliza/UiTextScalePercent","Text size","accessibility",UiTab::Settings,ScopeMask::Global,Inheritance::Fixed,ValueType::Number,Presentation::Segmented,ApplyTiming::ImmediateUi,"100",{{"100","Standard"},{"125","Large"},{"150","Extra large"},{"175","Huge"},{"200","Maximum"}},
		"Scales interface text while keeping navigation in one axis.","","","",5,100.0,200.0,25.0,false,false,false,false,true,false,false,"",0,"Accessibility addition; no legacy consumer exists yet."},
	{"accessibility.high-contrast","Caliza/UiHighContrast","High contrast","accessibility",UiTab::Settings,ScopeMask::Global,Inheritance::Fixed,ValueType::Boolean,Presentation::Toggle,ApplyTiming::ImmediateUi,"false",{{"false","Off"},{"true","On"}},
		"Uses the high-contrast Caliza palette and stronger focus separation.","","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,"Accessibility addition; no legacy consumer exists yet."},
	{"accessibility.reduce-motion","Caliza/UiReduceMotion","Reduce motion","accessibility",UiTab::Settings,ScopeMask::Global,Inheritance::Fixed,ValueType::Boolean,Presentation::Toggle,ApplyTiming::ImmediateUi,"false",{{"false","Off"},{"true","On"}},
		"Turns off decorative animation, parallax and flashing transitions.","","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,"Accessibility addition; no legacy consumer exists yet."},
	{"accessibility.large-focus","Caliza/UiLargeFocus","Large focus indicator","accessibility",UiTab::Settings,ScopeMask::Global,Inheritance::Fixed,ValueType::Boolean,Presentation::Toggle,ApplyTiming::ImmediateUi,"true",{{"false","Off"},{"true","On"}},
		"Uses the enlarged focus outline and stronger shape change for the focused item.","","","",0,0.0,0.0,0.0,false,false,false,false,true,false,false,"",0,"Accessibility addition; no legacy consumer exists yet."},
};

static const std::vector<GroupDef> kCoordinatedGroups = {
	{"bios","PS2 BIOS",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),"BIOS selection is stored in PCSX2's Filenames/BIOS key.",16},
	{"memory-cards","Memory cards",UiTab::Settings,(ScopeMask::Global | ScopeMask::Game),"Slot assignment is per global or per-game settings; card images themselves are files in memcards/.",17},
	{"patches","Patches",UiTab::Settings,ScopeMask::Game,"Dynamic rows read from patches/<serial>_<crc>.pnach; orphaned enabled groups remain visible so they can be turned off.",18},
	{"cheats","Cheats",UiTab::Settings,ScopeMask::Game,"Dynamic rows read from cheats/<serial>_<crc>.pnach; enabling one coordinates EmuCore/EnableCheats.",19},
	{"accessibility","Accessibility",UiTab::Settings,ScopeMask::Global,"Caliza interface additions from the Estratos accessibility state.",20},
};

static const std::vector<ActionDef> kActions = {
	{"library.launch-game","Launch game",ActionDomain::Library,ScopeMask::Game,"Validate BIOS availability and image damage, then boot the selected game.","LaunchGame","game_key",Confirmation::None,0.0,false,false,true,false,false,false,false,false,false},
	{"library.launch-system-menu","PS2 system menu",ActionDomain::Library,ScopeMask::Global,"Boot the PS2 system menu with no disc; memory-card slot settings still apply.","LaunchSystemMenu","",Confirmation::TwoPress,4.0,false,false,false,false,true,false,false,false,false},
	// Smart Tune is experimental and still needs proper hardware testing on representative games.
	{"library.launch-smart-tune","Smart tune",ActionDomain::Library,ScopeMask::Game,"Start this game with the one-shot Smart Tune flag; measurement depends on reaching real gameplay and remains experimental.","LaunchGame","game_key; mode=smart_tune",Confirmation::TwoPress,4.0,false,false,true,false,false,true,false,false,false},
	{"library.open-settings","Open settings sheet",ActionDomain::Presentation,(ScopeMask::Global | ScopeMask::Game),"Open settings for the selected game, or global settings when no game exists.","","scope",Confirmation::None,0.0,false,false,false,false,false,false,false,true},
	{"library.close-settings","Close settings sheet",ActionDomain::Presentation,(ScopeMask::Global | ScopeMask::Game),"Close the sheet; hide/show choices are applied to the shelf afterwards.","","",Confirmation::None,0.0,false,false,false,false,false,false,false,true},
	{"library.switch-settings-scope","Switch game/global settings",ActionDomain::Presentation,(ScopeMask::Global | ScopeMask::Game),"Switch between the selected game's sheet and the all-games sheet.","","scope",Confirmation::None,0.0,false,false,false,false,false,false,false,true},
	{"library.switch-tab","Switch settings tab",ActionDomain::Presentation,(ScopeMask::Global | ScopeMask::Game),"Switch Settings, Controls and Achievements tabs.","","tab",Confirmation::None,0.0,false,false,false,false,false,false,false,true},
	{"library.toggle-companion-qr","Toggle companion QR",ActionDomain::Presentation,(ScopeMask::Global | ScopeMask::Game),"Enlarge or close the companion-panel QR view.","","",Confirmation::None,0.0,false,false,false,false,false,false,false,true},
	{"library.show-about","About and licenses",ActionDomain::Presentation,ScopeMask::Global,"Open GPL/PS2UI attribution and license information.","","",Confirmation::None,0.0,false,false,false,false,true,false,false,true},

	{"settings.set-value","Set setting",ActionDomain::Settings,(ScopeMask::Global | ScopeMask::Game),"Write one scalar setting through the serialized settings service.","SetSetting","compatibility_key=value",Confirmation::None,0.0,false,false,false,false,false,false,false,true,false},
	{"settings.unset-value","Reset setting",ActionDomain::Settings,(ScopeMask::Global | ScopeMask::Game),"Remove one scalar setting so it follows its inherited layer or default.","ResetSetting","compatibility_key",Confirmation::None,0.0,false,false,false,false,false,false,false,true,false},
	{"settings.apply-recommended","Use recommended settings",ActionDomain::Settings,(ScopeMask::Global | ScopeMask::Game),"Replace the scope's tuning settings with the matching preset while preserving player-owned keys; the old file is kept as .before-recommended.","ApplyRecommendedSettings","scope+game_key",Confirmation::TwoPress,4.0,true,false,false,false,false,false,false,true,false},
	{"settings.reset-settings-scope","Reset settings",ActionDomain::Settings,(ScopeMask::Global | ScopeMask::Game),"Unset every settings-tab row owned by this scope; unrelated lines stay in the file.","ResetSettingsScope","settings; scope+game_key",Confirmation::TwoPress,4.0,true,false,false,false,false,false,false,true,false},
	{"settings.reset-controls","Reset controls",ActionDomain::Settings,(ScopeMask::Global | ScopeMask::Game),"Unset every controls-tab row owned by this scope.","ResetSettingsScope","controls; scope+game_key",Confirmation::TwoPress,4.0,true,false,false,false,false,false,false,true,false},

	{"storage.add-game-folder","Add game folder",ActionDomain::Storage,ScopeMask::Global,"Add a folder link to Caliza/GameFolders; the folder is scanned at application start and is never copied.","AddLibraryFolder","path",Confirmation::None,0.0,false,false,false,false,true,false,false,true,false},
	{"storage.remove-game-folder","Remove game folder",ActionDomain::Storage,ScopeMask::Global,"Remove one folder link only; game images are never deleted.","RemoveLibraryFolder","path",Confirmation::None,0.0,false,false,false,false,true,false,false,true,false},
	{"storage.reset-game-folders","Reset game folders",ActionDomain::Storage,ScopeMask::Global,"Clear Caliza/GameFolders so only built-in roots are scanned.","ResetSetting","Caliza/GameFolders",Confirmation::None,0.0,false,false,false,false,true,false,false,true,false},
	{"storage.pick-bios-folder","Pick BIOS folder",ActionDomain::Storage,ScopeMask::Global,"Set Caliza/BiosFolder through the folder picker.","SetBiosFolder","path",Confirmation::None,0.0,false,false,false,false,true,false,false,true,false},
	{"storage.reset-bios-folder","Reset BIOS folder",ActionDomain::Storage,ScopeMask::Global,"Unset Caliza/BiosFolder and use the normal BIOS search order.","ResetSetting","Caliza/BiosFolder",Confirmation::None,0.0,false,false,false,false,true,false,false,true,false},
	{"storage.add-nfs-share","Add NFS share",ActionDomain::Storage,ScopeMask::Global,"Append one nfs:// share to the share list with text entry.","SetSetting","Caliza/NfsShares=existing+new",Confirmation::None,0.0,false,false,false,false,true,false,false,true,false},
	{"storage.edit-nfs-shares","Edit NFS shares",ActionDomain::Storage,ScopeMask::Global,"Edit the semicolon-separated Caliza/NfsShares list with text entry.","SetSetting","Caliza/NfsShares=nfs://...",Confirmation::None,0.0,false,false,false,false,true,false,false,true,false},
	{"storage.remove-nfs-share","Remove NFS share",ActionDomain::Storage,ScopeMask::Global,"Remove one share link from Caliza/NfsShares; the remote content is never changed.","SetSetting","Caliza/NfsShares=remaining-list",Confirmation::None,0.0,false,false,false,false,true,false,false,true,false},

	{"bios.select","Select BIOS",ActionDomain::Storage,(ScopeMask::Global | ScopeMask::Game),"Set or inherit Filenames/BIOS from the files discovered in the BIOS folder.","SelectBios","file or empty",Confirmation::None,0.0,false,false,false,false,false,false,false,true,false},
	{"memory-cards.assign","Assign memory card",ActionDomain::Storage,(ScopeMask::Global | ScopeMask::Game),"Assign a card file or No card to slot 1/2 by coordinating Filename and Enable keys.","AssignMemoryCard","slot+file|@none|inherit",Confirmation::None,0.0,false,false,false,false,false,false,false,true,false},
	{"memory-cards.create","Create memory card",ActionDomain::Storage,(ScopeMask::Global | ScopeMask::Game),"Create a blank unformatted 8/16/32/64 MB PS2 card atomically in memcards/.","CreateMemoryCard","size_mb+name",Confirmation::None,0.0,false,false,false,false,false,false,true,false,false},

	{"patches.toggle","Toggle patch group",ActionDomain::Patches,ScopeMask::Game,"Add or remove one repeated Patches/Enable group line.","SetSetting","patch+name / patch-name",Confirmation::None,0.0,false,false,true,false,false,true,false,true,false},
	{"cheats.toggle","Toggle cheat group",ActionDomain::Patches,ScopeMask::Game,"Add or remove one repeated Cheats/Enable group line and coordinate EmuCore/EnableCheats.","SetSetting","cheat+name / cheat-name",Confirmation::None,0.0,false,false,true,false,false,true,false,true,false},
	{"patches.fetch-online","Get patches and cheats",ActionDomain::Patches,ScopeMask::Game,"Fetch PCSX2, Gabominated and cheat pnach files by serial+CRC without replacing user-owned files.","FetchOnlinePatches","game_key",Confirmation::None,0.0,false,false,true,false,false,true,true,false,false},

	{"textures.download-pack","Download HD texture pack",ActionDomain::Textures,ScopeMask::Game,"Download, verify, unpack and install the selected archive.org pack, then enable texture replacements for this game.","DownloadTexturePack","game_key+pack_index",Confirmation::None,0.0,false,false,true,false,false,true,true,true,false},
	{"textures.select-pack-choice","Select texture pack",ActionDomain::Textures,ScopeMask::Game,"Move left/right through the archive.org packs available for this game before downloading.","","pack_index_delta",Confirmation::None,0.0,false,false,true,false,false,true,false,false,true},
	{"textures.retry-catalog","Retry texture pack lookup",ActionDomain::Textures,ScopeMask::Game,"Retry the archive.org metadata lookup after it was unavailable.","RetryTexturePackCatalog","game_key",Confirmation::None,0.0,false,false,true,false,false,true,true,false,false},
	{"textures.cancel-pack","Cancel texture pack download",ActionDomain::Textures,ScopeMask::Game,"Cancel or forget the selected game's in-progress/failed download; partial data may remain for resume according to the worker state.","CancelTexturePack","game_key",Confirmation::TwoPress,4.0,false,false,true,false,false,true,false,false,false},
	{"textures.delete-pack","Delete installed texture pack",ActionDomain::Textures,ScopeMask::Game,"Delete only a pack installed by this application; user-supplied packs are not deleted.","DeleteTexturePack","game_key",Confirmation::TwoPress,4.0,true,false,true,false,false,true,true,false,false},

	{"runtime.save-state","Save state",ActionDomain::Runtime,ScopeMask::RunningVm,"Save the running game to state slot 1; this can overwrite the existing state and currently has no confirmation.","SaveState","slot=1",Confirmation::None,0.0,true,true,false,false,false,false,false,false,false},
	{"runtime.load-state","Load state",ActionDomain::Runtime,ScopeMask::RunningVm,"Load state slot 1, falling back to its .backup when appropriate; unsaved current progress is lost and currently has no confirmation.","LoadState","slot=1",Confirmation::None,0.0,true,true,false,false,false,false,false,false,false},
	{"runtime.toggle-fast-forward","Toggle fast forward",ActionDomain::Runtime,ScopeMask::RunningVm,"Toggle fast forward at Caliza/FastSpeed.","ToggleFastForward","",Confirmation::None,0.0,false,true,false,false,false,false,false,false,false},
	{"runtime.change-disc-next","Change to next disc",ActionDomain::Runtime,ScopeMask::RunningVm,"Insert the next image in the running game's disc set.","ChangeDisc","next",Confirmation::None,0.0,false,true,false,false,false,false,false,false,false},
	{"runtime.change-disc-path","Change disc",ActionDomain::Runtime,ScopeMask::RunningVm,"Insert a known catalog image selected in the companion panel.","ChangeDisc","path",Confirmation::None,0.0,false,true,false,false,false,false,false,false,false},
	{"runtime.return-to-menu","Return to library",ActionDomain::Runtime,ScopeMask::RunningVm,"Stop the VM and re-enter the menu process flow; unsaved progress can be lost and the legacy chord does not confirm.","ReturnToLibrary","",Confirmation::None,0.0,true,true,false,false,false,false,false,false,false},
	{"runtime.open-companion-panel","Open companion panel",ActionDomain::Runtime,ScopeMask::RunningVm,"Open the web settings panel in the console browser or show its QR fallback while the game keeps running.","OpenCompanionPanel","",Confirmation::None,0.0,false,true,false,false,false,false,false,false,false},
	{"runtime.cycle-display-filter","Cycle display filter",ActionDomain::Runtime,ScopeMask::RunningVm,"Cycle the present/display filter from the controller chord when L3+R3 is held without a D-pad direction.","","",Confirmation::None,0.0,false,true,false,false,false,false,false,false,false},

	{"achievements.open-account","Open RetroAchievements account",ActionDomain::Achievements,(ScopeMask::Global | ScopeMask::Game),"Open the local account panel with the L1+Square chord.","","",Confirmation::None,0.0,false,false,false,false,false,false,false,true},
	{"achievements.sign-in","Sign in to RetroAchievements",ActionDomain::Achievements,(ScopeMask::Global | ScopeMask::Game),"Authenticate locally with username and password; credentials must never pass through the LAN web panel.","","username+password",Confirmation::None,0.0,false,false,false,false,false,false,false,false,false},
	{"achievements.sign-out","Sign out of RetroAchievements",ActionDomain::Achievements,(ScopeMask::Global | ScopeMask::Game),"Remove the saved RetroAchievements sign-in.","","",Confirmation::TwoPress,4.0,true,false,false,false,false,false,false,false,false},
	{"achievements.refresh-game","Refresh achievements",ActionDomain::Achievements,ScopeMask::Game,"Load or refresh the selected/running game's achievement list and badge images.","","game_path",Confirmation::None,0.0,false,false,true,false,false,true,true,false,false},
	{"achievements.cancel-load","Cancel achievement load",ActionDomain::Achievements,ScopeMask::Game,"Cancel a pending achievement list/image load when leaving the tab or scope.","","",Confirmation::None,0.0,false,false,true,false,false,true,false,false,false},

	{"diagnostics.generate-report","Download report",ActionDomain::Diagnostics,(ScopeMask::Global | ScopeMask::Game),"Bundle logs, settings and catalog inventory into the downloadable tester report.","GenerateReport","",Confirmation::None,0.0,false,false,false,false,false,false,true,false,false},
	{"diagnostics.add-note","Add tester note",ActionDomain::Diagnostics,(ScopeMask::Global | ScopeMask::Game),"Append a sanitized one-line tester note to settings.log before or without downloading a report.","","note_text",Confirmation::None,0.0,false,false,false,false,false,false,false,false,false},
};

namespace
{
struct LegacyKeyMigrationSeed
{
	const char* legacy_suffix;
	const char* canonical_key;
};

// The retired product namespace is not spelled here. These suffixes are the complete set of product-owned
// settings keys that existed before the final brand transition; new Caliza-only keys (including the
// accessibility keys) are deliberately absent. Keeping the pairs explicit prevents a future new Caliza key
// from accidentally acquiring a fake legacy counterpart.
constexpr LegacyKeyMigrationSeed kLegacyKeyMigrationSeeds[] = {
	{"GameFolders","Caliza/GameFolders"},
	{"BezelDir","Caliza/BezelDir"},
	{"NfsShares","Caliza/NfsShares"},
	{"BiosFolder","Caliza/BiosFolder"},
	{"HideGame","Caliza/HideGame"},
	{"ShowHiddenGames","Caliza/ShowHiddenGames"},
	{"CheatDiscStart","Caliza/CheatDiscStart"},
	{"FrameGeneration","Caliza/FrameGeneration"},
	{"Renderer","Caliza/Renderer"},
	{"TexturesDir","Caliza/TexturesDir"},
	{"GameLanguage","Caliza/GameLanguage"},
	{"Overlay","Caliza/Overlay"},
	{"FpsGraph","Caliza/FpsGraph"},
	{"Bezel","Caliza/Bezel"},
	{"RAChallengeIcons","Caliza/RAChallengeIcons"},
	{"SaveButton1","Caliza/SaveButton1"},
	{"SaveButton2","Caliza/SaveButton2"},
	{"LoadButton1","Caliza/LoadButton1"},
	{"LoadButton2","Caliza/LoadButton2"},
	{"StateHold","Caliza/StateHold"},
	{"FastButton1","Caliza/FastButton1"},
	{"FastButton2","Caliza/FastButton2"},
	{"FastSpeed","Caliza/FastSpeed"},
	{"DiscButton1","Caliza/DiscButton1"},
	{"DiscButton2","Caliza/DiscButton2"},
	{"ButtonCross","Caliza/ButtonCross"},
	{"ButtonCircle","Caliza/ButtonCircle"},
	{"ButtonSquare","Caliza/ButtonSquare"},
	{"ButtonTriangle","Caliza/ButtonTriangle"},
	{"ButtonL1","Caliza/ButtonL1"},
	{"ButtonR1","Caliza/ButtonR1"},
	{"ButtonL2","Caliza/ButtonL2"},
	{"ButtonR2","Caliza/ButtonR2"},
	{"ButtonL3","Caliza/ButtonL3"},
	{"ButtonR3","Caliza/ButtonR3"},
	{"ButtonOptions","Caliza/ButtonOptions"},
	{"ButtonTouchpad","Caliza/ButtonTouchpad"},
	{"ButtonUp","Caliza/ButtonUp"},
	{"ButtonDown","Caliza/ButtonDown"},
	{"ButtonLeft","Caliza/ButtonLeft"},
	{"ButtonRight","Caliza/ButtonRight"},
	{"ButtonCrossPressure","Caliza/ButtonCrossPressure"},
	{"ButtonCirclePressure","Caliza/ButtonCirclePressure"},
	{"ButtonSquarePressure","Caliza/ButtonSquarePressure"},
	{"ButtonTrianglePressure","Caliza/ButtonTrianglePressure"},
	{"ButtonL1Pressure","Caliza/ButtonL1Pressure"},
	{"ButtonR1Pressure","Caliza/ButtonR1Pressure"},
	{"ButtonL2Pressure","Caliza/ButtonL2Pressure"},
	{"ButtonR2Pressure","Caliza/ButtonR2Pressure"},
	{"ButtonTouchpadPressure","Caliza/ButtonTouchpadPressure"},
	{"ButtonUpPressure","Caliza/ButtonUpPressure"},
	{"ButtonDownPressure","Caliza/ButtonDownPressure"},
	{"ButtonLeftPressure","Caliza/ButtonLeftPressure"},
	{"ButtonRightPressure","Caliza/ButtonRightPressure"},
	{"SwapSticks","Caliza/SwapSticks"},
	{"InvertLeft","Caliza/InvertLeft"},
	{"InvertRight","Caliza/InvertRight"},
	{"LeftStickDpad","Caliza/LeftStickDpad"},
	{"DeadzoneLeft","Caliza/DeadzoneLeft"},
	{"DeadzoneRight","Caliza/DeadzoneRight"},
	{"Rumble","Caliza/Rumble"},
	{"RumbleStrength","Caliza/RumbleStrength"},
	{"Multitap","Caliza/Multitap"},
	{"KeyboardMouse","Caliza/KeyboardMouse"},
	{"MouseAim","Caliza/MouseAim"},
	{"MouseSpeed","Caliza/MouseSpeed"},
	{"MouseButtons","Caliza/MouseButtons"},
	{"ElfDisc","Caliza/ElfDisc"},
	{"CheatDisc","Caliza/CheatDisc"},
};
} // namespace

static std::vector<SettingDef> MakeSettings()
{
	std::vector<SettingDef> out = kExtractedSettings;
	const auto vu = std::find_if(out.begin(), out.end(), [](const SettingDef& def) {
		return def.compatibility_key == "EmuCore/Speedhacks/vuThread";
	});
	out.insert(vu == out.end() ? out.end() : vu + 1, kPs4PerformanceSettings.begin(), kPs4PerformanceSettings.end());
	out.insert(out.end(), kCoordinatedSettings.begin(), kCoordinatedSettings.end());
	return out;
}

static std::vector<GroupDef> MakeGroups()
{
	std::vector<GroupDef> out = kGroups;
	out.insert(out.end(), kCoordinatedGroups.begin(), kCoordinatedGroups.end());
	return out;
}

const std::vector<GroupDef>& Groups()
{
	static const std::vector<GroupDef> groups = MakeGroups();
	return groups;
}

const std::vector<SettingDef>& Settings()
{
	static const std::vector<SettingDef> settings = MakeSettings();
	return settings;
}

const std::vector<ActionDef>& Actions()
{
	return kActions;
}

const GroupDef* FindGroup(const std::string& id)
{
	for (const GroupDef& group : Groups())
		if (group.id == id)
			return &group;
	return nullptr;
}

const SettingDef* FindSetting(const std::string& id)
{
	for (const SettingDef& def : Settings())
		if (def.id == id)
			return &def;
	return nullptr;
}

const SettingDef* FindSettingByKey(std::string compatibility_key)
{
	compatibility_key = CanonKey(compatibility_key);
	for (const SettingDef& def : Settings())
		if (def.compatibility_key == compatibility_key)
			return &def;
	return nullptr;
}


static std::vector<LegacyKeyMigration> MakeLegacyKeyMigrations()
{
	std::vector<LegacyKeyMigration> out;
	out.reserve(sizeof(kLegacyKeyMigrationSeeds) / sizeof(kLegacyKeyMigrationSeeds[0]));
	const std::string legacy_namespace = legacy::RetiredIniNamespace();
	for (const LegacyKeyMigrationSeed& seed : kLegacyKeyMigrationSeeds)
		out.push_back({legacy_namespace + seed.legacy_suffix, seed.canonical_key});
	return out;
}

const std::vector<LegacyKeyMigration>& LegacyKeyMigrations()
{
	static const std::vector<LegacyKeyMigration> migrations = MakeLegacyKeyMigrations();
	return migrations;
}

std::string MigrateLegacyKey(std::string key)
{
	key = CanonKey(key);
	for (const LegacyKeyMigration& migration : LegacyKeyMigrations())
		if (key == migration.legacy_key)
			return migration.canonical_key;
	return key;
}

const ActionDef* FindAction(const std::string& id)
{
	for (const ActionDef& action : Actions())
		if (action.id == id)
			return &action;
	return nullptr;
}

std::vector<const SettingDef*> SettingsInGroup(const std::string& group_id)
{
	std::vector<const SettingDef*> out;
	for (const SettingDef& def : Settings())
		if (def.group_id == group_id)
			out.push_back(&def);
	return out;
}

std::vector<const SettingDef*> SettingsForScope(ScopeMask scope, UiTab tab)
{
	std::vector<const SettingDef*> out;
	for (const SettingDef& def : Settings())
		if (def.tab == tab && HasScope(def.scopes, scope))
			out.push_back(&def);
	return out;
}

std::string Trim(const std::string& text)
{
	size_t first = 0, last = text.size();
	while (first < last && (text[first] == ' ' || text[first] == '\t' || text[first] == '\r' || text[first] == '\n'))
		++first;
	while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t' || text[last - 1] == '\r' || text[last - 1] == '\n'))
		--last;
	return text.substr(first, last - first);
}

std::string CanonKey(std::string key)
{
	key = Trim(key);
	if (key.compare(0, 11, "EmuCore/GS/") == 0)
		key.erase(0, 11);
	return key;
}

bool IsListKey(const std::string& key)
{
	return key == "Patches/Enable" || key == "Patches/Disable" || key == "Cheats/Enable" || key == "Cheats/Disable";
}

bool SafeKey(const std::string& key)
{
	if (key.empty() || key.size() > 80 || IsListKey(key))
		return false;
	for (char c : key)
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/'))
			return false;
	std::string first = key.substr(0, key.find('/'));
	for (char& c : first)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return first != "achievements";
}

bool SafeValue(const std::string& value)
{
	if (value.size() > 200)
		return false;
	for (char c : value)
		if (c == '\n' || c == '\r' || c == '#')
			return false;
	return true;
}

bool Truthy(const std::string& value)
{
	std::string v = Trim(value);
	for (char& c : v)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return v == "true" || v == "1" || v == "on" || v == "yes";
}


std::vector<std::string> SplitFolderList(const std::string& list)
{
	std::vector<std::string> items;
	size_t at = 0;
	while (at <= list.size())
	{
		size_t end = list.find_first_of(";|\n", at);
		if (end == std::string::npos)
			end = list.size();
		std::string item = list.substr(at, end - at);
		at = end + 1;
		while (!item.empty() && (item.back() == ' ' || item.back() == '\t' || item.back() == '\r' || (item.back() == '/' && item.size() > 1)))
			item.pop_back();
		while (!item.empty() && (item.front() == ' ' || item.front() == '\t'))
			item.erase(0, 1);
		bool seen = false;
		for (const std::string& existing : items)
			seen = seen || existing == item;
		if (!item.empty() && !seen)
			items.push_back(item);
		if (end == list.size())
			break;
	}
	return items;
}

std::string JoinFolderList(const std::vector<std::string>& items)
{
	std::string list;
	for (const std::string& item : items)
		list += (list.empty() ? "" : ";") + item;
	return list;
}

static bool ParseNumber(const std::string& text, double& out)
{
	const std::string v = Trim(text);
	if (v.empty())
		return false;
	char* end = nullptr;
	out = std::strtod(v.c_str(), &end);
	return end != v.c_str() && end && *end == '\0';
}

static std::string Lower(std::string value)
{
	for (char& c : value)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return value;
}

ValidationResult ValidateValue(const SettingDef& def, const std::string& value)
{
	ValidationResult result;
	const std::string trimmed = Trim(value);
	if (!SafeValue(trimmed))
	{
		result.error = "value is too long or contains a line break/#";
		return result;
	}
	switch (def.type)
	{
		case ValueType::Boolean:
			if (Truthy(trimmed))
			{
				result.ok = true;
				result.normalized = "true";
			}
			else if (Lower(trimmed) == "false" || trimmed == "0" || trimmed.empty())
			{
				result.ok = true;
				result.normalized = "false";
			}
			else
				result.error = "expected true or false";
			return result;
		case ValueType::Choice:
		case ValueType::MemoryCardFile:
		case ValueType::GroupList:
		{
			if (def.type != ValueType::Choice && trimmed.empty())
			{
				result.error = "empty value";
				return result;
			}
			double numeric_input = 0.0;
			bool numeric_input_valid = ParseNumber(trimmed, numeric_input);
			if (!numeric_input_valid && trimmed.size() > 1 && trimmed.back() == '%')
			{
				double percent = 0.0;
				if (ParseNumber(trimmed.substr(0, trimmed.size() - 1), percent))
				{
					numeric_input = percent / 100.0;
					numeric_input_valid = true;
				}
			}
			for (const Choice& choice : def.choices)
			{
				double want = 0.0, got = numeric_input;
				const bool numeric = ParseNumber(choice.value, want) && numeric_input_valid;
				if (choice.value == trimmed || Lower(choice.value) == Lower(trimmed) || (numeric && want == got))
				{
					result.ok = true;
					result.normalized = choice.value;
					return result;
				}
			}
			if (def.allow_unlisted || def.dynamic_choices)
			{
				result.ok = true;
				result.normalized = trimmed;
				return result;
			}
			result.error = def.choices.empty() ? "no static choices are defined" : "not one of the schema choices";
			return result;
		}
		case ValueType::Text:
		case ValueType::PathList:
			result.ok = true;
			result.normalized = trimmed;
			return result;
		case ValueType::Number:
		{
			double number = 0.0;
			if (!ParseNumber(trimmed, number))
			{
				result.error = "expected a number";
				return result;
			}
			if ((def.number_max > def.number_min) && (number < def.number_min || number > def.number_max))
			{
				result.error = "outside the allowed range";
				return result;
			}
			if (!def.choices.empty())
			{
				for (const Choice& choice : def.choices)
				{
					double choice_number = 0.0;
					if (ParseNumber(choice.value, choice_number) && choice_number == number)
					{
						result.ok = true;
						result.normalized = choice.value;
						return result;
					}
				}
				if (!def.allow_unlisted)
				{
					result.error = "not one of the schema choices";
					return result;
				}
			}
			result.ok = true;
			result.normalized = trimmed;
			return result;
		}
	}
	result.error = "unknown value type";
	return result;
}

bool SameSettingValue(const SettingDef& def, const std::string& a, const std::string& b)
{
	const ValidationResult va = ValidateValue(def, a), vb = ValidateValue(def, b);
	if (va.ok && vb.ok)
		return va.normalized == vb.normalized;
	return Trim(a) == Trim(b);
}

DocumentState ParseSettingsDocument(const std::string& text)
{
	DocumentState state;
	size_t pos = 0;
	while (pos <= text.size())
	{
		size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		const std::string trimmed = Trim(line);
		if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
		{
			if (end == text.size())
				break;
			continue;
		}
		const size_t equals = trimmed.find('=');
		if (equals == std::string::npos)
		{
			if (end == text.size())
				break;
			continue;
		}
		const std::string key = CanonKey(trimmed.substr(0, equals));
		const std::string value = Trim(trimmed.substr(equals + 1));
		if (key == "Patches/Enable")
		{
			bool seen = false;
			for (const std::string& patch : state.patches)
				seen = seen || patch == value;
			if (!seen)
				state.patches.push_back(value);
		}
		else if (key == "Cheats/Enable")
		{
			bool seen = false;
			for (const std::string& cheat : state.cheats)
				seen = seen || cheat == value;
			if (!seen)
				state.cheats.push_back(value);
		}
		else if (!IsListKey(key))
		{
			bool found = false;
			for (auto& kv : state.values)
				if (kv.first == key)
				{
					kv.second = value;
					found = true;
				}
			if (!found)
				state.values.emplace_back(key, value);
		}
		if (end == text.size())
			break;
	}
	return state;
}

std::string SerializeSettingsDocument(const DocumentState& state)
{
	std::string out;
	for (const auto& kv : state.values)
		out += kv.first + "=" + kv.second + "\n";
	for (const std::string& patch : state.patches)
		out += "Patches/Enable=" + patch + "\n";
	for (const std::string& cheat : state.cheats)
		out += "Cheats/Enable=" + cheat + "\n";
	return out;
}

const std::string* FindValue(const DocumentState& state, const std::string& key)
{
	for (const auto& kv : state.values)
		if (kv.first == key)
			return &kv.second;
	return nullptr;
}

std::string EffectiveValue(const SettingDef& def, const DocumentState& own, const DocumentState& global, bool* inherited)
{
	if (inherited)
		*inherited = false;
	if (const std::string* value = FindValue(own, def.compatibility_key))
		return *value;
	if (def.inheritance == Inheritance::InheritGlobal)
		if (const std::string* value = FindValue(global, def.compatibility_key))
		{
			if (inherited)
				*inherited = true;
			return *value;
		}
	return def.default_value;
}

bool ParseMutationLine(const std::string& line, Mutation& out, std::string* error)
{
	const std::string command = Trim(line);
	if (command.empty())
	{
		if (error) *error = "empty mutation";
		return false;
	}
	if (command == "recommended")
	{
		out.kind = Mutation::Kind::ApplyRecommended;
		out.key.clear();
		out.value.clear();
		return true;
	}
	const size_t space = command.find(' ');
	const std::string verb = command.substr(0, space);
	const std::string arg = space == std::string::npos ? std::string() : Trim(command.substr(space + 1));
	if (verb == "set")
	{
		const size_t equals = arg.find('=');
		if (equals == std::string::npos)
		{
			if (error) *error = "set needs key=value";
			return false;
		}
		out.kind = Mutation::Kind::Set;
		out.key = CanonKey(arg.substr(0, equals));
		out.value = Trim(arg.substr(equals + 1));
		if (!SafeKey(out.key) || !SafeValue(out.value))
		{
			if (error) *error = "unsafe key or value";
			return false;
		}
		return true;
	}
	if (verb == "unset")
	{
		out.kind = Mutation::Kind::Unset;
		out.key = CanonKey(arg);
		out.value.clear();
		if (!SafeKey(out.key))
		{
			if (error) *error = "unsafe key";
			return false;
		}
		return true;
	}
	if (verb == "patch+" || verb == "patch-" || verb == "cheat+" || verb == "cheat-")
	{
		out.kind = verb == "patch+" ? Mutation::Kind::PatchOn : verb == "patch-" ? Mutation::Kind::PatchOff :
		           verb == "cheat+" ? Mutation::Kind::CheatOn : Mutation::Kind::CheatOff;
		out.key = arg;
		out.value.clear();
		if (arg.empty() || arg.size() > 100 || !SafeValue(arg))
		{
			if (error) *error = "bad patch or cheat group name";
			return false;
		}
		return true;
	}
	if (error) *error = "unknown mutation verb";
	return false;
}

bool ParseMutationBatch(const std::string& body, std::vector<Mutation>& out, std::string* error)
{
	out.clear();
	if (Trim(body) == "recommended")
	{
		Mutation mutation;
		mutation.kind = Mutation::Kind::ApplyRecommended;
		out.push_back(mutation);
		return true;
	}
	size_t pos = 0;
	while (pos <= body.size())
	{
		size_t end = body.find('\n', pos);
		if (end == std::string::npos)
			end = body.size();
		const std::string line = body.substr(pos, end - pos);
		pos = end + 1;
		if (!Trim(line).empty())
		{
			Mutation mutation;
			if (!ParseMutationLine(line, mutation, error))
				return false;
			out.push_back(mutation);
		}
		if (end == body.size())
			break;
	}
	return !out.empty();
}

std::string SerializeMutation(const Mutation& mutation)
{
	switch (mutation.kind)
	{
		case Mutation::Kind::Set: return "set " + mutation.key + "=" + mutation.value;
		case Mutation::Kind::Unset: return "unset " + mutation.key;
		case Mutation::Kind::PatchOn: return "patch+ " + mutation.key;
		case Mutation::Kind::PatchOff: return "patch- " + mutation.key;
		case Mutation::Kind::CheatOn: return "cheat+ " + mutation.key;
		case Mutation::Kind::CheatOff: return "cheat- " + mutation.key;
		case Mutation::Kind::ApplyRecommended: return "recommended";
	}
	return {};
}

std::string JsonEscape(const std::string& text)
{
	std::string out;
	for (const char raw : text)
	{
		const unsigned char c = static_cast<unsigned char>(raw);
		switch (c)
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20)
				{
					static const char hex[] = "0123456789abcdef";
					out += "\\u00";
					out += hex[c >> 4];
					out += hex[c & 15];
				}
				else
					out += static_cast<char>(c);
		}
	}
	return out;
}

static const char* TabName(UiTab tab)
{
	switch (tab)
	{
		case UiTab::Settings: return "settings";
		case UiTab::Controls: return "controls";
		case UiTab::Achievements: return "achievements";
		case UiTab::System: return "system";
	}
	return "settings";
}

static const char* TypeName(ValueType type)
{
	switch (type)
	{
		case ValueType::Boolean: return "boolean";
		case ValueType::Choice: return "choice";
		case ValueType::Text: return "text";
		case ValueType::PathList: return "path-list";
		case ValueType::Number: return "number";
		case ValueType::MemoryCardFile: return "memory-card-file";
		case ValueType::GroupList: return "group-list";
	}
	return "choice";
}

static const char* PresentationName(Presentation presentation)
{
	switch (presentation)
	{
		case Presentation::Toggle: return "toggle";
		case Presentation::Segmented: return "segmented";
		case Presentation::ChoiceList: return "choice-list";
		case Presentation::Text: return "text";
		case Presentation::Composite: return "composite";
		case Presentation::Hidden: return "hidden";
	}
	return "segmented";
}

static const char* ApplyName(ApplyTiming timing)
{
	switch (timing)
	{
		case ApplyTiming::Live: return "live";
		case ApplyTiming::NextLaunch: return "next-launch";
		case ApplyTiming::NextApplicationStart: return "next-application-start";
		case ApplyTiming::NextMenuOpen: return "next-menu-open";
		case ApplyTiming::ImmediateUi: return "immediate-ui";
		case ApplyTiming::RunningCommand: return "running-command";
	}
	return "live";
}

static std::string ScopeJson(ScopeMask scopes)
{
	std::string out = "[";
	if (HasScope(scopes, ScopeMask::Global)) out += "\"global\"";
	if (HasScope(scopes, ScopeMask::Game)) out += std::string(out.size() > 1 ? "," : "") + "\"game\"";
	if (HasScope(scopes, ScopeMask::RunningVm)) out += std::string(out.size() > 1 ? "," : "") + "\"running\"";
	return out + "]";
}

std::string RegistryToJson(bool include_hints)
{
	std::string out = "{\"schema_version\":" + std::to_string(kSchemaVersion) + ",\"groups\":[";
	bool first = true;
	for (const GroupDef& group : Groups())
	{
		out += (first ? "" : ",");
		first = false;
		out += "{\"id\":\"" + JsonEscape(group.id) + "\",\"title\":\"" + JsonEscape(group.title) + "\",\"tab\":\"" + TabName(group.tab) +
		       "\",\"scopes\":" + ScopeJson(group.scopes) + ",\"order\":" + std::to_string(group.order);
		if (include_hints && !group.hint.empty())
			out += ",\"hint\":\"" + JsonEscape(group.hint) + "\"";
		out += "}";
	}
	out += "],\"settings\":[";
	first = true;
	for (const SettingDef& def : Settings())
	{
		out += (first ? "" : ",");
		first = false;
		out += "{\"id\":\"" + JsonEscape(def.id) + "\",\"key\":\"" + JsonEscape(def.compatibility_key) + "\",\"label\":\"" + JsonEscape(def.label) +
		       "\",\"group\":\"" + JsonEscape(def.group_id) + "\",\"tab\":\"" + TabName(def.tab) + "\",\"scopes\":" + ScopeJson(def.scopes) +
		       ",\"inheritance\":\"" + std::string(def.inheritance == Inheritance::InheritGlobal ? "inherit-global" : "fixed") +
		       "\",\"type\":\"" + TypeName(def.type) + "\",\"presentation\":\"" + PresentationName(def.presentation) + "\",\"apply\":\"" +
		       ApplyName(def.apply_timing) + "\",\"default\":\"" + JsonEscape(def.default_value) + "\"";
		if (!def.choices.empty())
		{
			out += ",\"choices\":[";
			for (size_t i = 0; i < def.choices.size(); ++i)
				out += std::string(i ? "," : "") + "{\"value\":\"" + JsonEscape(def.choices[i].value) + "\",\"label\":\"" + JsonEscape(def.choices[i].label) + "\"}";
			out += "]";
		}
		if (include_hints && !def.hint.empty()) out += ",\"hint\":\"" + JsonEscape(def.hint) + "\"";
		if (!def.short_format.empty()) out += ",\"short\":\"" + JsonEscape(def.short_format) + "\"";
		if (!def.later_label.empty()) out += ",\"later\":\"" + JsonEscape(def.later_label) + "\"";
		if (!def.placeholder.empty()) out += ",\"placeholder\":\"" + JsonEscape(def.placeholder) + "\"";
		if (def.columns) out += ",\"columns\":" + std::to_string(def.columns);
		if (def.number_max > def.number_min)
			out += ",\"min\":" + std::to_string(def.number_min) + ",\"max\":" + std::to_string(def.number_max) + ",\"step\":" + std::to_string(def.number_step);
		if (def.allow_unlisted) out += ",\"allow_unlisted\":true";
		if (def.dynamic_choices) out += ",\"dynamic_choices\":true";
		if (def.manual_hardware_fix) out += ",\"manual_hardware_fix\":true";
		if (def.manual_hardware_fix_master) out += ",\"manual_hardware_fix_master\":true";
		if (def.player_owned) out += ",\"player_owned\":true";
		if (def.implementation_detail) out += ",\"implementation_detail\":true";
		if (def.list_key) out += ",\"list_key\":true";
		if (!def.pair_id.empty()) out += ",\"pair\":\"" + JsonEscape(def.pair_id) + "\",\"pair_part\":" + std::to_string(def.pair_part);
		if (!def.note.empty()) out += ",\"note\":\"" + JsonEscape(def.note) + "\"";
		out += "}";
	}
	return out + "]}";
}

static const char* DomainName(ActionDomain domain)
{
	switch (domain)
	{
		case ActionDomain::Library: return "library";
		case ActionDomain::Settings: return "settings";
		case ActionDomain::Storage: return "storage";
		case ActionDomain::Patches: return "patches";
		case ActionDomain::Textures: return "textures";
		case ActionDomain::Runtime: return "runtime";
		case ActionDomain::Achievements: return "achievements";
		case ActionDomain::Diagnostics: return "diagnostics";
		case ActionDomain::Accessibility: return "accessibility";
		case ActionDomain::Presentation: return "presentation";
	}
	return "settings";
}

std::string ActionsToJson(bool include_descriptions)
{
	std::string out = "{\"schema_version\":" + std::to_string(kSchemaVersion) + ",\"actions\":[";
	bool first = true;
	for (const ActionDef& action : Actions())
	{
		out += (first ? "" : ",");
		first = false;
		out += "{\"id\":\"" + JsonEscape(action.id) + "\",\"label\":\"" + JsonEscape(action.label) + "\",\"domain\":\"" + DomainName(action.domain) +
		       "\",\"scopes\":" + ScopeJson(action.scopes);
		if (include_descriptions && !action.description.empty()) out += ",\"description\":\"" + JsonEscape(action.description) + "\"";
		if (!action.command_type.empty()) out += ",\"command\":\"" + JsonEscape(action.command_type) + "\"";
		if (!action.payload.empty()) out += ",\"payload\":\"" + JsonEscape(action.payload) + "\"";
		if (action.confirmation == Confirmation::TwoPress)
			out += ",\"confirmation\":\"two-press\",\"confirm_seconds\":" + std::to_string(action.confirm_seconds);
		if (action.destructive) out += ",\"destructive\":true";
		if (action.requires_running_vm) out += ",\"requires_running_vm\":true";
		if (action.requires_game) out += ",\"requires_game\":true";
		if (action.requires_elf) out += ",\"requires_elf\":true";
		if (action.global_only) out += ",\"global_only\":true";
		if (action.game_only) out += ",\"game_only\":true";
		if (action.long_running) out += ",\"long_running\":true";
		if (action.writes_settings) out += ",\"writes_settings\":true";
		if (action.ui_local) out += ",\"ui_local\":true";
		out += "}";
	}
	return out + "]}";
}

} // namespace caliza::settings

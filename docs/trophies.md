# Local trophies

Trophies has its own title-screen button (Y) and pause-menu entry. It records 36 local awards:
22 bronze, 8 silver, 5 gold and a platinum reward for earning all the others.
Move through four entries per page. Each row has a tier-coloured cup and a mixed-case name. The selected row uses the normal menu selection colour. Locked names are grey. The selected requirement
appears below the list; gold awards and story spoilers show `???` instead. Unlock notifications appear at the
bottom-right of the top screen, with a compact dark panel, blue frame, cup and two left-aligned text lines. Each popup plays the supplied achievement sound through the selected DSP backend and respects the SFX/master volume. There is no PlayStation account integration.

Progress is shared between save slots and stored separately in `trophies.dat`
in the game folder. Existing saves can earn trophies whose requirements are
still represented in their game state. Coin awards use the current coin count;
past coin collections cannot be reconstructed from a save. The attract demo
is excluded. File replacement preserves the previous record if writing fails.

The award set follows the PS3 Sly Collection release. Descriptions are shortened
for the 3DS screen. Sucker Punched is earned by starting this game, rather than
selecting it from the PS3 collection launcher.

References:
- https://www.playstationtrophies.org/game/sly-cooper-collection-sly-1/trophies/
- https://strategywiki.org/wiki/The_Sly_Collection/Sly_1_trophies

Conker3DSRecomp is the reference for the popup placement and compact cup
design. The popup occupies 212 x 40 logical pixels, six pixels from the
top screen's bottom-right corner, and lasts six seconds. It uses Sly's font
and the retail menu palette, with a blue frame. No Conker game
textures or fonts are bundled.

The browser uses the retail prompt renderer for its frame, title, scaling and
opening transition. Text follows the normal menu palette and selection pulse.

Options > Debug > Reset trophies clears the local trophy record after confirmation.
It does not erase game saves. Current conditions are suppressed until they stop
being true during this session; loading an existing save on a later launch can
earn awards again from its saved progress.

Options > Debug > Trophy popups enables or disables the popup and its jingle.
Trophies still unlock and save; suppressed notifications are not queued for later.
This preference is stored in native-options.cfg and defaults to Off.

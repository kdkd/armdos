/* altcontrol.c - ARM-DOS replacement for ReflectionHLE's kdreams/altcontroller.c.
 * ReflectionHLE maps game pads and touch screens onto the game's keys
 * ("alternative controller schemes"); an ARM PC has a keyboard (and maybe a
 * mouse), so the mappings are empty and nothing is ever bound: the game
 * reads the keyboard exactly as KDREAMS.EXE did. GPL-2+ like the game code. */
#include "refkeen.h"
#include "altcontroller.h"

int g_binding_value_button[2], g_binding_value_stats,
    g_binding_value_up, g_binding_value_down, g_binding_value_left, g_binding_value_right;
bool g_keybind_used_button[2], g_keybind_used_stats,
     g_keybind_used_up, g_keybind_used_down, g_keybind_used_left, g_keybind_used_right;

BE_ST_ControllerMapping g_ingame_altcontrol_mapping_inackback, g_ingame_altcontrol_mapping_notenoughmemorytostart,
    g_ingame_altcontrol_mapping_simpledialog, g_ingame_altcontrol_mapping_menu_help, g_ingame_altcontrol_mapping_menu,
    g_ingame_altcontrol_mapping_demoloop, g_ingame_altcontrol_mapping_gameplay, g_ingame_altcontrol_mapping_funckeys;

void PrepareGamePlayControllerMapping(void) {}
void FinalizeControlPanelMappingsByMousePresence(bool withmouse) { (void)withmouse; }
void UpdateGameplayMappingsByMousePresence(bool withmouse) { (void)withmouse; }
void RefKeen_PrepareAltControllerScheme(void) {}
BE_ST_ControllerMapping g_beStControllerMappingTextInput, g_beStControllerMappingDebugKeys;

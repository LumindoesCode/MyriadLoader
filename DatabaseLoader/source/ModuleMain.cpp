#define SOL_ALL_SAFETIES_ON 1
#include <YYToolkit/YYTK_Shared.hpp>
#include "ModuleMain.h"
#include "DatabaseLoader.h"
#include <sol/sol.hpp>
#include "DBLua.h"
#include "Files.h"
#include <string>
#include <windows.h>
#include <shlobj_core.h>
#include "GMHooks.h"
#include <filesystem>
#include <iostream>
#include "SaverLoader.h"
#include <fstream>
#include <thread>

#pragma comment(lib, "lua54.lib")

using namespace Aurie;
using namespace DatabaseLoader;

static DLInterface* dl_interface = nullptr;
static YYTK::YYTKInterface* yytk_interface = nullptr;
void HandleBoss(int id, string bossName, sol::table data);
void HandleMiniboss(int id, string minibossName);
void HandleEnemy(int id, string enemyName);
void HandleEnemyType(string enemyName, sol::table data);
void HandleFloormap(string floorName, sol::table data);
void HandleCartridges(string name, sol::table data);
void GamestateBehaviorRun(size_t currentState);
void ScreenBehaviorRun(CInstance* currentInstance, size_t currentState);
void PlayerBehaviorRun(size_t currentState);
void CheckForAllBehavior(size_t currentState);
void SortAllBehaviors(sol::table allBehaviors, size_t currentState);
void HandleAllExistingTypes(sol::table allBehaviorsParameters);
void HandleAllEnemyTypes(sol::table allBehaviorParameters);
void HandleGlobalTypes(sol::table tbl, double var, size_t currentState);
void ClearData();

static sol::table CopyTableFromStateTo(sol::state& source, sol::state& target, sol::table table_to_copy) {

	sol::table new_table = target.create_table();
	for (auto it = table_to_copy.begin(); it != table_to_copy.end(); ++it) {

		auto [key, value] = *it;

		new_table.set(key, table_to_copy.copy(target)); // Recursively copy nested values if needed [6, 8]
	}
	return new_table;
}


string GetUserDirectory() {
	char path[MAX_PATH];
	if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path) == S_OK) {
		return string(path);
	}
	else {
		return "";
	}
}

string bossListNum;
RValue bossListCopy;

#pragma region DataHandling
static void RegisterData(lua_State* state, sol::table data)
{
	sol::state_view sview(state);
	sol::table tbl = sview["all_behaviors"];

	tbl[tbl.size() + 1] = data;

	RValue objectType = g_YYTKInterface->CallBuiltin("asset_get_index", { (string_view)data.get<string>("Name") });

	string getName = data.get<string>("Name");
	string getType = data.get<string>("DataType");

	if (!g_YYTKInterface->CallBuiltin("object_exists", { objectType }))
	{
		if (getType == "enemy" && getName != "all")
		{
			
			HandleEnemyType(getName, data);
		}

		if (getType == "cartridge" && getName != "all")
		{
			HandleCartridges(getName, data);
		}

		if (getType == "floormap" && getName != "all")
		{
			HandleFloormap(getName, data);
		}
    }

}

#pragma region HandleEnemies
//Processes EnemyData and sorts it to the correct list.
void HandleEnemyType(string enemyName, sol::table data) 
{
	int id = Files::HashString(enemyName);
		
	if (data.get<bool>("Boss") == true)
	{
		HandleBoss(id, enemyName, data);
	}
	else if (data.get<bool>("Miniboss") == true)
	{
		HandleMiniboss(id, enemyName);
	}
	else if (std::find(customEnemyNames.begin(), customEnemyNames.end(), enemyName) == customEnemyNames.end())
	{
		HandleEnemy(id, enemyName);
	}
}

//Adds a custom boss data to the game.
void HandleBoss(int id, string bossName, sol::table data)
{
	if (std::find(customBossNames.begin(), customBossNames.end(), bossName) == customBossNames.end())
	{
		g_YYTKInterface->CallBuiltin("array_set", { GMWrappers::GetGlobal("gen_list"), id, id });
		customBossNames.push_back(bossName);

		string bosslist = "bosslist_" + to_string(data.get<int>("BossFloor"));

		if (g_YYTKInterface->CallBuiltin("variable_global_exists", { (string_view)bosslist }))
		{
			g_YYTKInterface->CallBuiltin("ds_list_add", { GMWrappers::GetGlobal(bosslist), id });
			g_YYTKInterface->Print(CM_LIGHTPURPLE, "[Myriad Loader] Boss '" + bossName + "' (numeric ID " + to_string(id) + ") implemented for floor " + to_string(data.get<int>("BossFloor")));
		}
		else
		{
			g_YYTKInterface->Print(CM_LIGHTPURPLE, "[Myriad Loader] Created boss '" + bossName + "' with numeric ID: " + to_string(id) + " using custom spawning logic");
		}
	}
}

//Adds a custom miniboss data to the game.
void HandleMiniboss(int id, string minibossName) 
{
	if (std::find(customMinibossNames.begin(), customMinibossNames.end(), minibossName) == customMinibossNames.end())
	{
		g_YYTKInterface->CallBuiltin("array_set", { GMWrappers::GetGlobal("gen_list"), id, id });
		customMinibossNames.push_back(minibossName);
		g_YYTKInterface->Print(CM_LIGHTPURPLE, "[Myriad Loader] Created miniboss '" + minibossName + "' with numeric ID: " + to_string(id));
	}
}

//Adds a custom enemy data to the game.
void HandleEnemy(int id, string enemyName) 
{
	g_YYTKInterface->CallBuiltin("array_set", { GMWrappers::GetGlobal("gen_list"), id, id });
	customEnemyNames.push_back(enemyName);
	g_YYTKInterface->Print(CM_LIGHTPURPLE, "[Myriad Loader] Created enemy '" + enemyName + "' with numeric ID: " + to_string(id));
}
#pragma endregion HandleEnemies

//Adds a cartridge data to the game.
void HandleCartridges(string name, sol::table data)
{
	string getShownName = data.get<string>("ShownName");
	string getDescription = data.get<string>("Description");
	int id = Files::HashString(name);

	if (std::find(customCartridgeNames.begin(), customCartridgeNames.end(), name) == customCartridgeNames.end())
	{

		g_YYTKInterface->CallBuiltin("array_set", { GMWrappers::GetGlobal("gen_list"), id, id });
		g_YYTKInterface->CallBuiltin("array_set", { GMWrappers::GetGlobal("cart_name"), id, g_YYTKInterface->CallBuiltin("array_create", { 2, (string_view)getShownName }) });
		g_YYTKInterface->CallBuiltin("array_set", { GMWrappers::GetGlobal("cart_desc"), id, g_YYTKInterface->CallBuiltin("array_create", { 2, (string_view)getDescription }) });

		customCartridgeNames.push_back(name);


		g_YYTKInterface->Print(CM_LIGHTPURPLE, "[Myriad Loader] Created cartridge '" + name + "' with numeric ID: " + to_string(id));
	}
}

//Adds a custom floor data to the game.
void HandleFloormap(string floorName, sol::table data)
{
	
	
		//string floorRooms = Files::GetModsDirectory() + data.get<string>("Rooms");
		//string floorRoomsDestiny = data.get<string>("RoomsDestination");
		//string roomsDirectory = "rooms/";

		double bossList = data.get<double>("BossList");

		int id = Files::HashString(floorName);

		if (std::find(customFloorNames.begin(), customFloorNames.end(), floorName) == customFloorNames.end())
		{
			RValue floordsmap = g_YYTKInterface->CallBuiltin("ds_map_create", {});
			string floormapnum = "floormap_" + to_string(data.get<int>("Floor"));

			g_YYTKInterface->CallBuiltin("ds_map_set", { floordsmap, "index", id });

			/*
			if (bossList > 0 && g_YYTKInterface->CallBuiltin("ds_map_find_value", { GMWrappers::GetGlobal("current_floormap"), "index" }).ToDouble() == g_YYTKInterface->CallBuiltin("ds_map_find_value", { floordsmap, "index" }).ToDouble())
			{
				string bossListNum = "bosslist_" + to_string(data.get<int>("Floor"));
				RValue bossListCopy = g_YYTKInterface->CallBuiltin("ds_list_copy", { bossListCopy, GMWrappers::GetGlobal(bossListNum) });

				g_YYTKInterface->CallBuiltin("ds_list_clear", { GMWrappers::GetGlobal(bossListNum) });
				g_YYTKInterface->CallBuiltin("ds_list_add", { GMWrappers::GetGlobal(bossListNum), bossList});

				g_YYTKInterface->CallBuiltin("ds_map_replace", { floordsmap, "boss", (string_view)bossListNum});
			}*/

			if (g_YYTKInterface->CallBuiltin("variable_global_exists", { (string_view)floormapnum }))
			{
				g_YYTKInterface->CallBuiltin("ds_map_set", { GMWrappers::GetGlobal(floormapnum), "next", floordsmap });
				g_YYTKInterface->Print(CM_LIGHTPURPLE, "[Myriad Loader] Floor '" + floorName + "' (numeric ID " + to_string(id) + ") implemented for floor " + to_string(data.get<int>("Floor")));
			}
			else
			{
				g_YYTKInterface->Print(CM_LIGHTPURPLE, "[Myriad Loader] Created floor '" + floorName + "' with numeric ID: " + to_string(id) + " using custom spawning logic");
			}


			customFloorNames.push_back(floorName);
		}
	
}

void DatabaseLoader::UnloadMods()
{
	for (size_t i = 0; i < modState.size(); i++)
	{
		currentState = i;

		modState[currentState]["mod_unload"].call();

		//lua_close(modState[currentState]);

		g_YYTKInterface->Print(CM_LIGHTBLUE, "[Myriad Loader] Unloaded mod " + modState[currentState]["mod_name"].get<string>());
	}

	if (!bossListNum.empty())
	{
		g_YYTKInterface->CallBuiltin("ds_list_copy", { GMWrappers::GetGlobal(bossListNum), bossListCopy });
	}

	ClearData();
}

void ClearData() 
{
	roomFiles.clear();
	customEnemyNames.clear();
	customMinibossNames.clear();
	customBossNames.clear();
	customCartridgeNames.clear();
	customFloorNames.clear();
	modState.clear();
}
#pragma endregion DataHandling

// Helper to bind the state_ptr and pass functions to lua
// Sol is unable to properly examine the result std::bind front
// but it can examine this
template <typename Ret, typename... Args, typename BoundArg>
auto bind_one(Ret(*func)(BoundArg, Args...), BoundArg bound) {
	return std::function<Ret(Args...)>(
		[=](Args... args) {
			return func(bound, args...);
		}
	);
}

sol::state DatabaseLoader::MakeModState()
{
	sol::state inState;

	lua_State* const state_ptr = inState.lua_state();

	inState.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::math, sol::lib::string);

	inState["modName"] = "";

	inState["debug_out"] = [](string text) {
		yytk_interface->PrintInfo(text);
		};

	inState["object_behaviors"] = inState.create_table();

	inState["hard_mode"] = false;
	inState["paused"] = false;
	inState["loop"] = 0;
	inState["view_x"] = 0;
	inState["view_y"] = 0;
	inState["player"] = 0;
	inState["player_x"] = 0;
	inState["player_y"] = 0;
	inState["player_dead"] = false;
	inState["screen_center_x"] = 0;
	inState["screen_center_y"] = 0;

	inState["enemy_data"] = bind_one(DBLua::EnemyData, state_ptr);
	//inState["cartridge_data"] = bind_one(DBLua::CartridgeData, state_ptr);
	inState["projectile_data"] = bind_one(DBLua::ProjectileData, state_ptr);
	inState["global_data"] = bind_one(DBLua::GlobalData, state_ptr);
	inState["player_data"] = bind_one(DBLua::PlayerData, state_ptr);
	//inState["custom_floor"] = bind_one(DBLua::FloorData, state_ptr);

	inState["register_data"] = bind_one(RegisterData, state_ptr);

	inState["spawn_particle"] = DBLua::SpawnParticle;

	inState["init_var"] = DBLua::InitVar;

	inState["set_var"] = DBLua::SetVar;

	inState["get_var"] = bind_one(DBLua::GetVar, state_ptr);

	inState["init_global"] = DBLua::InitGlobal;

	inState["set_global"] = DBLua::SetGlobal;

	inState["get_global"] = bind_one(DBLua::GetGlobal, state_ptr);

	inState["init_number"] = DBLua::InitVar;

	inState["init_bool"] = DBLua::InitVar;

	inState["init_string"] = DBLua::InitVar;

	inState["set_number"] = DBLua::SetVar;

	inState["set_bool"] = DBLua::SetVar;

	inState["set_string"] = DBLua::SetVar;

	inState["get_number"] = bind_one(DBLua::GetVar, state_ptr);

	inState["get_bool"] = bind_one(DBLua::GetVar, state_ptr);

	inState["get_string"] = bind_one(DBLua::GetVar, state_ptr);

	inState["get_ds_map"] = bind_one(DBLua::GetDSMap, state_ptr);

	inState["get_ds_list"] = bind_one(DBLua::GetDSList, state_ptr);

	inState["custom_sprite"] = DBLua::GetCustomSprite;

	inState["custom_sound"] = DBLua::GetCustomSound;

	inState["custom_music"] = DBLua::GetCustomMusic;

	//inState["unlock_song"] = DBLua::UnlockSong;

	inState["get_asset"] = DBLua::GetAsset;

	inState["call_function"] = bind_one(DBLua::CallFunction, state_ptr);

	inState["call_game_function"] = DBLua::CallGameFunction;

	inState["play_sound"] = DBLua::DoSound;

	inState["play_sound_ext"] = DBLua::DoSoundExt;

	inState["play_music"] = DBLua::DoMusic;

	inState["boss_message"] = DBLua::ShowBossMessage;

	inState["draw_sprite"] = DBLua::DrawSprite;

	inState["draw_sprite_ext"] = DBLua::DrawSpriteExt;

	inState["draw_primitive_begin_texture"] = DBLua::DrawPrimitiveBeginTexture;
	inState["draw_vertex_texture"] = DBLua::DrawVertexTexture;
	inState["draw_primitive_begin"] = DBLua::DrawPrimitiveBeginSolid;
	inState["draw_vertex_color"] = DBLua::DrawVertexColor;
	inState["draw_primitive_end"] = DBLua::DrawVertexEnd;

	inState["draw_sprite"] = DBLua::DrawSprite;

	inState["draw_text"] = DBLua::DrawString;

	inState["draw_rectangle"] = DBLua::DrawRect;

	inState["draw_set_depth"] = DBLua::DrawSetDepth;

	inState["save_data"] = SaverLoader::SaveVariable;

	inState["load_data"] = SaverLoader::LoadVariable;

	inState["create_color"] = DBLua::CreateColor;
	inState["create_colour"] = DBLua::CreateColor;

	inState["draw_set_color"] = DBLua::DrawSetColor;
	inState["draw_set_colour"] = DBLua::DrawSetColor;

	inState["spawn_object"] = DBLua::SpawnObject;

	inState["spawn_boss_intro"] = DBLua::SpawnBossIntro;

	inState["kill_boss_effect"] = DBLua::KillBoss;

	inState["add_screenshake"] = DBLua::AddScreenshake;

	inState["clear_bullets"] = DBLua::ClearBullets;

	inState["spawn_projectile"] = DBLua::SpawnProjectile;

	inState["spawn_laser"] = DBLua::SpawnLaser;

	//inState["get_direction"] = DBLua::DirectionTo;

	inState["add_rooms_to"] = DBLua::AddRoomsTo;

	//inState["add_bestiary_entry"] = DBLua::AddBestiaryEntry;

	inState["check_cart"] = DBLua::CheckCart;

	inState["error_handler"] = [state_ptr](std::string err) {
		g_YYTKInterface->PrintWarning("An Error occured : %s", err.c_str());
		g_YYTKInterface->PrintWarning("Traceback");

		lua_Debug dbg_info;
		for (int level = 1;lua_getstack(state_ptr, level, &dbg_info);level++) {
			lua_getinfo(state_ptr, "nSl", &dbg_info);
			std::string source = dbg_info.source;
			if (source[0] == '@') {
				auto pos = source.find("Mods");
				if (pos != std::string::npos)
					source.erase(0, pos + 4);
				g_YYTKInterface->PrintWarning("at %s:%d", source.c_str(), dbg_info.currentline);
			} else {
				g_YYTKInterface->PrintWarning("builtin");
			}
			if (dbg_info.name == nullptr) {
				dbg_info.name = "EntryPoint";
			}
			g_YYTKInterface->PrintWarning("    in %s", dbg_info.name);
		}

		return err;
	};

	sol::protected_function::set_default_handler(inState["error_handler"]);

	return inState;
}

static void RestoreRoomFiles()
{
	for (size_t i = 0; i < roomFiles.size(); i++)
	{
		Files::CopyFileTo(roomFiles.at(i).backupName, roomFiles.at(i).destinationName);
	}
}

#pragma region ObjectBehaviors
//Handles Code of Injected Objects
void ObjectBehaviorRun(FWFrame& context)
{
	UNREFERENCED_PARAMETER(context);
	CInstance* GlobalInstance;
	g_YYTKInterface->GetGlobalInstance(&GlobalInstance);
	
	for (size_t stateNum = 0; stateNum < modState.size(); stateNum++)
	{
		g_YYTKInterface->CallBuiltin("audio_sound_gain", { GMWrappers::GetGlobal("current_music"), GMWrappers::GetGlobal("volume_music"), 0 });
		GamestateBehaviorRun(stateNum);
		ScreenBehaviorRun(GlobalInstance, stateNum);
		PlayerBehaviorRun(stateNum);
		CheckForAllBehavior(stateNum);
	}
};

#pragma region SingleBehaviors
//Handles the game states (loop, hardmode, pause...)
void GamestateBehaviorRun(size_t currentState) 
{
	modState.at(currentState)["hard_mode"] = GMWrappers::GetGlobal("hardmode").ToBoolean();
	modState.at(currentState)["paused"] = g_YYTKInterface->CallBuiltin("instance_exists", { g_YYTKInterface->CallBuiltin("asset_get_index", {"obj_pause"}) }).ToBoolean();
	modState.at(currentState)["loop"] = GMWrappers::GetGlobal("game_loop");
}

//Handles the current ScreenView
void ScreenBehaviorRun(CInstance* currentInstance, size_t currentState) 
{
	RValue view;
	g_YYTKInterface->GetBuiltin("view_current", currentInstance, 0, view);
	RValue viewCamera = g_YYTKInterface->CallBuiltin("view_get_camera", { view });

	modState.at(currentState)["view_x"] = g_YYTKInterface->CallBuiltin("camera_get_view_x", { viewCamera }).ToDouble();
	modState.at(currentState)["screen_center_x"] = modState.at(currentState).get<double>("view_x") + 120;
	modState.at(currentState)["view_y"] = g_YYTKInterface->CallBuiltin("camera_get_view_y", { viewCamera }).ToDouble();
	modState.at(currentState)["screen_center_y"] = modState.at(currentState).get<double>("view_y") + 160;
}

//Handles the current Player
void PlayerBehaviorRun(size_t currentState) 
{
	RValue playerAsset = g_YYTKInterface->CallBuiltin("asset_get_index", { "obj_player" });
	RValue player = g_YYTKInterface->CallBuiltin("instance_find", { playerAsset, 0 });

	modState.at(currentState)["player_dead"] = false;

	if (g_YYTKInterface->CallBuiltin("instance_exists", { player.ToDouble() }))
	{
		modState.at(currentState)["player"] = player.ToDouble();
		modState.at(currentState)["player_x"] = g_YYTKInterface->CallBuiltin("variable_instance_get", { player, "x" }).ToDouble();
		modState.at(currentState)["player_y"] = g_YYTKInterface->CallBuiltin("variable_instance_get", { player, "y" }).ToDouble();
	}
	else
	{
		modState.at(currentState)["player_dead"] = true;
	}
}

#pragma endregion SingleBehaviors
#pragma region AllBehaviors
//Checks if the received state is an All Behavior.
void CheckForAllBehavior(size_t currentState)
{
	if (modState.at(currentState)["all_behaviors"])
	{
		sol::table count = modState.at(currentState)["all_behaviors"];
		SortAllBehaviors(count, currentState);
	}
}

//Sorts the "All Behavior" to the right data type.
void SortAllBehaviors(sol::table allBehaviors, size_t currentState) 
{
	for (double var = 0; var < allBehaviors.size() + 1; var++)
	{
		sol::table tbl = modState.at(currentState)["all_behaviors"][var];
		if (modState.at(currentState)["all_behaviors"][var])
		{
			
			HandleAllExistingTypes(tbl);
			HandleAllEnemyTypes(tbl);
			HandleGlobalTypes(tbl, var, currentState);
			
		}
	}
}

//Handles all the existing types in Star of Providence, enemies and projectiles alike.
void HandleAllExistingTypes(sol::table allBehaviorsParameters)
{
	if (g_YYTKInterface->CallBuiltin("object_exists", { g_YYTKInterface->CallBuiltin("asset_get_index", {(string_view)allBehaviorsParameters.get<string>("Name")}) }))
	{
		if (allBehaviorsParameters.get<string>("DataType") == "enemy" || allBehaviorsParameters.get<string>("DataType") == "projectile")
		{
			DBLua::InvokeWithObjectIndex(allBehaviorsParameters.get<string>("Name"), allBehaviorsParameters["Step"]);
		}
	}
}

//Handles "All Behaviors" for enemy types
void HandleAllEnemyTypes(sol::table allBehaviorParameters) 
{
	if (allBehaviorParameters.get<string>("Name") == "all")
	{
		if (allBehaviorParameters.get<string>("DataType") == "enemy")
		{
			DBLua::InvokeWithObjectIndex("obj_enemy", allBehaviorParameters["Step"]);
			
		}
	}
}

//Handles global behaviors 
void HandleGlobalTypes(sol::table tbl, double var, size_t currentState) 
{
	if (tbl.get<string>("DataType") == "global")
	{
		modState.at(currentState)["all_behaviors"][var]["Step"].call();
	}
}
#pragma endregion AllBehaviors

#pragma endregion ObjectBehaviors

double degreesToRadians(double degrees) {
	return degrees * 3.14159265358979323846 / 180.0;
}

void DrawLoadingScreen(FWCodeEvent& context)
{
	CCode* Code = std::get<2>(context.Arguments());

	if ((string)Code->GetName() == "gml_Object_obj_screen_Draw_64" && loadingMods)
	{
		static double rotation = 0;
		static double dist = 0;
		static double frame = 0;
		CInstance* Self = std::get<0>(context.Arguments());
		RValue Instance = Self->ToRValue();

		frame += 0.2;
		rotation += sin(frame / 10) * 0.06;
		dist = lerp(dist, 15, 0.3);
		for (size_t i = 0; i < 6; i++)
		{
			double x = 120;
			g_YYTKInterface->CallBuiltin("draw_sprite", { g_YYTKInterface->CallBuiltin("asset_get_index", {"spr_myriad"}), frame + i, x + (cos(rotation + degreesToRadians(i * 60)) * dist), 40 + (-sin(rotation + degreesToRadians(i * 60)) * dist)});
			g_YYTKInterface->CallBuiltin("draw_set_font", { GMWrappers::GetGlobal("font_nes") });
			g_YYTKInterface->CallBuiltin("draw_set_halign", { 1 });
			g_YYTKInterface->CallBuiltin("draw_text", { x, 60, "loading mods..." });
		}
	}
}

RValue ObjectToRValue(sol::object obj)
{
	RValue val = 0;

	switch (obj.get_type())
	{
	case sol::lua_type_of_v<double>:
		return obj.as<double>();
	case sol::lua_type_of_v<bool>:
		return obj.as<bool>();
	case sol::lua_type_of_v<string>:
		return obj.as<string_view>();
	default:
		break;
	}

	return 0;
}

int DatabaseLoader::LoadFileRequire(lua_State* L)
{
	std::string path = sol::stack::get<std::string>(L);

	std::string script = Files::GetFileContents(Files::GetModsDirectory() + path + ".lua");
	std::string script2 = Files::GetFileContents(Files::GetModsDirectory() + path);

	if (script != "")
	{
		g_YYTKInterface->Print(CM_LIGHTGREEN, "[Myriad Loader] Loaded module " + path);
		luaL_loadbuffer(L, script.data(), script.size(), path.c_str());
	}
	else if (script2 != "")
	{
		g_YYTKInterface->Print(CM_LIGHTGREEN, "[Myriad Loader] Loaded module " + path);
		luaL_loadbuffer(L, script2.data(), script2.size(), path.c_str());
	}
	else
	{
		g_YYTKInterface->Print(CM_LIGHTRED, "[Myriad Loader] Could not load module: " + path);
	}

	return 1;
}

HWINEVENTHOOK closeWindowHook;

void HandleWindowEvent(DWORD event, HWND hwnd) {
	if (event == EVENT_OBJECT_DESTROY) {
		UnloadMods();
	}
}

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
	if (idObject == OBJID_WINDOW) {
		HandleWindowEvent(event, hwnd);
	}
}

BOOL(*g_WriteFileTrampoline)(
	IN HANDLE,
	IN LPCVOID,
	IN DWORD,
	OPTIONAL OUT LPDWORD,
	IN OUT OPTIONAL LPOVERLAPPED
	) = nullptr;

BOOL WINAPI WriteFileHook(
	IN HANDLE File,
	IN LPCVOID Buffer,
	IN DWORD NumberOfBytesToWrite,
	OPTIONAL OUT LPDWORD NumberOfBytesWritten,
	IN OUT OPTIONAL LPOVERLAPPED OverlapInformation
)
{
	if (File == GetStdHandle(STD_OUTPUT_HANDLE) ||
		File == GetStdHandle(STD_ERROR_HANDLE))
	{
		SetLastError(ERROR_FILE_NOT_FOUND);
		return false;
	}

	return g_WriteFileTrampoline(File, Buffer, NumberOfBytesToWrite, NumberOfBytesWritten, OverlapInformation);
}

void DatabaseLoader::LoadMods()
{
	loadingMods = true;

	string dir = Files::GetModsDirectory();
	string savedir = Files::GetModSavesDirectory();
	string rooms = Files::GetSteamDirectory() + "rooms/";
	string roomsBackup = Files::GetSteamDirectory() + "rooms/backup/";

	Files::MakeDirectory(dir);
	Files::MakeDirectory(savedir);
	Files::MakeDirectory(roomsBackup);

	vector<filesystem::path> mods = Files::GetImmediateSubfolders(dir);

	for (size_t i = 0; i < mods.size(); i++)
	{
		auto state = MakeModState();

		currentState = i;

		state.clear_package_loaders();
		state.add_package_loader(LoadFileRequire);

		// We should prevent users from overriding these
		state["all_behaviors"] = state.create_table();
		state["mod_name"] = mods[i].filename().string();

		if (std::filesystem::exists(mods[i].string() + "/main.lua"))
		{
			sol::protected_function_result pfr = state.safe_script_file(mods[i].string() + "/main.lua", [&](lua_State* state, sol::protected_function_result pfr) {
				sol::error err = pfr;
				g_YYTKInterface->PrintWarning("Error while loading mod %s : %s", mods[i].filename().string().c_str(), err.what());
				return pfr;
			});
			if (!pfr.valid())
				continue;

			if (! state["mod_load"].call().valid() )
				continue;
		}

		modState.push_back(std::move(state));

		g_YYTKInterface->Print(CM_LIGHTBLUE, "[Myriad Loader] Loaded mod " + mods[i].filename().string());
	}

	for (size_t i = 0; i < roomFiles.size(); i++)
	{
		Files::CopyFileTo(roomFiles.at(i).destinationName, roomFiles.at(i).backupName);
		Files::AddRoomsToFile(roomFiles.at(i).sourceName, roomFiles.at(i).destinationName);
	}

	int size = g_YYTKInterface->CallBuiltin("array_length", { GMWrappers::GetGlobal("gen_list") }).ToInt64();

	for (size_t i = size; i <= 15000; i++)
	{
		g_YYTKInterface->CallBuiltin("array_set", { GMWrappers::GetGlobal("gen_list"), i, i });
	}

	GMWrappers::CallGameScript("gml_Script_load_room_files", {});

	RestoreRoomFiles();

	loadingMods = false;
}

//Handle all hooks attached to Star of Providence.
static void RegisterHooks(AurieModule* Module) {
	yytk_interface->CreateCallback(
		Module,
		YYTK::EVENT_OBJECT_CALL,
		GMHooks::EnemyData,
		0);

	yytk_interface->CreateCallback(
		Module,
		YYTK::EVENT_FRAME,
		ObjectBehaviorRun,
		0);

	yytk_interface->CreateCallback(
		Module,
		YYTK::EVENT_OBJECT_CALL,
		GMHooks::FloorData,
		0);

	CScript* script_data = nullptr;
	PVOID original_function = nullptr;

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_music_jukebox_get_songs",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"Jukebox Injection",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::JukeboxInjection,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_music_do",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"MusicDo",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::MusicDo,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_music_do_loop",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"MusicDoLoop",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::MusicDoLoop,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_anon_gml_Object_obj_boss_setter_Create_0_29_gml_Object_obj_boss_setter_Create_0",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"MusicDoLoop",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::ChooseBossIntro,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_music_do_loop_from_start",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"MusicDoLoopFromStart",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::MusicDoLoopFromStart,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_enemy_damage",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"EnemyDamage",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::EnemyDamage,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_player_takeHit",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"PlayerTakeHit",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::PlayerTakeHit,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_button_exit_to_menu",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"ReloadAllMods",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::ReloadAllMods,
		&original_function
	);
	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_instance_create",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"SpawnRoomObject",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::SpawnRoomObject,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_write_savedata",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"WriteSaveData",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::WriteSaveData,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_write_midsave",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"WriteMidSave",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::WriteMidSave,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_GlobalScript_button_exit_out",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"ExitGame",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::ExitGame,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Script_button_start",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"EnterRun",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::EnterRun,
		&original_function
	);

	g_YYTKInterface->GetNamedRoutinePointer(
		"gml_Object_obj_beacon_Other_25",
		reinterpret_cast<PVOID*>(&script_data)
	);
	MmCreateHook(
		g_ArSelfModule,
		"ChooseBossIntro",
		script_data->m_Functions->m_ScriptFunction,
		GMHooks::ChooseBossIntro,
		&original_function
	);

	MmCreateHook(
		Module,
		"QL_WriteFile",
		WriteFile,
		WriteFileHook,
		reinterpret_cast<PVOID*>(&g_WriteFileTrampoline)
	);
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	g_LocalModule = Module;

	UNREFERENCED_PARAMETER(ModulePath);

	AurieStatus last_status = AURIE_SUCCESS;

	last_status = ObGetInterface(
		"YYTK_Main",
		reinterpret_cast<AurieInterfaceBase*&>(yytk_interface)
	);

	DatabaseLoader::g_YYTKInterface = yytk_interface;

#ifdef _DEBUG

	g_YYTKInterface->PrintWarning("Myriad.dll was compiled in Debug mode which has a different ABI. Refusing to execute to prevent crashing");

	return AURIE_MODULE_INITIALIZATION_FAILED;

#endif


	yytk_interface->CreateCallback(
		Module,
		YYTK::EVENT_OBJECT_CALL,
		DrawLoadingScreen,
		0);

	if (!AurieSuccess(last_status))
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

	g_YYTKInterface->CallBuiltin("instance_deactivate_object", { g_YYTKInterface->CallBuiltin("asset_get_index", {"obj_intro"}) });


	LoadMods();

	do 
	{
		//Nothin
	} while (loadingMods = false);

	RegisterHooks(Module);
	
	
	//TODO: check that the array functions do not already work
	//check that the value being overriden is 0
	//check that array functions work after the override

	//Allows YYTK to correctly read GameMaker tables.
	g_YYTKInterface->PrintWarning("I'm about to do some sketchy stuff !");
	*(int64_t*)((char*)g_YYTKInterface + 0x3C0) = 0x90;

	g_YYTKInterface->CallBuiltin("instance_activate_all", {});
	g_YYTKInterface->PrintWarning("Mods Loaded");
	

	return AURIE_SUCCESS;
}

EXPORTED AurieStatus ModuleUnload(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	UNREFERENCED_PARAMETER(Module);
	UNREFERENCED_PARAMETER(ModulePath);

	UnloadMods();

	return AURIE_SUCCESS;
}

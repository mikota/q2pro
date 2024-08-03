#include "../g_local.h"
#include "../acesrc/acebot.h"
#include "botlib.h"
#include "/opt/homebrew/include/jansson.h"

// Count of bot personalities loaded
char** botNames = NULL; // Global array of bot names
size_t loadedBotCount = 0;

bot_mapping_t bot_mappings[MAX_BOTS];

// #define WEAPON_COUNT 9
// #define ITEM_COUNT 6

// typedef struct bot_personality_s
// {
// 	// These are +1 because we're ignoring the first index [0]
// 	// So that MK23_NUM (1) stays at 1 here as well
// 	float weapon_prefs[WEAPON_COUNT + 1];  	//-1 = Will never choose, 1 = Will always choose
// 	float item_prefs[ITEM_COUNT +1];      	//-1 = Will never choose, 1 = Will always choose

// 	float map_prefs;						//-1 = Hate, 0 = Neutral, 1 = Love
// 	float combat_demeanor;					//-1 = Timid | 1 = Aggressive
// 	float chat_demeanor;					//-1 = Quiet | 1 = Chatty
// 	int leave_percent; 						// Percentage calculated that the bot will leave the map.  Recalculated/increases every time the bot dies.
//     float shooting_skill;                   // -1 = Poor Skill | 1 = Excellent Skill (higher value = harder bot)
//     float movement_skill;                   // -1 = Poor Skill | 1 = Excellent Skill (higher value = harder bot
    
//     // Will use the edict's values here, these are only here for testing
//     char* skin;
//     char* name;
// } bot_personality_t;

// #define MK23_NUM				1
// #define MP5_NUM					2
// #define M4_NUM					3
// #define M3_NUM					4
// #define HC_NUM					5
// #define SNIPER_NUM				6
// #define DUAL_NUM				7
// #define KNIFE_NUM				8
// #define GRENADE_NUM				9

// #define SIL_NUM					10
// #define SLIP_NUM				11
// #define BAND_NUM				12
// #define KEV_NUM					13
// #define LASER_NUM				14
// #define HELM_NUM				15

// #define MK23_NAME    "MK23 Pistol"
// #define MP5_NAME     "MP5/10 Submachinegun"
// #define M4_NAME      "M4 Assault Rifle"
// #define M3_NAME      "M3 Super 90 Assault Shotgun"
// #define HC_NAME      "Handcannon"
// #define SNIPER_NAME  "Sniper Rifle"
// #define DUAL_NAME    "Dual MK23 Pistols"
// #define KNIFE_NAME   "Combat Knife"
// #define GRENADE_NAME "M26 Fragmentation Grenade"

// #define SIL_NAME     "Silencer"
// #define SLIP_NAME    "Stealth Slippers"
// #define BAND_NAME    "Bandolier"
// #define KEV_NAME     "Kevlar Vest"
// #define HELM_NAME    "Kevlar Helmet"
// #define LASER_NAME   "Lasersight"

/////*
// This file focuses on loading bot personality traits
/////*

// Data Validation
float validate_pref_numeric(json_t* value, const char* prefName) {
    float numValue = 0.0f;

    if (value) {
        if (json_is_real(value)) {
            numValue = (float)json_real_value(value);
        } else if (json_is_integer(value)) {
            numValue = (float)json_integer_value(value);
        }

        // Check if the value is within the bounds [-1, 1]
        if (numValue < -1.0f || numValue > 1.0f) {
    
            gi.dprintf("Warning: Value %f for '%s' is out of bounds. It must be between -1 and 1. Setting to 0.\n", numValue, prefName);
            return 0.0f; // Value is out of bounds
        }

        return numValue; // Value is valid and within bounds
    }

    gi.dprintf("Warning: Invalid or missing value for '%s'.\n", prefName);
    return 0.0f; // Return 0 if validation fails
}
char* validate_pref_string(json_t* value, int stringtype) {
    const char* str = json_string_value(value);
    size_t len = strlen(str);
    int maxlen = 0;
    char* valuename;
    if (value && json_is_string(value)) {
        if (stringtype == 0) {
            maxlen = 16;
            valuename = "name";
        } else {
            maxlen = 64;
            valuename = "skin";
        }
        if (len > 0 && len <= maxlen) {
            return strdup(str); // Return a copy of the string if valid
        }
    }
    gi.dprintf("Warning: Invalid %s (%s). Using default value.\n", valuename, str);
    gi.dprintf("Future improvement: run it through the randomizer instead\n");
    return NULL; // Return NULL if validation fails
}

// qboolean json_load_check(const char* filename)
// {
//     json_error_t error;
//     json_t* root = json_loadf(filename, 0, &error);
//     fclose(filename);

//     if (!root) {
//         gi.dprintf("bot_personality: Error parsing JSON: %s\n", error.text);
//         return NULL;
//     }

//     json_t* bots = json_object_get(root, "bots");
//     if (!bots) {
//         gi.dprintf("bot_personality: 'bots' object not found\n");
//         json_decref(root);
//         return NULL;
//     }
// }


// Evaluate leave percentage
/*
 We will call this on every bot death and kill if
 map_prefs is < 0
*/

qboolean BotRageQuit(edict_t* self, qboolean frag_or_death)
{
    // Don't do anything if the map_prefs are 0 to positive
    if (self->bot.personality.map_prefs >= 0)
        return false;

    // Deaths are worth more than frags when you don't like the map you're on
    float fragplus = 0.05;
    float deathplus = 0.07;

    // Death is true, Frag is false
    if (frag_or_death) {
        gi.dprintf("%s received a death, they're mad!\n", self->client->pers.netname);
        self->bot.personality.map_prefs -= deathplus;
    } else {
        gi.dprintf("%s received a frag, they're happy!\n", self->client->pers.netname);
        self->bot.personality.map_prefs += fragplus;
    }

    float rage_quit_threshold = -1.0; // Example threshold value
    if (self->bot.personality.map_prefs <= rage_quit_threshold) {
        gi.dprintf("%s is rage quitting due to low map preference!\n", self->client->pers.netname);
        return true;
    }

    //Default return false, bot sticks around
    return false;
}


// Dynamically update the map preferences of the bot
// Not all bots will have a preference, so we default to 0 for those
void update_map_pref(json_t* root, char* map_name, temp_bot_personality_t* personality)
{
    // Get the "map_prefs" object from the root
    json_t* map_prefs = json_object_get(root, "map_prefs");
    if (!map_prefs) {
        gi.dprintf("Map preferences not found.\n");
        return;
    }

    // Fetch the value for the specified map name
    json_t* value = json_object_get(map_prefs, map_name);
    if (value && json_is_real(value)) {
        // Update the personality struct with the fetched value
        personality->map_prefs = (float)json_real_value(value);
        gi.dprintf("Updated map preference for '%s' to %f.\n", map_name, personality->map_prefs);
    } else {
        // If the map name is not found or the value is not a real number, default to 0.0f
        personality->map_prefs = 0.0f;
        gi.dprintf("Map '%s' not found or invalid. Defaulting to %f.\n", map_name, personality->map_prefs);
    }
}

// Function to load a bot personality from a JSON file using libjansson
bot_mapping_t* BOTLIB_LoadPersonalities(const char* filename) {
    
    // Open the file
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open file");
        return NULL;
    }

    json_error_t error;
    json_t* root = json_loadf(file, 0, &error);
    fclose(file);

    if (!root) {
        gi.dprintf("bot_personality: Error parsing JSON: %s\n", error.text);
        return NULL;
    }

    json_t* bots = json_object_get(root, "bots");
    if (!bots) {
        gi.dprintf("bot_personality: 'bots' object not found\n");
        json_decref(root);
        return NULL;
    }

    // json_t* bot_data = json_object_get(bots, bot_name);
    // if (!bot_data) {
    //     gi.dprintf("bot_personality: Bot '%s' not found\n", bot_name);
    //     json_decref(root);
    //     return NULL;
    // }

    bot_mapping_t* temp_bot = (bot_mapping_t*)malloc(sizeof(bot_mapping_t));
    if (!temp_bot) {
        json_decref(root);
        return NULL;
    }

    // Extract and assign values from the JSON object
    // Define an array of weapon names corresponding to their preferences
    const char* weaponNames[] = {
        MK23_NAME, MP5_NAME, M4_NAME, M3_NAME, HC_NAME, 
        SNIPER_NAME, DUAL_NAME, KNIFE_NAME, GRENADE_NAME
    };

    json_t* weapon_prefs = json_object_get(root, "weapon_prefs");
    for (size_t i = 0; i < json_array_size(weapon_prefs); i++) {
        json_t* wpref_value = json_array_get(weapon_prefs, i);
        // Ensure the index is within the bounds of weaponNames array
        const char* weaponName = (i < sizeof(weaponNames)/sizeof(weaponNames[0])) ? weaponNames[i] : "Unknown Weapon";
        float value = validate_pref_numeric(wpref_value, weaponName); // Updated function call with weapon name

        // Proceed with assignment only if value is not 0.0 (assuming 0.0 indicates invalid or not set)
        if (value != 0.0f) {
            temp_bot->personality.weapon_prefs[i] = value; // Direct assignment using index, assuming array is properly sized and ordered
        }
    }

    // Assuming you have an array of preference names corresponding to each index
    // Define an array of item names corresponding to their preferences
    const char* itemNames[] = {SIL_NAME, SLIP_NAME, BAND_NAME, KEV_NAME, HELM_NAME, LASER_NAME};

    json_t* item_prefs = json_object_get(root, "item_prefs");
    for (size_t i = 0; i < json_array_size(item_prefs); i++) {
        json_t* ipref_value = json_array_get(item_prefs, i);
        // Ensure the index is within the bounds of itemNames array
        const char* itemName = (i < sizeof(itemNames)/sizeof(itemNames[0])) ? itemNames[i] : "Unknown Item";
        float value = validate_pref_numeric(ipref_value, itemName); // Pass the item name for detailed warnings

        // Assuming 0.0f indicates an invalid or out-of-bounds value, skip assignment in such cases
        if (value != 0.0f) {
            temp_bot->personality.item_prefs[i] = value;
        }
    }

    // Extract single float values, if they exist, else default to 0
    json_t *checkval;

    char* current_map_name = level.mapname;
    //personality->map_prefs = validate_pref_numeric(json_object_get(root, "map_prefs"), "map_prefs");
    update_map_pref(root, current_map_name, temp_bot);

    temp_bot->personality.combat_demeanor = validate_pref_numeric(json_object_get(root, "combat_demeanor"), "combat_demeanor");

    temp_bot->personality.chat_demeanor = validate_pref_numeric(json_object_get(root, "chat_demeanor"), "chat_demeanor");
    
    // Extract integer value, every fresh personality load sets this to 0
    temp_bot->personality.leave_percent = 0;

    // Set the skin, if provided
    char* defaultSkin = "male/grunt";

    checkval = json_object_get(root, "skin");
    char* validatedSkin = validate_pref_string(checkval, 1);
    if (validatedSkin != NULL) {
        // If skin value is valid, use it
        temp_bot->personality.skin_pref = validatedSkin;
    } else {
        // If skin value is invalid, use defaultSkin and print a warning
        temp_bot->personality.skin_pref = strdup(defaultSkin); // Use strdup to copy defaultSkin
    }

    // Set the name, if provided
    char* defaultName = "AqtionBot";

    checkval = json_object_get(root, "name");
    char* validatedName = validate_pref_string(checkval, 0);
    if (validatedName != NULL) {
        // If name value is valid, use it
        temp_bot->name = validatedName;
    } else {
        // If name value is invalid, use defaultName and print a warning
        temp_bot->name = strdup(defaultName); // Use strdup to copy defaultName
    }

    // Clean up
    json_decref(root);

    return temp_bot;
}

// int main() {
//     // Example usage of load_bot_personality
//     bot_personality_t* personality = load_bot_personality("bot.json");
//     if (personality != NULL) {        
//         printf("\n");
//         printf("Weapon Preferences:");
//         for (int i = 1; i < WEAPON_COUNT; i++) { // Assuming WEAPON_PREFS_SIZE is defined
//             printf(" %f", personality->weapon_prefs[i]);
//         }
//         printf("\nItem Preferences:");
//         for (int i = 1; i < ITEM_COUNT; i++) { // Assuming ITEM_PREFS_SIZE is defined
//             printf(" %f", personality->item_prefs[i]);
//         }
//         printf("\n");
//         // Print Map Preferences
//         printf("Map Preferences: %f\n", personality->map_prefs);
//         // Print Chat Demeanor
//         printf("Chat Demeanor: %f\n", personality->chat_demeanor);
        
//         // Print Combat Demeanor
//         printf("Combat Demeanor: %f\n", personality->combat_demeanor);
        
//         // Print Skin
//         printf("Skin: %s\n", personality->skin);
        
//         printf("\n");
//         //
//         if (!warningCount)
//             printf("Bot personality loaded successfully.\n");
//         else
//             printf("Loaded with %i warnings\n", warningCount);

//         // Remember to free the allocated memory for personality
//         free(personality);
//     } else {
//         printf("Failed to load bot personality.\n");
//     }
//     return 0;
// }

// Function to load bot names from file
// int loadBotNames(const char* filename) {
//     json_error_t error;
//     json_t* root = json_load_file(filename, 0, &error);

//     if (!root) {
//         return 0; // Failed to load file
//     }

//     json_t* bots = json_object_get(root, "bots");
//     if (!bots) {
//         json_decref(root);
//         return 0; // No "bots" object
//     }

//     size_t fileBotCount = json_array_size(bots);
//     // Adjust the number of bots to load based on game.maxclients
//     loadedBotCount = (fileBotCount < game.maxclients) ? fileBotCount : game.maxclients;
//     botNames = malloc(loadedBotCount * sizeof(char*)); // Allocate array for bot names

//     for (size_t i = 0; i < loadedBotCount; i++) {
//         json_t* bot = json_array_get(bots, i);
//         const char* name = json_string_value(json_object_get(bot, "name"));
//         botNames[i] = strdup(name); // Copy name into array
//     }

//     json_decref(root);
//     return 1; // Success
// }
// // Function to get a random bot name
// char* getRandomBotName() {
//     if (loadedBotCount == 0) return NULL; // No bots loaded
//     srand(time(NULL)); // Seed the random number generator
//     size_t index = rand() % loadedBotCount;
//     return botNames[index]; // Return random bot name
// }

// void freeBotNames() {
//     for (size_t i = 0; i < loadedBotCount; i++) {
//         free(botNames[i]); // Free each name
//     }
//     free(botNames); // Free the array
//     botNames = NULL;
//     loadedBotCount = 0;
// }

// Function to count bots in the JSON file
size_t BOTLIB_PersonalityCount(void) {
    json_t* root;
    json_error_t error;
    json_t* bots;
    size_t bot_count = 0;
    FILE* fIn;
	char filename[128];
    cvar_t* game_dir = gi.cvar("game", "action", 0);
	cvar_t* botdir = gi.cvar("botdir", "bots", 0);

#ifdef _WIN32
	f = sprintf(filename, ".\\");
	f += sprintf(filename + f, game_dir->string);
	f += sprintf(filename + f, "\\");
	f += sprintf(filename + f, botdir->string);
	f += sprintf(filename + f, "\\bots.json");
#else
	strcpy(filename, "./");
	strcat(filename, game_dir->string);
	strcat(filename, "/");
	strcat(filename, botdir->string);
	strcat(filename, "/bots.json");
#endif

    // Save file path for later references
    strncpy(game.bot_file_path, filename, MAX_QPATH);

	if ((fIn = fopen(filename, "rb")) == NULL) // See if .json file exists
	{
		return 0; // No file
	}

    // Rewind the file pointer to the beginning of the file
    rewind(fIn);

    // Load JSON from file
    root = json_loadf(fIn, 0, &error);

    if (!root) {
        // Handle JSON parsing error
        fprintf(stderr, "JSON error on line %d: %s\n", error.line, error.text);
        json_decref(root);
        return 0; // Return 0 to indicate failure
    }

    // Get the "bots" object from the root of the JSON structure
    bots = json_object_get(root, "bots");
    if (!json_is_object(bots)) {
        // Handle error: "bots" is not an object or not found
        json_decref(root);
        return 0; // Return 0 to indicate failure
    }

    // Count the number of keys (bots) in the "bots" object
    bot_count = json_object_size(bots);

    // Clean up
    json_decref(root);
    fclose(fIn);

    return bot_count; // Return the count of bots
}

// qboolean BOTLIB_LoadPersonality(char* botname)
// {
//     int maxclients = game.maxclients;
//     FILE* fIn;
// 	char tempfilename[128];
// 	int fileSize = 0;
// 	int f, n, l, p; // File, nodes, links, paths
//     qboolean randomized;

//     // Const-ify the filename
//     const char* filename = strdup(game.bot_file_path);

// 	if ((fIn = fopen(filename, "rb")) == NULL) // See if .json file exists
// 	{
// 		return false; // No file
// 	}

// 	fclose(fIn);

// 	Com_Printf("%s Loaded %s from disk\n", __func__, filename);

//     // If no botname provided, let's re-run with a random bot
//     if(botname == NULL)
//         BOTLIB_LoadPersonality(getRandomBotName());

//     if(load_bot_personality()){

//     }
// }
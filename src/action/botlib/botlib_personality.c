#include "../g_local.h"
#include "../acesrc/acebot.h"
#include "botlib.h"
#include "/opt/homebrew/include/jansson.h"

// Count of bot personalities loaded
char** botNames = NULL; // Global array of bot names
qboolean pers_debug_mode = true;
int loaded_bot_personalities = 0;
int bot_personality_index = 0;

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
    if (!value) { // Check if value is NULL
        return NULL; // Early return if value is NULL
    }
    const char* str = json_string_value(value);
    if (!str) { // Check if str is NULL
        return NULL; // Early return if str is NULL
    }
    size_t len = strlen(str);
    int maxlen = 0;
    char* valuename = NULL;
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
    gi.dprintf("Value for skin was: %s %s\n", valuename, str);
    gi.dprintf("Warning: Invalid %s (%s). Using default value.\n", valuename, str);
    gi.dprintf("Future improvement: run it through the randomizer instead\n");
    return NULL; // Return NULL if validation fails
}

void DeactivateBotPersonality(void)
{
    gi.dprintf("INFO: Deactivating bot_personality.\n");
    gi.cvar_forceset("bot_personality", 0);
}

// Function to create a new bot_mapping_t instance
temp_bot_mapping_t* create_new_bot(char* name) {
    temp_bot_mapping_t* newBot = (temp_bot_mapping_t*)malloc(sizeof(temp_bot_mapping_t));
    if (newBot == NULL) {
        // Handle memory allocation failure
        return NULL;
    }
    newBot->name = strdup(name); // Duplicate the name
    if (newBot->name == NULL) {
        gi.dprintf("%s: error, missing bot name in file\n", __func__);
        free(newBot);
        return NULL;
    }
    newBot->personality.skin_pref = NULL; // Initialize to NULL
    newBot->personality.pId = bot_personality_index;  // Initialize ID for indexing
    bot_personality_index++;
    return newBot;
}

// Death = True, Frag = False
qboolean BotRageQuit(edict_t* self, qboolean frag_or_death)
{
    // Don't do anything if the map_prefs are 0 or positive
    if (self->bot.personality.map_prefs >= 0)
        return false;

    // Deaths are worth more than frags when you don't like the map you're on
    float fragplus = 0.05;
    float deathminus = 0.07;

    // Death is true, Frag is false
    if (frag_or_death) {
        gi.dprintf("%s received a death, they're mad!\n", self->client->pers.netname);
        self->bot.personality.map_prefs -= deathminus;
    } else {
        gi.dprintf("%s received a frag, they're happy!\n", self->client->pers.netname);
        self->bot.personality.map_prefs += fragplus;
    }

    float rage_quit_threshold = -1.0; // Max threshold value
    if (self->bot.personality.map_prefs <= rage_quit_threshold) {
        gi.dprintf("%s is rage quitting due to low map preference!\n", self->client->pers.netname);
        BOTLIB_Chat(self, CHAT_RAGE);
        return true;
    }

    //Default return false, bot sticks around
    return false;
}


// Dynamically update the map preferences of the bot
// Not all bots will have a preference, so we default to 0 for those
void update_map_pref(json_t* root, char* map_name, temp_bot_mapping_t* newBot)
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
        // Update the newBot struct with the fetched value
        newBot->personality.map_prefs = (float)json_real_value(value);
        if(pers_debug_mode)
            gi.dprintf("Updated map preference for '%s' to %f.\n", map_name, newBot->personality.map_prefs);
    } else {
        // If the map name is not found or the value is not a real number, default to 0.0f
        newBot->personality.map_prefs = 0.0f;
        if(pers_debug_mode)
            gi.dprintf("Map '%s' not found or invalid. Defaulting to %f.\n", map_name, newBot->personality.map_prefs);
    }
}

// Function to load a bot personality from a JSON file using libjansson
temp_bot_mapping_t* BOTLIB_LoadPersonalities(const char* filename)
{
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open file");
        DeactivateBotPersonality();
        return NULL;
    }

    json_error_t error;
    json_t* root = json_loadf(file, 0, &error);
    fclose(file);

    if (!root) {
        gi.dprintf("bot_personality: Error parsing JSON: %s\n", error.text);
        DeactivateBotPersonality();
        return NULL;
    }

    json_t* bots = json_object_get(root, "bots");
    if (!bots || !json_is_object(bots)) {
        gi.dprintf("bot_personality: 'bots' object not found or is not an object\n");
        DeactivateBotPersonality();
        json_decref(root);
        return NULL;
    }

    loaded_bot_personalities = json_object_size(bots);
    if (loaded_bot_personalities == 0) {
        gi.dprintf("bot_personality: No bots found in JSON\n");
        DeactivateBotPersonality();
        json_decref(root);
        return NULL;
    }

    const char* botName;
    json_t* botDetails;
    int botIndex = 0;

    json_object_foreach(bots, botName, botDetails) {
        temp_bot_mapping_t* newBot = create_new_bot((char*)botName);
        if (!newBot) {
            json_decref(root);
            return NULL;
        }

        json_t* weapon_prefs = json_object_get(botDetails, "weapon_prefs");
        for (size_t i = 0; i < WEAPON_COUNT; i++) {
            json_t* wpref_value = json_array_get(weapon_prefs, i);
            newBot->personality.weapon_prefs[i] = validate_pref_numeric(wpref_value, "weapon_prefs");
        }

        json_t* item_prefs = json_object_get(botDetails, "item_prefs");
        for (size_t i = 0; i < ITEM_COUNT; i++) {
            json_t* ipref_value = json_array_get(item_prefs, i);
            newBot->personality.item_prefs[i] = validate_pref_numeric(ipref_value, "item_prefs");
        }

        json_t* map_prefs = json_object_get(botDetails, "map_prefs");
        newBot->personality.map_prefs = validate_pref_numeric(map_prefs, "map_prefs");
        update_map_pref(botDetails, level.mapname, newBot);

        json_t* combat_demeanor = json_object_get(botDetails, "combat_demeanor");
        newBot->personality.combat_demeanor = validate_pref_numeric(combat_demeanor, "combat_demeanor");

        json_t* chat_demeanor = json_object_get(botDetails, "chat_demeanor");
        newBot->personality.chat_demeanor = validate_pref_numeric(chat_demeanor, "chat_demeanor");

        json_t* skin_pref = json_object_get(botDetails, "skin");
        if (skin_pref) {
            char* validatedSkinPref = validate_pref_string(skin_pref, MAX_QPATH);
            if (validatedSkinPref != NULL) {
                newBot->personality.skin_pref = validatedSkinPref;
            } else {
                if (pers_debug_mode)
                    gi.dprintf("%s: warning: skin object missing from %s\n", __func__, botName);
                newBot->personality.skin_pref = "male/grunt";
            }
        } else {
            if (pers_debug_mode)
                gi.dprintf("%s: warning: skin object missing from %s\n", __func__, botName);
            newBot->personality.skin_pref = "male/grunt";
        }

        bot_mappings[botIndex++] = *newBot;

        if (pers_debug_mode) {
            gi.dprintf("Loaded bot %s\n", newBot->name);
            gi.dprintf("Weapon Preferences:\n");
            for (size_t i = 0; i < WEAPON_COUNT; i++) {
                gi.dprintf("  %.2f\n", newBot->personality.weapon_prefs[i]);
            }
            gi.dprintf("Item Preferences:\n");
            for (size_t i = 0; i < ITEM_COUNT; i++) {
                gi.dprintf("  %.2f\n", newBot->personality.item_prefs[i]);
            }
            gi.dprintf("Map Preferences: %.2f\n", newBot->personality.map_prefs);
            gi.dprintf("Combat Demeanor: %.2f\n", newBot->personality.combat_demeanor);
            gi.dprintf("Chat Demeanor: %.2f\n", newBot->personality.chat_demeanor);
            gi.dprintf("Skin Preference: %s\n", newBot->personality.skin_pref);
        }

        free(newBot); // Free the temporary newBot since it's now copied to bot_mappings
    }

    json_decref(root);
    return bot_mappings;
}

    
    
    
    
    
    
    
    
    
    
    
    
    // Extract and assign values from the JSON object
    // Define an array of weapon names corresponding to their preferences
    // const char* weaponNames[] = {
    //     MK23_NAME, MP5_NAME, M4_NAME, M3_NAME, HC_NAME, 
    //     SNIPER_NAME, DUAL_NAME, KNIFE_NAME, GRENADE_NAME
    // };

    // json_t* weapon_prefs = json_object_get(root, "weapon_prefs");
    // for (size_t i = 0; i < json_array_size(weapon_prefs); i++) {
    //     json_t* wpref_value = json_array_get(weapon_prefs, i);
    //     // Ensure the index is within the bounds of weaponNames array
    //     const char* weaponName = (i < sizeof(weaponNames)/sizeof(weaponNames[0])) ? weaponNames[i] : "Unknown Weapon";
    //     float value = validate_pref_numeric(wpref_value, weaponName); // Updated function call with weapon name

    //     // Proceed with assignment only if value is not 0.0 (assuming 0.0 indicates invalid or not set)
    //     if (value != 0.0f) {
    //         temp_bot->personality.weapon_prefs[i] = value; // Direct assignment using index, assuming array is properly sized and ordered
    //     }
    // }

    // // Assuming you have an array of preference names corresponding to each index
    // // Define an array of item names corresponding to their preferences
    // const char* itemNames[] = {SIL_NAME, SLIP_NAME, BAND_NAME, KEV_NAME, HELM_NAME, LASER_NAME};

    // json_t* item_prefs = json_object_get(root, "item_prefs");
    // for (size_t i = 0; i < json_array_size(item_prefs); i++) {
    //     json_t* ipref_value = json_array_get(item_prefs, i);
    //     // Ensure the index is within the bounds of itemNames array
    //     const char* itemName = (i < sizeof(itemNames)/sizeof(itemNames[0])) ? itemNames[i] : "Unknown Item";
    //     float value = validate_pref_numeric(ipref_value, itemName); // Pass the item name for detailed warnings

    //     // Assuming 0.0f indicates an invalid or out-of-bounds value, skip assignment in such cases
    //     if (value != 0.0f) {
    //         temp_bot->personality.item_prefs[i] = value;
    //     }
    // }

    // // Extract single float values, if they exist, else default to 0
    // json_t *checkval;

    // char* current_map_name = level.mapname;
    // //personality->map_prefs = validate_pref_numeric(json_object_get(root, "map_prefs"), "map_prefs");
    // update_map_pref(root, current_map_name, temp_bot);

    // temp_bot->personality.combat_demeanor = validate_pref_numeric(json_object_get(root, "combat_demeanor"), "combat_demeanor");

    // temp_bot->personality.chat_demeanor = validate_pref_numeric(json_object_get(root, "chat_demeanor"), "chat_demeanor");
    
    // // Extract integer value, every fresh personality load sets this to 0
    // temp_bot->personality.leave_percent = 0;

    // // Set the skin, if provided
    // char* defaultSkin = "male/grunt";

    // checkval = json_object_get(root, "skin");
    // char* validatedSkin = validate_pref_string(checkval, 1);
    // if (validatedSkin != NULL) {
    //     // If skin value is valid, use it
    //     temp_bot->personality.skin_pref = validatedSkin;
    // } else {
    //     // If skin value is invalid, use defaultSkin and print a warning
    //     temp_bot->personality.skin_pref = strdup(defaultSkin); // Use strdup to copy defaultSkin
    // }

    // // Set the name, if provided
    // char* defaultName = "AqtionBot";

    // checkval = json_object_get(root, "bots");
    // char* validatedName = validate_pref_string(checkval, 0);
    // if (validatedName != NULL) {
    //     // If name value is valid, use it
    //     temp_bot->name = validatedName;
    // } else {
    //     // If name value is invalid, use defaultName and print a warning
    //     temp_bot->name = strdup(defaultName); // Use strdup to copy defaultName
    // }

    
    // Clean up
//     json_decref(root);

//     return 0;
// }

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

// Main function that will load other personality methods
void BOTLIB_PersonalityFile(void) {
    FILE* fIn;
	char filename[MAX_QPATH];
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

    // Validate file exists
	if ((fIn = fopen(filename, "rb")) == NULL) {// See if .json file exists
        DeactivateBotPersonality();
		return; // No file
    }
    // Now that we have the file, let's load the bots
    BOTLIB_LoadPersonalities(filename);
}

// void copy_temp_to_real(edict_t* bot, temp_bot_mapping_t *roster, bot_personality_t *personality) {
//     // Copying array fields
//     memcpy(personality->weapon_prefs, roster->personality.weapon_prefs, sizeof(personality->weapon_prefs));
//     memcpy(personality->item_prefs, roster->personality.item_prefs, sizeof(personality->item_prefs));

//     // Copying simple fields
//     personality->map_prefs = roster->personality.map_prefs;
//     personality->combat_demeanor = roster->personality.combat_demeanor;
//     personality->chat_demeanor = roster->personality.chat_demeanor;
//     personality->leave_percent = roster->personality.leave_percent;

//     // Copying string fields, assuming they should be char* in bot_personality_t
//     if (roster->name != NULL) {
//         personality->name_pref = malloc(strlen(roster->name) + 1);
//         Info_SetValueForKey(userinfo, "name", roster->name);
//     } else {
//         // Safe default
//         personality->name_pref = "AqtionBot";
//     }

//     if (roster->personality.skin_pref != NULL) {
//         personality->skin_pref = malloc(strlen(roster->personality.skin_pref) + 1);
//         strcpy(personality->skin_pref, roster->personality.skin_pref);
//     } else {
//         // Safe default
//         personality->skin_pref = "male/grunt";
//     }


// }

// Return skin if false, return model if true
char* _splitSkinChar(char *skinpathInput, qboolean returnSkin) {
    char *skinpath;
    char *saveptr = NULL;

    // Ensure skinpath is modifiable by duplicating the input
    if(skinpathInput == NULL || skinpathInput[0] == '\0') {
        skinpath = strdup("male/grunt");
    } else {
        skinpath = strdup(skinpathInput);
    }

    if(skinpath == NULL) {
        // Handle memory allocation failure
        return NULL;
    }

    // Use strtok_r for a safer tokenization
    char *token = strtok_r(skinpath, "/", &saveptr);
    if (token == NULL) {
        gi.dprintf("%s skin path provided is invalid\n", skinpath);
        free(skinpath); // Clean up
        return NULL;
    }

    if (!returnSkin) {
        // Return model
        return token;
    } else {
        // Get the second token (skin)
        token = strtok(NULL, "/");
        if (token == NULL) {
            gi.dprintf("No skin part in the path: %s\n", skinpath);
            return NULL;
        }
        // Return skin
        return token;
    }
}

// At this point we have the bot ready, we just need to copy over the right loaded personality
qboolean BOTLIB_LoadBotPersonality(edict_t* self)
{
    temp_bot_mapping_t* selectedBot = &bot_mappings[self->bot.personality.pId];

    // Copying array fields
    memcpy(self->bot.personality.weapon_prefs, selectedBot->personality.weapon_prefs, sizeof(self->bot.personality.weapon_prefs));
    memcpy(self->bot.personality.item_prefs, selectedBot->personality.item_prefs, sizeof(self->bot.personality.item_prefs));

    // Copying simple fields
    self->bot.personality.map_prefs = selectedBot->personality.map_prefs;
    self->bot.personality.combat_demeanor = selectedBot->personality.combat_demeanor;
    self->bot.personality.chat_demeanor = selectedBot->personality.chat_demeanor;
    self->bot.personality.leave_percent = selectedBot->personality.leave_percent;

    if(pers_debug_mode)
      gi.dprintf("Selected Bot %s(indexes: %i/%i) - Weapon Pref[0]: %f, Item Pref[0]: %f, Map Pref: %f, Combat Demeanor: %f, Chat Demeanor: %f, Leave Percent: %d\n", selectedBot->name, self->bot.personality.pId, selectedBot->personality.pId, selectedBot->personality.weapon_prefs[0], selectedBot->personality.item_prefs[0], selectedBot->personality.map_prefs, selectedBot->personality.combat_demeanor, selectedBot->personality.chat_demeanor, selectedBot->personality.leave_percent);
}

qboolean BOTLIB_SetPersonality(edict_t* self, int team, int force_gender)
{
    if (bot_personality_index <= 0) {
        // Handle error: No bot personalities loaded
        return false;
    }

    srand(time(NULL));
    int randomIndex = rand() % bot_personality_index;
    int attempts = 0;
    temp_bot_mapping_t* selectedBot = &bot_mappings[randomIndex];
    qboolean foundInactiveBot = false;

    while (attempts < bot_personality_index) {
        randomIndex = rand() % bot_personality_index;
        if (!bot_mappings[randomIndex].personality.isActive) {
            foundInactiveBot = true;
            break;
        }
        attempts++;
    }

    if (!foundInactiveBot) {
        // Unable to find an inactive bot
        return false;
    }

    // Mark the selected bot as active in both loaded and live structs
    bot_mappings[randomIndex].personality.isActive = true;
    self->bot.personality.isActive = true;
    self->bot.personality.pId = randomIndex;

    gi.dprintf("Random index %i\n", randomIndex);
    
    int gender = INVALID;
	char name[MAX_QPATH]; // Full bot name ( [prefix/clan/rng]  and/or  [name]  and/or  [postfix] )
	char skin[MAX_INFO_STRING];
	char userinfo[MAX_INFO_STRING];
	memset(userinfo, 0, sizeof(userinfo)); // Init userinfo

	gi.cvar_forceset(stat_logs->name, "0"); // Turning off stat collection since bots are enabled

    // Set bot name
    Info_SetValueForKey(userinfo, "name", selectedBot->name);

    char* femaleSkinDirs[] = { "actionrally", "female", "sydney" };
    char* maleSkinsDirs[] = { "actionmale", "aqmarine", "male", "messiah", "sas", "terror" };
    int femaleSkinDirsSize = sizeof(femaleSkinDirs) / sizeof(femaleSkinDirs[0]);
    int maleSkinsDirsSize = sizeof(maleSkinsDirs) / sizeof(maleSkinsDirs[0]);

	// Teamplay and 3TEAMS
	if (teamplay->value && team) // TEAM1, TEAM2, or TEAM3
	{
		// Figure out the gender based on the team skins
		for (int i = 0; i < sizeof(femaleSkinDirs) / sizeof(femaleSkinDirs[0]); ++i)
		{
			if (Q_strcasestr(teams[team].skin, femaleSkinDirs[i]) != NULL)
			{
				gender = GENDER_FEMALE;
				break;
			}
		}
		if (gender == INVALID)
		{
			for (int i = 0; i < sizeof(maleSkinsDirs) / sizeof(maleSkinsDirs[0]); ++i)
			{
				if (Q_strcasestr(teams[team].skin, maleSkinsDirs[i]) != NULL)
				{
					gender = GENDER_MALE;
					break;
				}
			}
		}
		if (gender == INVALID) // Couldn't find skin gender (perhaps server is using custom skins)
		{
			if (rand() % 2 == 0) // So just randomize the skin gender
				gender = GENDER_MALE;
			else
				gender = GENDER_FEMALE;
		}
	}
	else // Deathmatch
	{
        // Set the skin
        char* selectedSkin = selectedBot->personality.skin_pref;
        // Assuming selectedBot is correctly initialized and contains the desired data
        if (selectedSkin != NULL || selectedSkin == "") {
            // Directly use the skin_pref from selectedBot
            Info_SetValueForKey(userinfo, "skin", selectedSkin);
        } else {
            // Safe default
            Info_SetValueForKey(userinfo, "skin", "male/grunt");
        }

        // Set the gender
        char* modelName = _splitSkinChar(selectedSkin, false);
        for (int i = 0; i < maleSkinsDirsSize; i++) {
            if (strcmp(maleSkinsDirs[i], modelName) == 0) {
                Info_SetValueForKey(userinfo, "gender", "male");
            }
        }
        for (int i = 0; i < femaleSkinDirsSize; i++) {
            if (strcmp(femaleSkinDirs[i], modelName) == 0) {
                Info_SetValueForKey(userinfo, "gender", "female");
            }
        }
	}

	//Set userinfo: hand, spec
	Info_SetValueForKey(userinfo, "hand", "2"); // bot is center handed for now!
	Info_SetValueForKey(userinfo, "spectator", "0"); // NOT a spectator

	ClientConnect(self, userinfo);

    game.used_bot_personalities++;
    return true;
}

void BOTLIB_FreeBotPersonality(edict_t* bot)
{
    // If this is a bot with personality, free it up
    int bot_pId = bot->bot.personality.pId;
    for (int i = 0; i < MAX_BOTS; i++) {
        if (bot_mappings[i].personality.pId == bot_pId) {
            bot_mappings[i].personality.isActive = false;
            break; // Exit the loop once the bot is found and deactivated
        }
    }
    game.used_bot_personalities--;
}
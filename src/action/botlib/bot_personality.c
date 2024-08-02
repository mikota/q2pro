//#include "../g_local.h"
#include "/opt/homebrew/include/jansson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int warningCount = 0;

#define WEAPON_COUNT 9
#define ITEM_COUNT 6

typedef struct bot_personality_s
{
	// These are +1 because we're ignoring the first index [0]
	// So that MK23_NUM (1) stays at 1 here as well
	float weapon_prefs[WEAPON_COUNT + 1];  	//-1 = Will never choose, 1 = Will always choose
	float item_prefs[ITEM_COUNT +1];      	//-1 = Will never choose, 1 = Will always choose

	float map_prefs;						//-1 = Hate, 0 = Neutral, 1 = Love
	float combat_demeanor;					//-1 = Timid | 1 = Aggressive
	float chat_demeanor;					//-1 = Quiet | 1 = Chatty
	int leave_percent; 						// Percentage calculated that the bot will leave the map.  Recalculated/increases every time the bot dies.
    char* skin;
    char* name;
} bot_personality_t;

#define MK23_NUM				1
#define MP5_NUM					2
#define M4_NUM					3
#define M3_NUM					4
#define HC_NUM					5
#define SNIPER_NUM				6
#define DUAL_NUM				7
#define KNIFE_NUM				8
#define GRENADE_NUM				9

#define SIL_NUM					10
#define SLIP_NUM				11
#define BAND_NUM				12
#define KEV_NUM					13
#define LASER_NUM				14
#define HELM_NUM				15

#define MK23_NAME    "MK23 Pistol"
#define MP5_NAME     "MP5/10 Submachinegun"
#define M4_NAME      "M4 Assault Rifle"
#define M3_NAME      "M3 Super 90 Assault Shotgun"
#define HC_NAME      "Handcannon"
#define SNIPER_NAME  "Sniper Rifle"
#define DUAL_NAME    "Dual MK23 Pistols"
#define KNIFE_NAME   "Combat Knife"
#define GRENADE_NAME "M26 Fragmentation Grenade"

#define SIL_NAME     "Silencer"
#define SLIP_NAME    "Stealth Slippers"
#define BAND_NAME    "Bandolier"
#define KEV_NAME     "Kevlar Vest"
#define HELM_NAME    "Kevlar Helmet"
#define LASER_NAME   "Lasersight"

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
            warningCount++;
            printf("Warning: Value %f for '%s' is out of bounds. It must be between -1 and 1.\n", numValue, prefName);
            return 0.0f; // Value is out of bounds
        }

        return numValue; // Value is valid and within bounds
    }

    warningCount++;
    printf("Warning: Invalid or missing value for '%s'.\n", prefName);
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
    warningCount++;
    printf("Warning: Invalid %s (%s). Using default value.\n", valuename, str);
    return NULL; // Return NULL if validation fails
}

// Function to load a bot personality from a JSON file using libjansson
bot_personality_t* load_bot_personality(const char* filename) {
    // Open the file
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open file");
        return NULL;
    }

    // Use jansson's json_loadf to parse the file directly into a json_t object
    json_error_t error;
    json_t* root = json_loadf(file, 0, &error);
    fclose(file); // Close the file as soon as it's no longer needed

    if (!root) {
        fprintf(stderr, "Error parsing JSON: %s\n", error.text);
        return NULL;
    }

    // Allocate memory for the bot_personality_t struct
    bot_personality_t* personality = (bot_personality_t*)malloc(sizeof(bot_personality_t));
    if (!personality) {
        json_decref(root); // Ensure to release the json_t object to prevent memory leaks
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
            personality->weapon_prefs[i] = value; // Direct assignment using index, assuming array is properly sized and ordered
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
            personality->item_prefs[i] = value;
        }
    }

    // Extract single float values, if they exist, else default to 0
    json_t *checkval;

    personality->map_prefs = validate_pref_numeric(json_object_get(root, "map_prefs"), "map_prefs");

    personality->combat_demeanor = validate_pref_numeric(json_object_get(root, "combat_demeanor"), "combat_demeanor");

    personality->chat_demeanor = validate_pref_numeric(json_object_get(root, "chat_demeanor"), "chat_demeanor");
    
    // Extract integer value, every fresh personality load sets this to 0
    personality->leave_percent = 0;

    // Set the skin, if provided
    char* defaultSkin = "male/grunt";

    checkval = json_object_get(root, "skin");
    char* validatedSkin = validate_pref_string(checkval, 1);
    if (validatedSkin != NULL) {
        // If skin value is valid, use it
        personality->skin = validatedSkin;
    } else {
        // If skin value is invalid, use defaultSkin and print a warning
        personality->skin = strdup(defaultSkin); // Use strdup to copy defaultSkin
    }

    // Set the name, if provided
    char* defaultName = "AqtionBot";

    checkval = json_object_get(root, "name");
    char* validatedName = validate_pref_string(checkval, 0);
    if (validatedName != NULL) {
        // If name value is valid, use it
        personality->name = validatedName;
    } else {
        // If name value is invalid, use defaultName and print a warning
        personality->name = strdup(defaultName); // Use strdup to copy defaultName
    }

    // Clean up
    json_decref(root);

    return personality;
}

int main() {
    // Example usage of load_bot_personality
    bot_personality_t* personality = load_bot_personality("bot.json");
    if (personality != NULL) {        
        printf("\n");
        printf("Weapon Preferences:");
        for (int i = 1; i < WEAPON_COUNT; i++) { // Assuming WEAPON_PREFS_SIZE is defined
            printf(" %f", personality->weapon_prefs[i]);
        }
        printf("\nItem Preferences:");
        for (int i = 1; i < ITEM_COUNT; i++) { // Assuming ITEM_PREFS_SIZE is defined
            printf(" %f", personality->item_prefs[i]);
        }
        printf("\n");
        // Print Map Preferences
        printf("Map Preferences: %f\n", personality->map_prefs);
        // Print Chat Demeanor
        printf("Chat Demeanor: %f\n", personality->chat_demeanor);
        
        // Print Combat Demeanor
        printf("Combat Demeanor: %f\n", personality->combat_demeanor);
        
        // Print Skin
        printf("Skin: %s\n", personality->skin);
        
        printf("\n");
        //
        if (!warningCount)
            printf("Bot personality loaded successfully.\n");
        else
            printf("Loaded with %i warnings\n", warningCount);

        // Remember to free the allocated memory for personality
        free(personality);
    } else {
        printf("Failed to load bot personality.\n");
    }
    return 0;
}

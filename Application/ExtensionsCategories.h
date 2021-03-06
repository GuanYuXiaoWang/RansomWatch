#pragma once

#include <unordered_map>

// extensions categories used for extensions trigger, we divided most known categories to types
enum CATEGORIES {
	CATEGORIES_DOCS,
	CATEGORIES_XLS,
	CATEGORIES_PPT,
	CATEGORIES_OUTLOOK,
	CATEGORIES_SDOCS,
	CATEGORIES_IMAGES,
	CATEGORIES_ARCHIVE,
	CATEGORIES_DB,
	CATEGORIES_CODE,
	CATEGORIES_MUSIC,
	CATEGORIES_VIDEO,
	NUM_CATEGORIES_NO_OTHERS,
	NUM_CATEGORIES_WITH_OTHERS = NUM_CATEGORIES_NO_OTHERS + 7
};

uint16_t ExtensionCategory(const wchar_t * Extension);
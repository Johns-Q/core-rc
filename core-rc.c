///
///	@file core-rc.c		@brief core runtime configuration functions
///
///	Copyright (c) 2009, 2010 by Lutz Sammer.  All Rights Reserved.
///
///	Contributor(s):
///
///	License: AGPLv3
///
///	This program is free software: you can redistribute it and/or modify
///	it under the terms of the GNU Affero General Public License as
///	published by the Free Software Foundation, either version 3 of the
///	License.
///
///	This program is distributed in the hope that it will be useful,
///	but WITHOUT ANY WARRANTY; without even the implied warranty of
///	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
///	GNU Affero General Public License for more details.
///
///	$Id$
//////////////////////////////////////////////////////////////////////////////

///
///	@defgroup CoreRc	The core runtime configuration module.
///
///	This module contains the runtime configuration and configuration file
///	parsing functions.
///	The syntax of the config file is the CoreScript array constructor.
///
///
///	@todo cleanup module
///	@todo rewrite handling of global array
///	@todo write examples
///	@todo write man pages
///
///	@section examples
///
///	A simple example (incomplete) how to use core-rc:
///	@code
///		Config * config;
///		config = ConfigReadFile(0, NULL, "my.core-rc");
///	@endcode
///	Load runtime configuration from "my.core-rc".
///	@code
///		ConfigFreeMem(config);
///	@endcode
///	Free memory used by configuration.  "config" can't be used after
///	this point.
///
///	@section internals
///
///	The core-rc object pointers are tagged pointers.
///	<tt>
///	@n..3.........2.........1.........0
///	@n[10987654321098765432109876543210]
///	@n[xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx1]	x = 31 bit signed integer
///	@n[xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx10]	x = 30 bit floating point
///	@n[xxxxxxxxxxxxxxxxxxxxxxxxxxxxx100]	x = 32 bit string pointer
///	@n[xxxxxxxxxxxxxxxxxxxxxxxxxxxxx000]	x = 32 bit object pointer
///	</tt>
///
/// @{

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#ifndef NO_DEBUG_CORE_RC
#define NO_DEBUG_CORE_RC		///< debug enabled/disabled
#endif
#ifdef CORE_RC_TEST
#define USE_CORE_RC_PRINT		///< include core-rc print support
#define USE_CORE_RC_WRITE		///< include core-rc write support
#endif

#include "core-array/core-array.h"
#include "core-rc.h"

// ----------------------------------------------------------------------------
// Object pool
// ------------------------------------------------------------------------ //

/**
**	Allocate new object node.
**
**	@returns random object 8 bytes aligned
**
**	@todo FIXME: need to write an object pool.
*/
static inline ConfigObject *ConfigObjectNew(void)
{
    ConfigObject *object;

    // object is 4 bytes on 32bit 8 bytes on 64bit.
    object = malloc(sizeof(*object));
#ifdef DEBUG_CORE_RC
    if ((size_t) object & 7) {
	fprintf(stderr, "Object: %p\n", object);
	abort();
    }
#endif

    return object;
}

/**
**	Deallocate object node.
**
**	@param object	config pointer object
**
**	@todo FIXME: need to write an object pool.
*/
static inline void ConfigObjectDel(ConfigObject * object)
{
    free(object);
}

// ------------------------------------------------------------------------ //
// String pool
// ------------------------------------------------------------------------ //

///
///	@defgroup stringpool The string-pool module.
///
///	This module handles string-pools.  String pools handles string
///	memory managment.  Only one copy of equal strings is stored.
///	
/// @{

    /// end of various program segments
    ///@{
extern char etext, edata, end;

    ///@}

    /// bigger strings are malloced
static const size_t STRING_POOL_MAX_SIZE = 4096;

    /// pool size
static const size_t STRING_POOL_SIZE = 8192;

/**
**	String-pool typedef.
*/
typedef struct _string_pool_ StringPool;

/**
**	String-pool node typedef
*/
typedef struct _string_node_ StringNode;

/**
**	A node of string pool structure.
*/
struct _string_node_
{
    StringNode *Next;			///< next node
    uint32_t Size;			///< allocated size of node
    uint32_t Free;			///< free bytes in node
    char Data[1];			///< string memory
};

/**
**	String-pool variables structure.
*/
struct _string_pool_
{
    StringNode *Pools;			///< list of pools
    Array *Strings;			///< lookup of strings
};

/**
**	Generate key for string.
**
**	@param len	length of key string
**	@param str	string for key
**
**	@returns a 32bit/64bit key for first part of string.
*/
static inline size_t StringPoolKeygen(int len, const uint8_t * str)
{
    size_t key;

    key = 0;
    // 8/4 bytes -> index-key
    switch (len) {
	default:
#if SIZE_MAX == (18446744073709551615UL)
	    // 64 bit version
	case 8:
	    key |= str[7] << 0;
	case 7:
	    key |= str[6] << 8;
	case 6:
	    key |= str[5] << 16;
	case 5:
	    key |= str[4] << 24;
	case 4:
	    key |= (size_t) str[3] << 32;
	case 3:
	    key |= (size_t) str[2] << 40;
	case 2:
	    key |= (size_t) str[1] << 48;
	case 1:
	    key |= (size_t) str[0] << 56;
	    break;
#else
	    // 32 bit version
	case 4:
	    key |= str[3] << 0;
	case 3:
	    key |= str[2] << 8;
	case 2:
	    key |= str[1] << 16;
	case 1:
	    key |= str[0] << 24;
	    break;
#endif
	case 0:
	    break;
    }

    return key;
}

/**
**	Allocate string from pool.
**
**	@param pool	string-pool to use
**	@param string	string to be copied into pool
**
**	@returns pointer to unique string
*/
static const char *StringPoolAlloc(StringPool * pool, const char *string)
{
    unsigned len;
    char *dst;
    StringNode *node;

    if (string < &edata) {		// string from C constant area
#ifdef DEBUG_CORE_RC
	fprintf(stderr, "EDATA %s\n", string);
#endif
	return string;
    }
    len = strlen(string) + 1;
    if (len >= STRING_POOL_MAX_SIZE) {	// too big, just allocate
	return strdup(string);
    }
    // find pool with empty slot
    for (node = pool->Pools; node; node = node->Next) {
	if (node->Free >= len) {
	    goto out;
	}
    }

    node = malloc(STRING_POOL_SIZE);	// page aligned nodes
    node->Next = pool->Pools;
    pool->Pools = node;
    node->Free = node->Size = STRING_POOL_SIZE - sizeof(node) + sizeof(char);

  out:
    dst = node->Data + node->Size - node->Free;
    memcpy(dst, string, len);
    node->Free -= len;

    return dst;
}

#ifdef never_DEBUG_CORE_RC

/**
**	Helper for string pool dump.
**
**	@param array	array of strings in pool 
**	@param level	indent level
*/
static void StringPoolDump0(const Array * array, int level)
{
    size_t index;
    size_t *value;

    index = 0;
    value = ArrayFirst(array, &index);
    while (value) {
	printf("%*s%0*zx ", level, "", (int)sizeof(size_t) * 2, index);

	if (*value & 4) {		// strings are tagged
	    const char *string;
	    ConfigObject *object;

	    object = (ConfigObject *) * value;
	    string = ((ConfigObject *) ((size_t) object & ~7))->Pointer;
	    printf("= '%s'\n", string);
	} else {
	    printf("->\n");
	    StringPoolDump0((Array *) * value, level + 8);
	}

	value = ArrayNext(array, &index);
    }
}

/**
**	Dump string pool.
**
**	@param pool	string pool variables
**	@param level	indent level
*/
static void StringPoolDump(const StringPool * pool, int level)
{
    StringNode *node;

    StringPoolDump0(pool->Strings, level);
    for (node = pool->Pools; node; node = node->Next) {
	printf("%p[%d/%d]", node, node->Free, node->Size);
    }
    if (pool->Pools) {
	printf("\n");
    }
}

#endif

/**
**	Create a new empty string-pool.
**
**	@returns new empty string pool.
*/
static StringPool *StringPoolNew(void)
{
    return calloc(1, sizeof(StringPool));
}

/**
**	Free memory used by lookup array.
**
**	@param array	string pool lookup array
*/
static void StringPoolDelStrings(Array * array)
{
    size_t index;
    size_t *value;

    index = 0;
    value = ArrayFirst(array, &index);
    while (value) {
	size_t temp;

	temp = *value;
	if (temp & 4) {			// strings are tagged
	    char *s;
	    size_t n;

	    s = ((ConfigObject *) (temp & ~7))->Pointer;
	    n = strlen(s) + 1;
	    if (n >= STRING_POOL_MAX_SIZE) {	// too big, must free
		free(s);
	    }
	    ConfigObjectDel((ConfigObject *) (temp & ~7));
	} else {
	    StringPoolDelStrings((Array *) temp);
	}

	value = ArrayNext(array, &index);
    }
    ArrayFree(array);
}

/**
**	Delete a string-pool.
**
**	All memory used by string-pool is freed.
**
**	@param pool	string-pool to be freed.
*/
static void StringPoolDel(StringPool * pool)
{
    StringNode *node;

    StringPoolDelStrings(pool->Strings);

    while ((node = pool->Pools)) {	// free all nodes
	pool->Pools = node->Next;
	free(node);
    }

    free(pool);
}

/**
**	Intern string.
**
**	@param pool	pool to add string
**	@param string	string to add
*/
static ConfigObject *StringPoolIntern(StringPool * pool, const char *string)
{
    int len;
    size_t key;
    Array **parent;
    size_t *val;
    const char *str;
    ConfigObject *object;

#ifdef DEBUG
    object = NULL;
#endif
    str = string;
    len = strlen(str);
    parent = &pool->Strings;
    do {
	key = StringPoolKeygen(len, (uint8_t *) str);
	val = ArrayIns(parent, key, 0);
	if (!*val) {			// new key insert string and ready
	    string = StringPoolAlloc(pool, string);
	    object = ConfigObjectNew();
	    object->Pointer = (char *)string;
	    *val = (size_t) object | 4;
	    object = (ConfigObject *) * val;
	    break;
	}

	str += sizeof(size_t);
	len -= sizeof(size_t);

	// *val is string or array
	if (*val & 4) {			// |4 are string objects
	    const char *old;
	    int i;

	    object = (ConfigObject *) * val;
	    old = ((ConfigObject *) ((size_t) object & ~7))->Pointer;

	    i = strlen(old) - (str - string);
	    // compare string, if not same
	    if (i == len && (len < 0 || !strcmp(str, old + (str - string)))) {
		break;
	    }
	    // take next 4/8 bytes as next key
	    key = StringPoolKeygen(i, (uint8_t *) old + (str - string));
	    *val = (size_t) ArrayNew();
	    ArrayIns((Array **) val, key, (size_t) object);
	}
	parent = (Array **) val;

    } while (len >= 0);

    return object;
}

/// @}

// ----------------------------------------------------------------------------

static StringPool *ConfigStrings;	///< storage of parser strings

/**
**	Check if object is fixed integer.
**
**	@param object	tagged object pointer
**
**	@returns true if object is fixed integer object, false otherwise.
*/
static inline int ConfigIsFixed(const ConfigObject * object)
{
    return (size_t) object & 1;
}

/**
**	Check if object is floating-point number.
**
**	@param object	tagged object pointer
*/
static inline int ConfigIsFloat(const ConfigObject * object)
{
    return !ConfigIsFixed(object) && (size_t) object & 2;
}

/**
**	Check if object is a pointer object.
**
**	@param object	tagged object pointer
*/
static inline int ConfigIsPointer(const ConfigObject * object)
{
    return object && !((size_t) object & 7);
}

/**
**	Check if object is a word object.
**
**	@param object	tagged object pointer
*/
static inline int ConfigIsWord(const ConfigObject * object)
{
    return (((size_t) object & 7) == 4);
}

/**
**	Check if object is an array object.
**
**	@param object	tagged object pointer
*/
static inline int ConfigIsArray(const ConfigObject * object)
{
    return object && !((size_t) object & 7);
}

/**
**	Create a new fixed integer object.
**
**	@param integer	31/63 bit used unsigned integer
**
**	@returns tagged fixed integer object pointer.
*/
static inline ConfigObject *ConfigNewFixed(size_t integer)
{
    return (ConfigObject *) ((integer << 1) + 1);
}

/**
**	Create a new floating-point number object.
**
**	@param number	30/62 bit used floating-point number
**
**	@returns tagged floating point object pointer.
*/
static inline ConfigObject *ConfigNewFloat(double number)
{
    union
    {
#if SIZE_MAX == (18446744073709551615UL)
	double f;
#else
	float f;
#endif
	size_t i;
    } c;

    c.f = number;
    return (ConfigObject *) ((c.i & ~3) | 2);
}

/**
**	Create a new array object.
**	
**	@param array	core array converted into array object
**
**	@returns tagged array object pointer.
**
**	@todo should use an object pool
*/
static inline ConfigObject *ConfigNewArray(const Array * array)
{
    ConfigObject *object;

    object = ConfigObjectNew();
    object->Pointer = (void *)array;

    return object;
}

/**
**	Create a new word object.
**
**	@param string	string converted into fixed string object
**
**	@returns tagged fixed string object pointer.
**
**	@todo should use an object pool
**	@todo second array string -> object unnecessary
*/
static inline ConfigObject *ConfigNewWord(const char *string)
{
    return StringPoolIntern(ConfigStrings, string);
}

/**
**	Convert fixed object to C integer.
**
**	@param object	tagged object pointer
**
**	@returns fixed integer part of object pointer.
*/
static inline ssize_t ConfigInteger(const ConfigObject * object)
{
    return (ssize_t) object >> 1;
}

/**
**	Convert float object to C double.
*
**	@param object	tagged object pointer
**
**	@returns floating point part of object pointer.
*/
static inline double ConfigDouble(const ConfigObject * object)
{
    union
    {
#if SIZE_MAX == (18446744073709551615UL)
	double f;
#else
	float f;
#endif
	size_t i;
    } c;

    c.i = (size_t) object & ~2;
    return c.f;
}

/**
**	Convert word object to C string.
**
**	@param object	tagged object pointer
**
**	@returns pointer to fixed word string, stored in object.
*/
static inline const char *ConfigString(const ConfigObject * object)
{
    object = (const ConfigObject *)((size_t) object & ~7);
    return object->Pointer;
}

/**
**	Convert array object to C array.
**
**	@param object	tagged object pointer
*
**	@returns pointer to array, stored in object.
*/
static inline Array *ConfigArray(const ConfigObject * object)
{
    return object->Pointer;
}

/**
**	Check if value is a fixed integer object.
**
**	@param object		tagged object pointer
**	@param[out] result	fixed integer object is stored in result
**
**	@returns true if object is fixed integer object, false otherwise.
*/
int ConfigCheckInteger(const ConfigObject * object, ssize_t * result)
{
    if (ConfigIsFixed(object)) {
	*result = ConfigInteger(object);
	return 1;
    }
    return 0;
}

/**
**	Check if value is a floating point object.
**
**	@param object		tagged object pointer
**	@param[out] result	floating point object is stored in result
**
**	@returns true if object is floating point object, false otherwise.
*/
int ConfigCheckDouble(const ConfigObject * object, double *result)
{
    if (ConfigIsFloat(object)) {
	*result = ConfigDouble(object);
	return 1;
    }
    return 0;
}

/**
**	Check if value is a fixed string object.
**
**	@param object		tagged object pointer
**	@param[out] result	fixed string is stored in result
**
**	@returns true if object is fixed string object, false otherwise.
*/
int ConfigCheckString(const ConfigObject * object, const char **result)
{
    if (ConfigIsWord(object)) {
	*result = ConfigString(object);
	return 1;
    }
    return 0;
}

/**
**	Check if value is an array object.
**
**	@param object		tagged object pointer
**	@param[out] result	array object is stored in result
**
**	@returns true if object is an array object, false otherwise.
*/
int ConfigCheckArray(const ConfigObject * object, const ConfigObject ** result)
{
    if (ConfigIsArray(object)) {
	*result = object;
	return 1;
    }
    return 0;
}

/**
**	Lookup config object.
**
**	@param config	config dictionary or sub array
**	@param ap	array of strings NULL terminated, to select value
**
**	@returns object stored in dictionary at index ap
*/
static const ConfigObject *ConfigGet(const ConfigObject * config, va_list ap)
{
    const char *name;

    // loop over all index keys
    while ((name = va_arg(ap, const char *)))
    {
	if (!ConfigIsArray(config)) {
	    fprintf(stderr, "array required for index '%s'\n", name);
	    return NULL;
	}
	config = (const ConfigObject *)
	    ArrayGet(ConfigArray(config), (size_t) ConfigNewWord(name));
    }
    return config;
}

/**
**	Get config any value object.
**
**	@param config		config dictionary
**	@param[out] result	object result
**	@param ...		list of strings NULL terminated, to select value
**
**	@returns true if value found at index in dictionary.
*/
int ConfigGetObject(const ConfigObject * config, const ConfigObject ** result,
    ...)
{
    va_list ap;
    const ConfigObject *value;

    va_start(ap, result);
    value = ConfigGet(config, ap);
    va_end(ap);

    if (value) {
	*result = value;
	return 1;
    }
    return 0;
}

/**
**	Get config integer object.
**
**	@param config		config dictionary
**	@param[out] result	string result
**	@param ...		list of strings NULL terminated, to select value
**
**	@returns true if value found at index in dictionary.
*/
int ConfigGetInteger(const ConfigObject * config, ssize_t * result, ...)
{
    va_list ap;
    const ConfigObject *value;

    va_start(ap, result);
    value = ConfigGet(config, ap);
    va_end(ap);

    if (ConfigIsFixed(value)) {
	*result = ConfigInteger(value);
	return 1;
    }
    if (value) {
	fprintf(stderr, "value isn't a fixed integer\n");
    }
    return 0;
}

/**
**	Get config double object.
**
**	@param config		config dictionary
**	@param[out] result	string result
**	@param ...		list of strings NULL terminated, to select value
**
**	@returns true if value found at index in dictionary.
*/
int ConfigGetDouble(const ConfigObject * config, double *result, ...)
{
    va_list ap;
    const ConfigObject *value;

    va_start(ap, result);
    value = ConfigGet(config, ap);
    va_end(ap);

    if (ConfigIsFloat(value)) {
	*result = ConfigDouble(value);
	return 1;
    }
    if (value) {
	fprintf(stderr, "value isn't a double\n");
    }
    return 0;
}

/**
**	Get config string object.
**
**	@param config		config dictionary
**	@param[out] result	string result
**	@param ...		list of strings NULL terminated, to select value
**
**	@returns true if value found at index in dictionary.
*/
int ConfigGetString(const ConfigObject * config, const char **result, ...)
{
    va_list ap;
    const ConfigObject *value;

    va_start(ap, result);
    value = ConfigGet(config, ap);
    va_end(ap);

    if (ConfigIsWord(value)) {
	*result = ConfigString(value);
	return 1;
    }
    if (value) {
	fprintf(stderr, "value isn't a string\n");
    }
    return 0;
}

/**
**	Get config array object.
**
**	@param config		config dictionary
**	@param[out] result	array result
**	@param ...		list of strings NULL terminated, to select value
**
**	@returns true if value found at index in dictionary.
*/
int ConfigGetArray(const ConfigObject * config, const ConfigObject ** result,
    ...)
{
    va_list ap;
    const ConfigObject *value;

    va_start(ap, result);
    value = ConfigGet(config, ap);
    va_end(ap);

    if (ConfigIsArray(value)) {
	*result = value;
	return 1;
    }
    if (value) {
	fprintf(stderr, "value isn't an array\n");
    }
    return 0;
}

/**
**	Get first value from config array.
**
**	@param array		config array value
**	@param[in,out] index	config index value
*/
const ConfigObject *ConfigArrayFirst(const ConfigObject * array,
    const ConfigObject ** index)
{
    const ConfigObject **value;

    value =
	(const ConfigObject **)ArrayFirst(ConfigArray(array),
	(size_t *) index);
    return value ? *value : NULL;
}

/**
**	Get next value from config array.
**
**	@param array		config array value
**	@param[in,out] index	config index value
*/
const ConfigObject *ConfigArrayNext(const ConfigObject * array,
    const ConfigObject ** index)
{
    const ConfigObject **value;

    value =
	(const ConfigObject **)ArrayNext(ConfigArray(array), (size_t *) index);
    return value ? *value : NULL;
}

/**
**	Get first value with integer key from config array.
**
**	@param array		config array value
**	@param[in,out] index	config index value
*/
const ConfigObject *ConfigArrayFirstFixedKey(const ConfigObject * array,
    const ConfigObject ** index)
{
    const ConfigObject **value;

    value = (const ConfigObject **)
	ArrayFirst(ConfigArray(array), (size_t *) index);
    while (value) {
	if (ConfigIsFixed(*index)) {
	    return *value;
	}
	value = (const ConfigObject **)
	    ArrayNext(ConfigArray(array), (size_t *) index);
    }
    return (const ConfigObject *)value;
}

/**
**	Get next value with integer key from config array.
**
**	@param array		config array value
**	@param[in,out] index	config index value
*/
const ConfigObject *ConfigArrayNextFixedKey(const ConfigObject * array,
    const ConfigObject ** index)
{
    const ConfigObject **value;

    do {
	value = (const ConfigObject **)
	    ArrayNext(ConfigArray(array), (size_t *) index);
	if (!value) {
	    return (const ConfigObject *)value;
	}
    } while (!ConfigIsFixed(*index));

    return *value;
}

#ifdef USE_CORE_RC_PRINT

/**
**	Print config object.
**
**	@param object	tagged object pointer
**	@param level	indent level
**	@param out	output stream
**
**	@todo this is nice print, but no beautifier yet.
*/
void ConfigPrint(const ConfigObject * object, int level, FILE * out)
{
    if (!object) {
	printf("nil");
	return;
    }
    if (ConfigIsFixed(object)) {
	fprintf(out, "%zd", ConfigInteger(object));
    } else if (ConfigIsFloat(object)) {
	fprintf(out, "%.1g", ConfigDouble(object));
    } else if (ConfigIsWord(object)) {
	fprintf(out, "\"%s\"", ConfigString(object));
    } else {
	size_t index;
	size_t *value;
	Array *array;

	//fprintf(out, "[\n");
	fprintf(out, "[;%p\n", object);
	array = ConfigArray(object);
	index = 0;
	value = ArrayFirst(array, &index);
	while (value) {
	    const ConfigObject *lvalue;

	    //
	    //	string 
	    //
	    lvalue = (const ConfigObject *)index;
	    // FIXME: must check if word contains only a-zA-Z0-9_-
	    if (ConfigIsWord(lvalue)) {
		fprintf(out, "%*s%s = ", level, "", ConfigString(lvalue));
	    } else {
		fprintf(out, "%*s[", level, "");
		ConfigPrint(lvalue, level + 2, out);
		fprintf(out, "] = ");
	    }

	    ConfigPrint((const ConfigObject *)*value, level + 4, out);
	    fprintf(out, "\n");

	    value = ArrayNext(array, &index);
	}
	fprintf(out, "%*s]", level, "");
    }
}
#endif

// ------------------------------------------------------------------------ //
// Config file parser
// ------------------------------------------------------------------------ //

///
///	@defgroup parse The parse module.
///
///	This module handles parsing of config file.
///
/// @{

/**
**	Parse open file stack typedef.
*/
typedef struct _parse_file_stack_ ParseFileStack;

/**
**	Parse open file stack structure.
*/
struct _parse_file_stack_
{
    const char *Name;			///< previous file name
    FILE *File;				///< previous file stream
    int LineNr;				///< previous line number
};

static const char *ParseName;		///< current file name
static FILE *ParseFile;			///< current file stream
static int ParseLineNr;			///< current line number

//static ParseFileStack *ParseFiles;	///< pushed files
//static int ParseFileN;			///< number of pushed files

static ConfigObject **ParseStack;	///< parser stack
static int ParseSP;			///< parser stack pointer
static int ParseStackSize;		///< parser stack size

static Array *ParseGlobalArray;		///< global array
static Array *ParseCurrentArray;	///< current array
static int ParseCurrentIndex;		///< current array index
static Array **ParseCurrentLvalue;	///< current lvalue

static void ParseRecursive(const char *);	///< parse recursive file

#ifdef never_DEBUG_CORE_RC

/**
**	Print debug about object.
**
**	@param object	config object
*/
static void ParseDebug(const ConfigObject * object)
{
    if (!object) {
	printf("nil");
	return;
    }
    if (ConfigIsFixed(object)) {
	printf("fixed(%zd)", ConfigInteger(object));
    } else if (ConfigIsFloat(object)) {
	printf("float(%.1g)", ConfigDouble(object));
    } else if (ConfigIsWord(object)) {
	printf("word(%s)", ConfigString(object));
    } else {
	printf("array(%p)", object);
    }
}

/**
**	Print a parser object
**
**	@param object	config object
*/
static void ParsePrint(const ConfigObject * object)
{
    if (!object) {
	printf("nil");
	return;
    }
    if (ConfigIsFixed(object)) {
	printf("%zd", ConfigInteger(object));
    } else if (ConfigIsFloat(object)) {
	printf("%.1g", ConfigDouble(object));
    } else if (ConfigIsWord(object)) {
	printf("\"%s\"", ConfigString(object));
    } else {
	printf("[=%p]", object);
    }
}

/**
**	Dump parsed config
**
**	@param array	config main array
**	@param level	indent level
**
**	@todo nil?
*/
static void ParseDump(const Array * array, int level)
{
    size_t index;
    size_t *value;

    index = 0;
    value = ArrayFirst(array, &index);
    while (value) {
	const ConfigObject *object;

	printf("%*s[", level, "");
	ParsePrint((const ConfigObject *)index);
	printf("] = ");
	object = (const ConfigObject *)*value;
	if (ConfigIsArray(object)) {
	    printf("[\n");
	    ParseDump(ConfigArray(object), level + 4);
	    printf("%*s]\n", level, "");
	} else {
	    ParsePrint(object);
	    printf("\n");
	}

	value = ArrayNext(array, &index);
    }
}
#endif

/**
**	Pop object from parser stack.
**
**	@returns	top config object on parser stack
*/
static ConfigObject *ParsePop(void)
{
    if (!ParseSP) {
	fprintf(stderr, "internal error no objects on stack\n");
	return NULL;
    }
    return ParseStack[--ParseSP];
}

/**
**	Push object on parser stack.
**
**	@param object	config object
*/
static void ParsePush(const ConfigObject * object)
{
    if (ParseSP + 1 == ParseStackSize) {	// reach stack end
	ParseStackSize += 8;
	ParseStack = realloc(ParseStack, ParseStackSize * sizeof(*ParseStack));
    }
    ParseStack[ParseSP++] = (ConfigObject *) object;
}

/**
**	Push integer.
**
**	@param val	push val as integer object on value stack
*/
static void ParsePushI(int val)
{
    ParsePush(ConfigNewFixed(val));
}

/**
**	Push double float.
**
**	@param val	push val as floating point object on value stack
*/
static void ParsePushF(double val)
{
    ParsePush(ConfigNewFloat(val));
}

/**
**	Push string.
**
**	@param val	push val as string object on value stack
*/
static void ParsePushS(const char *val)
{
    ParsePush(ConfigNewWord(val));
}

/**
**	Push array.
**
**	@param val	push val as array object on value stack
*/
static void ParsePushA(const Array * val)
{
    ParsePush(ConfigNewArray(val));
}

/**
**	Push nil.
*/
static void ParsePushNil(void)
{
    ParsePush(NULL);
}

/**
**	Parse include statement.
**
**	@param file	include file name
*/
static void ParseInclude(const ConfigObject * file)
{
#ifdef DEBUG_CORE_RC
    printf("Must include '%s'\n", ConfigString(file));
#endif
    ParseRecursive(ConfigString(file));
}

/**
**	Generate store into array.
**
**	@param index	index key into array
**	@param value	value stored into array
*/
static void ParseArrayAddItem(const ConfigObject * index,
    const ConfigObject * value)
{
#ifdef never_DEBUG_CORE_RC
    printf("add %p:", index);
    ParseDebug(index);
    printf("=%p:", value);
    ParseDebug(value);
    printf("\n");
#endif
    if (ConfigIsFixed(index)) {
	ParseCurrentIndex = ConfigInteger(index) + 1;

    }
    ArrayIns(&ParseCurrentArray, (size_t) index, (size_t) value);
}

/**
**	Generate store into next index of array.
**
**	@param value	value stored into array
**
**	@note ParseCurrentIndex is incremented in ParseArrayAddItem.
*/
static void ParseArrayNextItem(const ConfigObject * value)
{
    ParseArrayAddItem(ConfigNewFixed(ParseCurrentIndex), value);
}

/**
**	Prepare new array.
*/
void ParseArrayStart(void)
{
    ParsePushA(ParseCurrentArray);
    ParsePushI(ParseCurrentIndex);

    ParseCurrentArray = ArrayNew();
    ParseCurrentIndex = 0;
}

/**
**	Finish array.
*/
void ParseArrayFinal(void)
{
    ConfigObject *object;
    Array *array;

    object = ParsePop();
    if (!object || !ConfigIsFixed(object)) {
	fprintf(stderr, "internal error\n");
	exit(-1);
    }
    ParseCurrentIndex = ConfigInteger(object);

    object = ParsePop();
    if (!object || !ConfigIsArray(object)) {
	fprintf(stderr, "internal error\n");
	exit(-1);
    }
    array = ConfigArray(object);
    ConfigObjectDel(object);

    ParsePushA(ParseCurrentArray);

    ParseCurrentArray = array;
}

/**
**	Generate start of lvalue.
*/
static void ParseLvalue(void)
{
    ParseCurrentLvalue = &ParseCurrentArray;
}

/**
**	Generate assign operator
*/
static void ParseAssign(const ConfigObject * index, const ConfigObject * value)
{
    const ConfigObject **vp;

#ifdef never_DEBUG_CORE_RC
    printf("assign %p:", index);
    ParseDebug(index);
    printf("=%p:", value);
    ParseDebug(value);
    printf("\n");
#endif
    if (*ParseCurrentLvalue == ParseGlobalArray) {
	vp = (const ConfigObject **)ArrayIns(ParseCurrentLvalue,
	    (size_t) index, (size_t) value);
	ParseGlobalArray = *ParseCurrentLvalue;
    } else {
	vp = (const ConfigObject **)ArrayIns(ParseCurrentLvalue,
	    (size_t) index, (size_t) value);
    }
    if (*vp != value) {
	// FIXME: value already set, loose memory!
	printf("need to overwrite old value\n");
	*vp = value;
    }
}

/**
**	Generate dot operator
*/
static void ParseDot(const ConfigObject * global, const ConfigObject * index)
{
    ConfigObject *value;

#ifdef never_DEBUG_CORE_RC
    printf("dot %p:", global);
    ParseDebug(global);

    printf(",%p:", index);
    ParseDebug(index);
    printf("\n");
#endif

    value = (ConfigObject *) ArrayGet(*ParseCurrentLvalue, (size_t) global);

    if (!value) {
	value = ConfigNewArray(ArrayNew());
	if (*ParseCurrentLvalue == ParseGlobalArray) {
	    ArrayIns(ParseCurrentLvalue, (size_t) global, (size_t) value);

	    ParseGlobalArray = *ParseCurrentLvalue;
	} else {
	    ArrayIns(ParseCurrentLvalue, (size_t) global, (size_t) value);
	}

    } else if (!ConfigIsArray(value)) {
	fprintf(stderr, "lvalue required\n");
	ParsePush(index);
	return;
    }
    ParseCurrentLvalue = (Array **) & value->Pointer;

    ParsePush(index);
}

/**
**	Generate string .
*/
static void ParseStringCat(const ConfigObject * o1, const ConfigObject * o2)
{
    const char *s1;
    const char *s2;
    char *buf;

    if (!ConfigIsWord(o1) || !ConfigIsWord(o2)) {
	fprintf(stderr, "wrong types for string-cat operator\n");
	ParsePushS("error");
	return;
    }
    s1 = ConfigString(o1);
    s2 = ConfigString(o2);

    buf = alloca(strlen(s1) + strlen(s2) + 1);
    strcat(stpcpy(buf, s1), s2);
    ParsePushS(buf);
}

/**
**	Generate variable.
**
**	@param v	variable name
*/
static void ParseVariable(const ConfigObject * v)
{
    const ConfigObject *value;

    value = (const ConfigObject *)ArrayGet(ParseGlobalArray, (size_t) v);
    ParsePush(value);
}

// ----------------------------------------------------------------------------

    /// peg parser generator class of external entry points
#define YY_PARSE(T)	static T

//#define YY_DEBUG

/**
**	Macro of parser generator, to read next bytes
*/
#define YY_INPUT(buf, result, max_size) \
    do { \
	result = fread(buf, 1, max_size, ParseFile); \
    } while (0)

/*
**	Parser generator generated:
*/
#include "core-rc_parser.c"

/**
**	Handle parser generator error message.
**
**	@param message	error message
*/
static void yyerror(const char *message)
{
    fprintf(stderr, "%s:%d: %s", ParseName, ParseLineNr, message);
    if (yytext[0]) {
	fprintf(stderr, " near token '%s'", yytext);
    }
    if (yypos < yylimit || !feof(ParseFile)) {
	yybuf[yylimit] = '\0';
	fprintf(stderr, " before text \"");
	while (yypos < yylimit) {
	    if ('\n' == yybuf[yypos] || '\r' == yybuf[yypos]) {
		break;
	    }
	    fputc(yybuf[yypos++], stderr);
	}
	if (yypos == yylimit) {
	    int c;

	    while (EOF != (c = fgetc(ParseFile)) && '\n' != c && '\r' != c) {
		fputc(c, stderr);
	    }
	}
	fputc('\"', stderr);
    }
    fprintf(stderr, "\n");
    // exit(1);
}

// ----------------------------------------------------------------------------

///
///	saved peg parser state
///
struct _saved_state_
{
    /// parser generator variables
    //@{
    char *yybuf;
    int yybuflen;
    int yypos;
    int yylimit;
    char *yytext;
    int yytextlen;
    int yybegin;
    int yyend;
    int yytextmax;
    yythunk *yythunks;
    int yythunkslen;
    int yythunkpos;
    //@}

    const char *Name;			///< previous file name
    FILE *File;				///< previous file stream
    int LineNr;				///< previous line number
};

/**
**	Push parser state for includes.
**
**	@param filename	config include file name
*/
static void ParseRecursive(const char *filename)
{
    struct _saved_state_ s;
    FILE *file;
    char *buf;

    //
    // Save current state.
    //
    s.yybuf = yybuf;
    s.yybuflen = yybuflen;
    s.yypos = yypos;
    s.yylimit = yylimit;
    s.yytext = yytext;
    s.yytextlen = yytextlen;
    s.yybegin = yybegin;
    s.yyend = yyend;
    s.yytextmax = yytextmax;
    s.yythunks = yythunks;
    s.yythunkslen = yythunkslen;
    s.yythunkpos = yythunkpos;
    s.Name = ParseName;
    s.File = ParseFile;
    s.LineNr = ParseLineNr;

    //
    //	initialize
    //
    yybuf = 0;
    yybuflen = 0;
    yypos = 0;
    yylimit = 0;
    yytext = 0;
    yytextlen = 0;
    yybegin = 0;
    yyend = 0;
    yytextmax = 0;
    yythunks = 0;
    yythunkslen = 0;
    yythunkpos = 0;

    file = fopen(filename, "rb");
    if (!file) {
	// absolute path
	if (filename[0] != '/' && (filename[0] != '.' || filename[1] != '/')
	    && (filename[0] != '.' || filename[1] != '.'
		|| filename[2] != '/')) {
	    char *s;

	    buf = alloca(strlen(ParseName) + strlen(filename) + 2);
	    if ((s = strrchr(ParseName, '/'))) {
		s = stpncpy(buf, ParseName, s - ParseName + 1);
		strcpy(s, filename);
	    } else {
		strcpy(buf, filename);
	    }
	    filename = buf;
	    file = fopen(filename, "rb");
	}
	if (!file) {
	    fprintf(stderr, "can't open include file '%s'\n", filename);
	}
    }
    if (file) {
	ParseName = filename;
	ParseFile = file;
	ParseLineNr = 1;

	if (yyparse()) {
#ifdef DEBUG_CORE_RC
	    printf("success\n");
#endif
	} else {
	    yyerror("syntax error");
	}
	fclose(file);
    }
    //
    //	cleanup
    //
    free(yybuf);
    free(yytext);
    free(yythunks);
    //free(yyvals);

    //
    // Restore current state
    //
    yybuf = s.yybuf;
    yybuflen = s.yybuflen;
    yypos = s.yypos;
    yylimit = s.yylimit;
    yytext = s.yytext;
    yytextlen = s.yytextlen;
    yybegin = s.yybegin;
    yyend = s.yyend;
    yytextmax = s.yytextmax;
    yythunks = s.yythunks;
    yythunkslen = s.yythunkslen;
    yythunkpos = s.yythunkpos;
    ParseName = s.Name;
    ParseFile = s.File;
    ParseLineNr = s.LineNr;
}

// ----------------------------------------------------------------------------

/// @}

// ----------------------------------------------------------------------------

/**
**	Read configuration from file stream.
**
**	@param ni	number of import contants
**	@param import	import constants
**	@param file	configuration file stream
**
**	@todo constants and predefined values must be imported
*/
Config *ConfigRead(int ni, const ConfigImport * import, FILE * file)
{
    int i;

    ConfigStrings = StringPoolNew();

    ParseFile = file;
    ParseLineNr = 1;

    ParseStackSize = 16;
    ParseStack = malloc(ParseStackSize * sizeof(*ParseStack));
    ParseSP = 0;

    ParseCurrentArray = ArrayNew();
    //
    //	export constants
    //
    for (i = 0; i < ni; ++i) {
	ArrayIns(&ParseCurrentArray, (size_t) ConfigNewWord(import[i].Index),
	    (size_t) ConfigNewWord(import[i].Value));
    }

    ParseGlobalArray = ParseCurrentArray;
    ParseCurrentIndex = 0;

    if (yyparse()) {
#ifdef DEBUG_CORE_RC
	printf("success\n");
#endif
    } else {
	yyerror("syntax error");
    }

#ifdef never_DEBUG_CORE_RC
    if (0) {
	printf("Strings:\n");
	StringPoolDump(ConfigStrings, 0);
    }
    if (0) {
	printf("Final %d ConfigArray:\n", ParseCurrentIndex);
	ParseDump(ParseCurrentArray, 0);
    }
#endif

    yybuflen = 0;
    free(yybuf);
    free(yytext);
    free(yythunks);
    //free(yyvals);

    free(ParseStack);

    return (Config *) ConfigNewArray(ParseCurrentArray);
}

/**
**	Read configuration from file.
**
**	@param ni	number of import contants
**	@param import	import constants
**	@param filename	configuration file name, use "-" for stdin.
*/
Config *ConfigReadFile(int ni, const ConfigImport * import,
    const char *filename)
{
    FILE *file;
    Config *config;

    // open configuration file
    if (filename && strcmp(filename, "-")) {
	file = fopen(filename, "rb");
    } else {
	file = stdin;
    }
    if (!file) {
	fprintf(stderr, "can't open configuration file '%s'\n", filename);
	return NULL;
    }
    // read configuration file
    ParseName = filename;
    config = ConfigRead(ni, import, file);

    // close config file
    if (file != stdin) {
	fclose(file);
    }
    return config;
}

/**
**	Free all objects in an array.
**
**	@param object	config array to free
**
**	@warning loops aren't detected and not supported!
**
**	@todo FIXME: free multiple
*/
static void ConfigArrayFree(ConfigObject * object)
{
    Array *array;
    size_t *value;
    size_t index;

    array = ConfigArray(object);
    index = 0;
    value = ArrayFirst(array, &index);
    while (value) {
	if (ConfigIsArray((const ConfigObject *)*value)) {
	    //printf("free: %0zx", *value); 
	    //ConfigPrint((const ConfigObject*)*value, 1, stdout);
	    //printf("\n"); 
	    ConfigArrayFree((ConfigObject *) * value);
	}
	if (ConfigIsArray((const ConfigObject *)index)) {
	    //printf("free: %0zx", index); 
	    //ConfigPrint((const ConfigObject*)index, 1, stdout);
	    //printf("\n"); 
	    ConfigArrayFree((ConfigObject *) index);
	}
	value = ArrayNext(array, &index);
    }

    ArrayFree(array);
    ConfigObjectDel(object);
}

#ifdef USE_CORE_RC_WRITE

/**
**	Write configuration to file stream.
**
**	@param config	config dictionary
**	@param out	output stream
**
**	@returns false if no failures, true otherwise.
**
**	@todo not correct, must handle top level special.
*/
int ConfigWrite(const Config * config, FILE * stream)
{
    ConfigPrint(ConfigDict(config), 0, stream);
    fprintf(stream, "\n");

    return 0;
}

/**
**	Write configuration to file name.
**
**	@param config	config dictionary
**	@param filename	output filename
**
**	@returns false if no failures, true otherwise.
*/
int ConfigWriteFile(const Config * config, const char *filename)
{
    FILE *file;
    int err;

    // open configuration file
    if (filename && strcmp(filename, "-")) {
	file = fopen(filename, "wb");
    } else {
	file = stdout;
    }
    if (!file) {
	fprintf(stderr, "can't open configuration file '%s'\n", filename);
	return -1;
    }
    // write configuration file
    err = ConfigWrite(config, file);

    // close configuration file
    if (file != stdout) {
	fclose(file);
    }
    return err;
}
#endif

/**
**	Release all memory used by config module.
**
**	@todo only one config currently supported.
*/
void ConfigFreeMem(Config * config)
{
    ConfigObject *dict;

    // FIXME: ConfigDictNoConst(config)
    dict = (ConfigObject *) (config);
    if (!dict || !ConfigIsArray(dict)) {
	fprintf(stderr, "no config array\n");
	return;
    }
#ifdef DEBUG_CORE_RC
    ConfigPrint(dict, 0, stdout);
#endif
    ConfigArrayFree(dict);

    StringPoolDel(ConfigStrings);
}

#ifdef CORE_RC_TEST			// {

#include <getopt.h>

static int Debug;			/// show additional debug informations

/**
**	Print version.
*/
static void PrintVersion(void)
{
    printf("rc_test: core-rc tester Version " VERSION
#ifdef GIT_REV
	"(GIT-" GIT_REV ")"
#endif
	",\n\t(c) 2009, 2010 by Lutz Sammer\n"
	"\tLicense AGPLv3: GNU Affero General Public License version 3\n");
}

/**
**	Print usage.
*/
static void PrintUsage(void)
{
    printf("Usage: rc_test [-?dhv] [-c file]\n"
	"\t-d\tenable debug, more -d increase the verbosity\n"
	"\t-c file\tconfig file\n" "\t-? -h\tdisplay this message\n"
	"\t-v\tdisplay version information\n"
	"Only idiots print usage on stderr!\n");
}

/**
**	Main entry point.
**
**	@param argc	number of arguments
**	@param argv	arguments vector
**
**	@returns -1 on failures, 0 clean exit.
*/
int main(int argc, char *const argv[])
{
    char *file;
    Config *config;

    Debug = 0;
    file = NULL;

    //
    //	Parse command line arguments
    //
    for (;;) {
	switch (getopt(argc, argv, "hv?-c:d")) {
	    case 'c':			// config file
		file = optarg;
		continue;
	    case 'd':			// enabled debug
		++Debug;
		continue;

	    case EOF:
		break;
	    case 'v':			// print version
		PrintVersion();
		return 0;
	    case '?':
	    case 'h':			// help usage
		PrintVersion();
		PrintUsage();
		return 0;
	    case '-':
		PrintVersion();
		PrintUsage();
		fprintf(stderr, "\nWe need no long options\n");
		return -1;
	    case ':':
		PrintVersion();
		fprintf(stderr, "Missing argument for option '%c'\n", optopt);
		return -1;
	    default:
		PrintVersion();
		fprintf(stderr, "Unkown option '%c'\n", optopt);
		return -1;
	}
	break;
    }
    if (optind < argc) {
	PrintVersion();
	while (optind < argc) {
	    fprintf(stderr, "Unhandled argument '%s'\n", argv[optind++]);
	}
	return -1;
    }
    //
    //	  main loop
    //

    if (file) {
	//
	//	load and parse the config file
	//
	config = ConfigReadFile(0, NULL, file);
	//
	//	returns NULL, if failures
	//
	if (!config) {
	    fprintf(stderr, "parsing error in file `%s`\n", file);
	    return -1;
	}
	//
	//	everything ok
	//

	//
	//	print the parsed configuration
	//
	ConfigWriteFile(config, "-");
	// StringPoolDump(ConfigStrings, 0);

	//
	//	free memory used by configuration
	//
	ConfigFreeMem(config);
    }

    return 0;
}

#endif // } CORE_RC_TEST
/// @}

///
///	@file core-rc.h		@brief core runtime configuration functions
///
///	Copyright (c) 2009, 2011, 2021 by Lutz Sammer.	All Rights Reserved.
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

/// @addtogroup CoreRc
/// @{

//////////////////////////////////////////////////////////////////////////////
//	Declares
//////////////////////////////////////////////////////////////////////////////

/**
**	Configuration main dictionary typedef.
*/
typedef union _config_ Config;

/**
**	Configuration main dictionary structure.
*/
union _config_
{
    void *Pointer;			///< pointer to array
};

/**
**	Config object.
*/
typedef union _config_object_
{
    void *Pointer;			///< pointer to data
} ConfigObject;

/**
**	Config constant import.
**
**	A table of index/value pairs to import into configuration.
**
**	@todo passing string isn't the best
*/
typedef struct _config_import_
{
    const char *Index;			///< index of import constant
    const char *Value;			///< value of import constant
} ConfigImport;

/**
**	Config string intern object.
*/
typedef struct _config_intern_object_
{
    const char *String;			///< string to intern
    ConfigObject *Object;		///< object of the interned string
} ConfigInternObject;

//////////////////////////////////////////////////////////////////////////////
//	Inlines
//////////////////////////////////////////////////////////////////////////////

/**
**	Get dictionary array of configuration.
**
**	This function is currently a cast only.
**
**	@param config	configuration loaded
**
**	@returns config object containing the config dictionary.
*/
static inline const ConfigObject *ConfigDict(const Config * config)
{
    return (const ConfigObject *)config;
}

/**
**	Create a new fixed integer object.
**
**	@param integer	31/63 bit used signed integer
**
**	@returns config object containing the integer value.
*/
static inline ConfigObject *ConfigNewInteger(ssize_t integer)
{
    return (ConfigObject *) ((integer << 1) + 1);
}

//////////////////////////////////////////////////////////////////////////////
//	Prototypes
//////////////////////////////////////////////////////////////////////////////

    /// Create a new floating-point number object.
extern ConfigObject *ConfigNewDouble(double);

    /// Create a new array object.
extern ConfigObject *ConfigNewArray(const Array *);

    /// Create a new string object.
extern ConfigObject *ConfigNewString(const char *);

    /// Check integer value.
extern int ConfigCheckInteger(const ConfigObject *, ssize_t *);

    /// Check unsigned value.
extern int ConfigCheckUnsigned(const ConfigObject *, size_t *);

    /// Check boolean value.
extern int ConfigCheckBoolean(const ConfigObject *);

    /// Check double value.
extern int ConfigCheckDouble(const ConfigObject *, double *);

    /// Check string value.
extern int ConfigCheckString(const ConfigObject *, const char **);

    /// Check array value.
extern int ConfigCheckArray(const ConfigObject *, const ConfigObject **);

#ifdef USE_CORE_RC_GET_STRINGS

    /// Get object value from config.
extern int ConfigStringsGetObject(const ConfigObject *, const ConfigObject **,
    ...);

    /// Get integer value from config.
extern int ConfigStringsGetInteger(const ConfigObject *, ssize_t *, ...);

    /// Get unsigned value from config.
extern int ConfigStringsGetUnsigned(const ConfigObject *, size_t *, ...);

    /// Get boolean value from config.
extern int ConfigStringsGetBoolean(const ConfigObject *, ...);

    /// Get double value from config.
extern int ConfigStringsGetDouble(const ConfigObject *, double *, ...);

    /// Get string value from config.
extern int ConfigStringsGetString(const ConfigObject *, const char **, ...);

    /// Get array value from config.
extern int ConfigStringsGetArray(const ConfigObject *, const ConfigObject **,
    ...);

#endif // USE_CORE_RC_GET_STRINGS

    /// Get object value from config.
extern int ConfigGetObject(const ConfigObject *, const ConfigObject **, ...);

    /// Get integer value from config.
extern int ConfigGetInteger(const ConfigObject *, ssize_t *, ...);

    /// Get unsigned value from config.
extern int ConfigGetUnsigned(const ConfigObject *, size_t *, ...);

    /// Get boolean value from config.
extern int ConfigGetBoolean(const ConfigObject *, ...);

    /// Get double value from config.
extern int ConfigGetDouble(const ConfigObject *, double *, ...);

    /// Get string value from config.
extern int ConfigGetString(const ConfigObject *, const char **, ...);

    /// Get array value from config.
extern int ConfigGetArray(const ConfigObject *, const ConfigObject **, ...);

    /// Get first value from config array.
extern const ConfigObject *ConfigArrayFirst(const ConfigObject *,
    const ConfigObject **);
    /// Get next value from config array.
extern const ConfigObject *ConfigArrayNext(const ConfigObject *,
    const ConfigObject **);

    /// Get first value with integer key from config array.
extern const ConfigObject *ConfigArrayFirstFixedKey(const ConfigObject *,
    const ConfigObject **);
    /// Get next value with integer key from config array.
extern const ConfigObject *ConfigArrayNextFixedKey(const ConfigObject *,
    const ConfigObject **);

    /// Print config object.
extern void ConfigPrint(const ConfigObject *, int, FILE *);

    /// Read configuration from file stream.
extern Config *ConfigRead(int, const ConfigImport *, FILE *);

    /// Read configuration from file name.
extern Config *ConfigReadFile(int, const ConfigImport *, const char *);

    /// Write configuration to file stream.
extern int ConfigWrite(const Config *, FILE *);

    /// Write configuration to file name.
extern int ConfigWriteFile(const Config *, const char *);

    /// Release memory used by config.
extern void ConfigFreeMem(Config *);

/// @}

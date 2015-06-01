#ifndef PLUGININFO_LOCALFS_VSS_20101126
#define PLUGININFO_LOCALFS_VSS_20101126

#include "PluginFSInterfaces.h"

//! \class PluginInfo
//! Implements interfaces of IPluginInfo to provide information about plugin to caller
class PluginInfo : public IPluginInfo
{
public:

	wchar_t* GetPluginVersion() const;
	wchar_t* GetPluginName() const;
	unsigned int GetFileSystemType() const;
	bool IsSeekFileSupported() const;
	bool IsMoveFileSupported() const;
	bool IsConfigurable() const;
};

#endif // PLUGININFO_LOCALFS_VSS_20101126

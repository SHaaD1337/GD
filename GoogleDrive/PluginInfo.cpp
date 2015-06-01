#include "PluginInfo.h"

//! Get plug-in version
wchar_t* PluginInfo::GetPluginVersion() const
{
	return L"1.0";
}

//! Get plug-in name. It uses in paths and listings
wchar_t* PluginInfo::GetPluginName() const
{
	return L"GoogleDrive";
}

//! Get plug-in file system type (source, destination or both)
unsigned int PluginInfo::GetFileSystemType() const
{
	return FileSystemType::Source | FileSystemType::Destination;
}

bool PluginInfo::IsSeekFileSupported() const
{
	return false;
}

bool PluginInfo::IsMoveFileSupported() const
{
	return false;
}

bool PluginInfo::IsConfigurable() const
{
	return true;
}

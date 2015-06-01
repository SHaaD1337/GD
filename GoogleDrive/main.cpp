//! \file main.cpp
//! Contains the plug-in export functions implementation
#include "PluginInfo.h"
#include "PluginImplementation.h"

extern "C"
{
	//! Return interface object with methods for getting plug-in info
	__declspec(dllexport) const IPluginInfo* GetPluginInfo()
	{
		return new PluginInfo;
	}

	//! Return plug-in write interface
	__declspec(dllexport) IBackupDataWrite* GetBackupDataWrite(ILog* pLog, ILink* pLink)
	{
		return new PluginImplementation(pLog, pLink);
	}

	//! Return plug-in read interface
	__declspec(dllexport) IBackupDataRead* GetBackupDataRead(ILog* pLog, ILink* pLink)
	{
		return new PluginImplementation(pLog, pLink);
	}
}


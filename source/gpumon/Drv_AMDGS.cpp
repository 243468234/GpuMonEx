// GPUMonEx.Driver.AMD.cpp : Defines the exported functions for the DLL application.
//

#include "..\platform.h"
#include "..\debug.h"
#include "Drv_AMDGS.h"


/* Logging file */
//std::ofstream logfi;


int AMDGS_Initialize()
{
	//_LOG( __FUNCTION__ << "(): " << "AMD driver initialization started...\n" );
	return 0;
}

void AMDGS_Uninitialize()
{
	//_LOG( __FUNCTION__ << "AMD driver uninitialization completed.\n" );
}

int AMDGS_GetGpuDetails( int AdapterNumber, GPUDETAILS* pGpuDetails )
{
	//_LOG( __FUNCTION__ << "TODO: Implement...\n" );

	return 0;
}

int AMDGS_GetOverallGpuLoad( int AdapterNumber, GPUSTATISTICS* pGpuStatistics )
{
	return 0;
}

int AMDGS_GetProcessGpuLoad( int AdapterNumber, void* pProcess )
{
	return 0;
}

int AMDGS_GetGpuTemperature( int AdapterNumber )
{
	return 0;
}

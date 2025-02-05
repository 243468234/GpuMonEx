// GPUMonEx.Driver.D3DKMT.cpp : Defines the exported functions for the DLL application.
//

#include "..\platform.h"
#include "Drv_D3DKMT.h"

#undef NTSTATUS
#define NTSTATUS ULONG


#include <Windows.h>
#include <iostream>
#include "d3dkmt.h"
#include <vector>
#include "..\debug.h"
#include <d3d11_2.h>
#include <comip.h>
#include <comdef.h>
#include <string.h>
#include <wbemidl.h>
#include <map>
//#include <ntdef.h>
#pragma comment(lib, "wbemuuid.lib")


#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

#define UpdateDelta( DltMgr, NewValue ) 	\
	((DltMgr)->Delta = (NewValue) - (DltMgr)->Value, \
    (DltMgr)->Value = (NewValue), (DltMgr)->Delta)


struct UINT64_DELTA
{
	ULONG64 Value;
	ULONG64 Delta;
};

typedef _com_ptr_t<_com_IIID<ID3D11Device, &IID_ID3D11Device>> CD3D11Device;
typedef _com_ptr_t<_com_IIID<IDXGIDevice, &IID_IDXGIDevice>> CDXGIDevice;
typedef _com_ptr_t<_com_IIID<IDXGIAdapter, &IID_IDXGIAdapter>> CDXGIAdapter;
typedef _com_ptr_t<_com_IIID<IDXGIAdapter1, &IID_IDXGIAdapter1>> CDXGIAdapter1;
typedef _com_ptr_t<_com_IIID<IDXGIAdapter2, &IID_IDXGIAdapter2>> CDXGIAdapter2;
typedef _com_ptr_t<_com_IIID<ID3D11DeviceContext, &IID_ID3D11DeviceContext>> CD3D11DeviceContext;
typedef _com_ptr_t<_com_IIID<ID3D11Device2, &IID_ID3D11Device2>> CD3D11Device2;
typedef _com_ptr_t<_com_IIID<IDXGIAdapter2, &IID_IDXGIAdapter2>> CDXGIAdapter2;
typedef _com_ptr_t<_com_IIID<IDXGIFactory, &IID_IDXGIFactory>> CDXGIFactory;
typedef _com_ptr_t<_com_IIID<IDXGIFactory1, &IID_IDXGIFactory1>> CDXGIFactory1;
typedef _com_ptr_t<_com_IIID<IDXGIFactory2, &IID_IDXGIFactory2>> CDXGIFactory2;


PFND3DKMT_QUERYSTATISTICS	pfnD3DKMTQueryStatistics = nullptr;
PFND3DKMT_OPENADAPTERFROMLUID pfnD3DKMTOpenAdapterFromLuid = nullptr;
PFND3DKMT_QUERYADAPTERINFO  pfnD3DKMTQueryAdapterInfo = nullptr;
PFND3DKMT_CLOSEADAPTER  pfnD3DKMTCloseAdapter = nullptr;


HMODULE						hGdi = nullptr;
//DXGI_ADAPTER_DESC2			DxgiDesc;
UINT						AdapterNumber = 0;
std::vector<float>			GpuUsage;
UINT64_DELTA				ClockTotalRunningTimeDelta = {0};
LARGE_INTEGER				ClockTotalRunningTimeFrequency = {0};
UINT64_DELTA				GpuTotalRunningTimeDelta = {0};
UINT64_DELTA				GpuSystemRunningTimeDelta = {0};
UINT64_DELTA				GpuNodesTotalRunningTimeDelta[16];


/*
 * Name: KmtGetAdapter
 * Desc: Returns the adapter description for the specified display adapter index.
 *		 Relies on DXGI to get the information needed.
 */
HRESULT KmtGetAdapter( UINT Index, DXGI_ADAPTER_DESC2* Desc )
{
	CDXGIFactory2 DXGIFactory;
	CDXGIAdapter1 Adapter1;
	CDXGIAdapter2 Adapter2;

	HRESULT hr = CreateDXGIFactory2( 0, __uuidof( CDXGIFactory2 ), (void**) &DXGIFactory );
	if( FAILED( hr ) )
		return hr;

	
	UINT iAdapterNum = 0;
	CDXGIAdapter1 Adapter3;
    while (DXGIFactory->EnumAdapters1(iAdapterNum, &Adapter1) != DXGI_ERROR_NOT_FOUND)
    {     
		hr = Adapter1->QueryInterface( __uuidof( Adapter1 ), (void**) &Adapter2 );
		Adapter2->GetDesc2( Desc );
		wprintf(L"des:%ls\n", Desc->Description);
        ++iAdapterNum;
    }


	hr = DXGIFactory->EnumAdapters1( Index, &Adapter1 );
	if( FAILED( hr ) )
		return hr;

	hr = Adapter1->QueryInterface( __uuidof( Adapter1 ), (void**) &Adapter2 );

	return Adapter2->GetDesc2( Desc );
}


/*
 * Name: KmtGetNodeCount
 * Desc: Returns the number of GPU nodes associated with this GPU.
 */
UINT KmtGetNodeCount( int adapter )
{
	DXGI_ADAPTER_DESC2 desc;

	if( FAILED( KmtGetAdapter( adapter, &desc ) ) )
		return 0;

	D3DKMT_QUERYSTATISTICS QueryStatistics;
	memset( &QueryStatistics, 0, sizeof( D3DKMT_QUERYSTATISTICS ) );
	QueryStatistics.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
	QueryStatistics.AdapterLuid = desc.AdapterLuid;

	NTSTATUS status = pfnD3DKMTQueryStatistics( &QueryStatistics );
	if( !NT_SUCCESS( status ) )
		return 0;

	return QueryStatistics.QueryResult.AdapterInformation.NodeCount;
}


/*
 * Name: KmtDetectGpu
 * Desc: Uses Direct3D KMT to detect an Intel GPU for Windows devices.  If the Intel
 *		 GPU hardware is not found, then this function returns a failure.  Once the
 *		 expected GPU is found, we open up GDI32.dll and get a function pointer to the
 *		 function required to monitor GPU specific stats.
 */
BOOL KmtDetectGpu()
{
#if 0
	CD3D11Device Device;
	CD3D11Device2 Device2;
	CDXGIAdapter Adapter;
	CDXGIAdapter2 Adapter2;
	CD3D11DeviceContext DeviceContext;
	CDXGIDevice DXGIDevice;

	/* In order to get the LUID of the display adapter, create a Direct3D11 device and get
	   a copy of the descriptor which contains the information we need. */

	HRESULT hr = D3D11CreateDevice( nullptr, D3D_DRIVER_TYPE_HARDWARE,
		nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, &Device, nullptr, &DeviceContext );

	hr = Device->QueryInterface( __uuidof(ID3D11Device2), (void**) &Device2 );
	hr = Device->QueryInterface( __uuidof(IDXGIDevice), (void**) &DXGIDevice );
	hr = DXGIDevice->GetAdapter( &Adapter );
	hr = Adapter->QueryInterface( __uuidof(IDXGIAdapter2), (void**) &Adapter2 );

	Adapter2->GetDesc2( &DxgiDesc );
#endif

	UINT i = 0;

	/* Search for an Intel GPU.  If we don't find one, exit. */

	/*do {
		if( SUCCEEDED( KmtGetAdapter( i, &DxgiDesc ) ) )
		{
			if( wcsstr( DxgiDesc.Description, L"Intel(R)" ) != NULL )
				break;
		}
		else 
		{
			_ERROR( "Intel GPU not found!" << std::endl );
			return FALSE;
		}

		i++;
	} while(true);*/

	/* Load the gdi32.dll directly, and get a function pointer to the necessary function. */

	hGdi = ::LoadLibrary( TEXT( "gdi32.dll" ) );
	if( !hGdi )
	{
		_ERROR( "Unable to open gdi32.dll!" << std::endl );
		return FALSE;
	}

	pfnD3DKMTQueryStatistics = (PFND3DKMT_QUERYSTATISTICS) GetProcAddress( hGdi, "D3DKMTQueryStatistics" );
	if( !pfnD3DKMTQueryStatistics )
	{
		_ERROR( "Unable to locate D3DKMTQueryStatistics()!" << std::endl );
		return FALSE;
	}

	pfnD3DKMTOpenAdapterFromLuid = (PFND3DKMT_OPENADAPTERFROMLUID) GetProcAddress( hGdi, "D3DKMTOpenAdapterFromLuid" );
	if( !pfnD3DKMTOpenAdapterFromLuid )
	{
		_ERROR( "Unable to locate D3DKMTOpenAdapterFromLuid()!" << std::endl );
		return FALSE;
	}

	pfnD3DKMTQueryAdapterInfo = (PFND3DKMT_QUERYADAPTERINFO) GetProcAddress( hGdi, "D3DKMTQueryAdapterInfo" );
	if( !pfnD3DKMTQueryAdapterInfo )
	{
		_ERROR( "Unable to locate pfnD3DKMTQueryAdapterInfo()!" << std::endl );
		return FALSE;
	}
	
	pfnD3DKMTCloseAdapter = (PFND3DKMT_CLOSEADAPTER) GetProcAddress( hGdi, "D3DKMTCloseAdapter" );
	if( !pfnD3DKMTCloseAdapter )
	{
		_ERROR( "Unable to locate pfnD3DKMTCloseAdapter()!" << std::endl );
		return FALSE;
	}

	//KmtGetAdapter( 0, &DxgiDesc );

	return TRUE;
}

void wchar2strstring(std::string & szDst,WCHAR * wchart)
{
	wchar_t * wtext = wchart;
	DWORD dwNmu = WideCharToMultiByte(CP_OEMCP,NULL,wtext,-1,NULL,0, NULL,FALSE);
	char * psTest;
	psTest = new char[dwNmu];
	WideCharToMultiByte(CP_OEMCP, NULL, wtext, -1, psTest, dwNmu, NULL, FALSE);
	szDst = psTest;
	delete[]psTest;
}

int GetNodeName(LUID luid, int adapterIndex, int nodeCnt, char buffer[][64]){
	D3DKMT_OPENADAPTERFROMLUID OpenAdapterFromLUID;
	ZeroMemory( &OpenAdapterFromLUID, sizeof( OpenAdapterFromLUID ) );
	OpenAdapterFromLUID.AdapterLuid = luid;
	ULONG ret = pfnD3DKMTOpenAdapterFromLuid( &OpenAdapterFromLUID );

	do {
		// version
		int version = 0;
		D3DKMT_QUERYADAPTERINFO info;
		info.hAdapter = OpenAdapterFromLUID.hAdapter;
		info.Type = KMTQAITYPE_DRIVERVERSION;
		info.pPrivateDriverData = &version;
		info.PrivateDriverDataSize = sizeof(version);
		ret = pfnD3DKMTQueryAdapterInfo(&info);
		if (ret < 0){
			break;
		}

		for (int i = 0; i < nodeCnt; ++i){
			D3DKMT_NODEMETADATA *node_data = new D3DKMT_NODEMETADATA;
			memset(node_data, 0, sizeof(D3DKMT_NODEMETADATA));
			int adapter_index = adapterIndex;
			int ordinal_index = i;
			node_data->NodeOrdinalAndAdapterIndex = ((adapter_index << 16) & 0xFFFF0000) + ordinal_index & 0xFFFF;

			info.hAdapter = OpenAdapterFromLUID.hAdapter;
			info.Type = KMTQAITYPE_NODEMETADATA;
			info.pPrivateDriverData = node_data;
			info.PrivateDriverDataSize = sizeof(D3DKMT_NODEMETADATA);
			ret = pfnD3DKMTQueryAdapterInfo(&info);

			std::string friendly_name;
			wchar2strstring(friendly_name, node_data->NodeData.FriendlyName);
			std::map<DXGK_ENGINE_TYPE, std::string> map_type = {
				{DXGK_ENGINE_TYPE_3D, "3D"},
				{DXGK_ENGINE_TYPE_VIDEO_DECODE, "Video Decode"},
				{DXGK_ENGINE_TYPE_VIDEO_ENCODE, "Video Encode"},
				{DXGK_ENGINE_TYPE_VIDEO_PROCESSING, "Video Processing"},
				{DXGK_ENGINE_TYPE_SCENE_ASSEMBLY, "Scene Assembly"},
				{DXGK_ENGINE_TYPE_COPY, "Copy"},
				{DXGK_ENGINE_TYPE_OVERLAY, "Overlay"},
				{DXGK_ENGINE_TYPE_OTHER, friendly_name}
			};

			std::string str_engine_name = "Unknown";
			if (map_type.find(node_data->NodeData.EngineType) != map_type.end()){
				if (!map_type.at(node_data->NodeData.EngineType).empty()){
					str_engine_name = map_type.at(node_data->NodeData.EngineType);
				}
			}
			memcpy(buffer[i], str_engine_name.c_str(), str_engine_name.length() + 1);
		}
	
	}while(0);
	
	D3DKMT_CLOSEADAPTER close_adapter;
	close_adapter.hAdapter = OpenAdapterFromLUID.hAdapter;
	pfnD3DKMTCloseAdapter(&close_adapter);
	return ret;
}


/*
 * Name: KmtGetGpuUsage
 * Desc: Returns the GPU usage in the form of a percentage.  
 */
int  KmtGetGpuUsage( int adapter )
{
	UINT64 SharedBytesUsed = 0, DedicatedBytesUsed = 0, CommittedBytesUsed = 0;
	D3DKMT_QUERYSTATISTICS QueryStatistics;
	ULONG i;
	ULONG64 TotalRunningTime = 0, SystemRunningTime = 0;
	UINT NodeCount = KmtGetNodeCount( adapter );
	DOUBLE ElapsedTime;
	DXGI_ADAPTER_DESC2 desc;

	if( FAILED( KmtGetAdapter( adapter, &desc ) ) )
		return -1;
	
	char name[16][64];
	memset(name, 0, sizeof(name));
	GetNodeName(desc.AdapterLuid, adapter, NodeCount, name);

	/* Query the statistics of each node and determine the level of running time for each one. */
	for( i = 0; i < NodeCount; i++ )
	{
		memset( &QueryStatistics, 0, sizeof( D3DKMT_QUERYSTATISTICS ) );
		QueryStatistics.Type = D3DKMT_QUERYSTATISTICS_NODE;
		QueryStatistics.AdapterLuid = desc.AdapterLuid;
		QueryStatistics.QueryNode.NodeId = i;

		NTSTATUS status = pfnD3DKMTQueryStatistics( &QueryStatistics );
		if( NT_SUCCESS( status ) )
		{
			UpdateDelta( &GpuNodesTotalRunningTimeDelta[i], QueryStatistics.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart );
			TotalRunningTime += QueryStatistics.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart;
			SystemRunningTime += QueryStatistics.QueryResult.NodeInformation.SystemInformation.RunningTime.QuadPart;
		}
	}
	
	/* Update timing */

	LARGE_INTEGER PerformanceCounter;

	QueryPerformanceCounter( &PerformanceCounter );
	QueryPerformanceFrequency( &ClockTotalRunningTimeFrequency );
	UpdateDelta( &ClockTotalRunningTimeDelta, PerformanceCounter.QuadPart );
	UpdateDelta( &GpuTotalRunningTimeDelta, TotalRunningTime );
	UpdateDelta( &GpuSystemRunningTimeDelta, SystemRunningTime );

	ElapsedTime = (DOUBLE) ClockTotalRunningTimeDelta.Delta * 10000000 / ClockTotalRunningTimeFrequency.QuadPart;

	float GpuUsage = 0;
	int PositiveNodes = 0;

	/* At the moment, we are only returning the GPU usage for the first node.  I noticed that 
	   with Intel GPUs, the first node is the one that is used primarily, and the other nodes
	   don't seem to get used much, if at all.  In the future, we should probably take them all
	   into account and average the results together, but for now, the first node tends to give
	   rather consistent results so far. */

	//for( i = 0; i < NodeCount; i++ )
	//{
		if( ElapsedTime != 0 )
		{
			GpuUsage = (float) ( GpuTotalRunningTimeDelta.Delta / ElapsedTime );
		}
	//}

	if( GpuUsage > 1.0f )
		GpuUsage = 1.0f;

	return int( GpuUsage * 100.0f );
}


/*
 * Name: KmtGetProcessGpuUsage
 * Desc: Returns the GPU usage of a specific process that is currently running
 *
 * NOTES: See link below.
 *		  https://stackoverflow.com/questions/13017148/how-do-i-get-gpu-usage-per-process
 *
 *		  Windows: *Should* work as long as the process handle was created with PROCESS_QUERY_INFORMATION.
 */
int KmtGetProcessGpuUsage( int adapter, void* pProcess )
{
	LUID luid = { 20 };
	TOKEN_PRIVILEGES privs = { 1, { luid, SE_PRIVILEGE_ENABLED } };
	HANDLE hToken;
	int Usage = -1;
	DXGI_ADAPTER_DESC2 desc;

	/* TODO: Find out exactly what this does, and why it is necessary... */
	if( OpenProcessToken( pProcess, TOKEN_ADJUST_PRIVILEGES, &hToken ) )
	{
		if( AdjustTokenPrivileges( hToken, FALSE, &privs, sizeof( privs ), NULL, NULL ) )
		{
		}
		else
		{
			//__asm int 3;
		}
	}

	/* Probably not necessary */
	/*D3DKMT_OPENADAPTERFROMLUID OpenAdapterFromLUID;
	ZeroMemory( &OpenAdapterFromLUID, sizeof( OpenAdapterFromLUID ) );
	OpenAdapterFromLUID.AdapterLuid = DxgiDesc.AdapterLuid;
	ULONG ret = pfnD3DKMTOpenAdapterFromLuid( &OpenAdapterFromLUID );*/

	KmtGetAdapter( adapter, &desc );

	D3DKMT_QUERYSTATISTICS stats = { D3DKMT_QUERYSTATISTICS_PROCESS, desc.AdapterLuid, pProcess };
	ULONG status = pfnD3DKMTQueryStatistics( &stats );
	if( status == 0 )
		Usage = stats.QueryResult.ProcessInformation.SystemMemory.BytesAllocated;

	return Usage;
}

/*
 * Name: KmtGetGpuTemperature
 * Desc: Returns the temperature of the GPU.
 * NOTE: Since the intel GPU is integrated with the same die as the CPU itself, this will
 *		 probably be the same as CPU temperature itself.
 */
int  KmtGetGpuTemperature()
{
#if 1	/* TODO: Find out why this implementation does not work */
	ULONG Temperature = -1;
    
    HRESULT ci = CoInitialize(NULL); // needs comdef.h
    HRESULT hr = CoInitializeSecurity( NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL );

    if( SUCCEEDED( hr ) )
    {
        IWbemLocator *pLocator; // needs Wbemidl.h & Wbemuuid.lib
        hr = CoCreateInstance( CLSID_WbemAdministrativeLocator, NULL, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*) &pLocator );
        if( SUCCEEDED( hr ) )
        {
            IWbemServices *pServices;
            BSTR ns = SysAllocString( L"root\\WMI" );
            hr = pLocator->ConnectServer( ns, NULL, NULL, NULL, 0, NULL, NULL, &pServices );
            pLocator->Release();
            SysFreeString(ns);
            if( SUCCEEDED( hr ) )
            {
                BSTR query = SysAllocString( L"SELECT * FROM MSAcpi_ThermalZoneTemperature" );
                BSTR wql = SysAllocString( L"WQL" );
                IEnumWbemClassObject *pEnum;
                hr = pServices->ExecQuery( wql, query, WBEM_FLAG_RETURN_IMMEDIATELY | WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum );
                SysFreeString(wql);
                SysFreeString(query);
                pServices->Release();
                if( SUCCEEDED( hr ) )
                {
                    IWbemClassObject *pObject;
                    ULONG returned;
                    hr = pEnum->Next( WBEM_INFINITE, 1, &pObject, &returned );
                    pEnum->Release();
                    if( SUCCEEDED( hr ) )
                    {
                        BSTR temp = SysAllocString( L"CurrentTemperature" );
                        VARIANT v;
                        VariantInit(&v);
                        hr = pObject->Get( temp, 0, &v, NULL, NULL );
                        pObject->Release();
                        SysFreeString(temp);
                        if( SUCCEEDED( hr ) )
                        {
                            Temperature = V_I4(&v);
                        }
                        VariantClear(&v);
                    }
                }
            }
            if( ci == S_OK )
            {
                CoUninitialize();
            }
        }
    }

	return Temperature;
#else
	return 0;
#endif
}


/* Logging file */
//std::ofstream logfi;


int D3DKMT_Initialize()
{
	//_LOG( __FUNCTION__ << "(): " << "D3DKMT driver (for Intel) initialization started...\n" );
	return KmtDetectGpu();
}

void D3DKMT_Uninitialize()
{
	//_LOG( __FUNCTION__ << "D3DKMT driver uninitialization completed.\n" );
	
	FreeLibrary( hGdi );
}

int D3DKMT_GetGpuDetails( int AdapterNumber, GPUDETAILS* pGpuDetails )
{
	//_LOG( __FUNCTION__ << "TODO: Implement...\n" );

	if( !pGpuDetails )
	{
		//_ERROR( "Invalid parameter!" << std::endl );
		return 0;
	}

	DXGI_ADAPTER_DESC2 desc;
	HRESULT hr = KmtGetAdapter( AdapterNumber, &desc );
	if( FAILED( hr ) )
		return 0;

	wcstombs( pGpuDetails->DeviceDesc, desc.Description, 128 );
	pGpuDetails->DeviceID = desc.DeviceId;
	pGpuDetails->VendorID = desc.VendorId;

	return 1;
}

int D3DKMT_GetOverallGpuLoad( int AdapterNumber, GPUSTATISTICS* pGpuStatistics )
{
	memset( pGpuStatistics, -1, sizeof( GPUSTATISTICS ) );

	pGpuStatistics->gpu_usage = KmtGetGpuUsage( AdapterNumber );
	pGpuStatistics->temperature = KmtGetGpuTemperature();

	return 1;
}

int D3DKMT_GetProcessGpuLoad( int AdapterNumber, void* pProcess )
{
	return KmtGetProcessGpuUsage( AdapterNumber, pProcess );
}

int D3DKMT_GetGpuTemperature( int AdapterNumber )
{
	return KmtGetGpuTemperature();
}

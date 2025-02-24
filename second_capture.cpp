/**
Capture screen every second into an AVI.
*/

// Change here
// #define CAPTURE_ADAPTER L"Radeon 550 Series"sv
// DeviceID = 0x000000160d0fbfc8 L"MONITOR\\PHLC07C\\{3eef687d-2cb6-472a-a32f-06434731a2c6}\\0002"
// DeviceID = 0x000000160d0fbfc8 L"MONITOR\\LEN65F4\\{3eef687d-2cb6-472a-a32f-06434731a2c6}\\0001"
#if 0
#define CAPTURE_NVIDIA 1
#define CAPTURE_ADAPTER L"NVIDIA GeForce RTX 3060"sv
// #define CAPTURE_ADAPTER L"AMD Radeon RX 6600"sv
// #define CAPTURE_DISPLAY L"\\\\.\\DISPLAY1"sv
#define CAPTURE_MONITOR L"MONITOR\\LEN65F4\\"sv
#define CAPTURE_NOM 1
#define CAPTURE_DENOM 2 // 1 / 2 fps
#define CAPTURE_FOLDER L"C:\\screencap\\"sv
#define CAPTURE_SUFFIX L"_c"sv
#define CAPTURE_RECORD_MUL 64 // higher is lower q (multiplies framerate as reported to encoder)
#define CAPTURE_PLAYBACK_MUL 10 // (multiplies framerate as reported to AVI file)
#define CAPTURE_HEVC 1
#else
#define CAPTURE_NVIDIA 1
#define CAPTURE_ADAPTER L"NVIDIA GeForce RTX 3060"sv
// #define CAPTURE_ADAPTER L"AMD Radeon RX 6600"sv
// #define CAPTURE_DISPLAY L"\\\\.\\DISPLAY2"sv
#define CAPTURE_MONITOR L"MONITOR\\PHLC07C\\"sv
#define CAPTURE_NOM 1
#define CAPTURE_DENOM 4 // 1 / 4 fps
#define CAPTURE_FOLDER L"C:\\screencap\\"sv
#define CAPTURE_SUFFIX L"_r"sv
#define CAPTURE_RECORD_MUL 256 // higher is lower q
#define CAPTURE_PLAYBACK_MUL 10
#define CAPTURE_HEVC 1
#endif

// Do not change
#define CAPTURE_TIMER ((1000 * CAPTURE_DENOM) / CAPTURE_NOM)

// Include Win32.
// Ensure malloc is included before anything else,
// There are some odd macros that may conflict otherwise.
// Include STL algorithm to ensure std::min and std::max
// are used everywhere, instead of min and max macros.
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#define NOMINMAX
#include <malloc.h>
#include <algorithm>
using std::max;
using std::min;
#include <Windows.h>

// Require C++17
#if defined(_MSC_VER) && (!defined(_HAS_CXX17) || !_HAS_CXX17)
static_assert(false);
#endif

// Define null
constexpr decltype(nullptr) null = nullptr;
#define null null

// Include STL string and allow string literals.
// Always use sv suffix when declaring string literals, it's free.
// Ideally, assign them as `constexpr std::string_view`.
// Use `std::string_view` wherever you'd use `const std::string &`.
#include <string>
#include <string_view>
using namespace std::string_literals;
using namespace std::string_view_literals;

// DirectX includes
#include <DXGI.h>
#include <DXGI1_2.h>
#include <D3D11.h>

// VFW includes
#include <vfw.h>

#if CAPTURE_NVIDIA

// NVIDIA NVENC header
#include "nvEncodeAPI.h"

typedef NVENCSTATUS (NVENCAPI *PFN_NvEncodeAPICreateInstance)(NV_ENCODE_API_FUNCTION_LIST *functionList);

#else

// AMF includes
#include "AMF/core/Factory.h"

#if CAPTURE_HEVC
#include "AMF/components/VideoEncoderHEVC.h"
#else
#include "AMF/components/VideoEncoderVCE.h"
#endif

#endif

// STL includes
#include <iostream>
#include <sstream>
#include <iomanip> 
#include <vector>
#include <mutex>
#include <map>
#include <assert.h>
#include "gsl/gsl_util" // gsl::finally

// Reference:
// https://github.com/diederickh/screen_capture/blob/master/src/test/test_win_api_directx_research.cpp
// https://github.com/google/latency-benchmark/blob/master/src/win/screenscraper.cpp
// https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/amfenc.h
// https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/amfenc.c
// https://mefau.com.br/wp-content/uploads/2018/10/AMF_API_Reference.pdf
// https://github.com/BackupTheBerlios/vbastep-svn/blob/master/tags/VBA_1_5_1/VisualBoyAdvance/src/win32/WriteAVI.cpp
// http://www.myexception.cn/vc-mfc/557377.html
// http://programmersought.com/article/67281418987/;jsessionid=55ABEC2106E3FFDB13D83791BDF2E3C8
// https://social.msdn.microsoft.com/Forums/windowsdesktop/en-US/39b287de-7277-4461-a72f-070c289c2a32/how-to-write-h264-encoded-data-into-avi-container?forum=windowsdirectshowdevelopment
// https://github.com/GPUOpen-LibrariesAndSDKs/AMF/blob/master/amf/public/include/components/VideoEncoderVCE.h
// https://github.com/GPUOpen-LibrariesAndSDKs/AMF/issues/15
// https://stackoverflow.com/questions/46601724/h264-inside-avi-mp4-and-raw-h264-streams-different-format-of-nal-units-or-f/46606524
// http://blog.mediacoderhq.com/h264-profiles-and-levels/
// https://en.wikipedia.org/wiki/High_Efficiency_Video_Coding_tiers_and_levels
// https://github.com/MediaEnhanced/LosslessScreenRecord/blob/main/src/losslessScreenRecord.c
// https://github.com/RMichelsen/Blitstream/blob/master/Blitstream_Encoder/Source/Encoder.cpp

// https://docs.microsoft.com/en-us/windows/win32/debug/retrieving-the-last-error-code
#define CHECK_LAST_ERROR_GOTO(wsvSection, label) do { if (DWORD dwErr = GetLastError()) { printError((wsvSection), dwErr); goto label; } } while (false)
#define CHECK_LAST_ERROR(wsvSection) do { if (DWORD dwErr = GetLastError()) { printError((wsvSection), dwErr); return EXIT_FAILURE; } } while (false)
void printError(std::wstring_view wsvSection, DWORD dwErr)
{
	LPVOID lpMsgBuf;
	FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER
		| FORMAT_MESSAGE_FROM_SYSTEM
		| FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dwErr,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&lpMsgBuf,
		0, NULL);
	auto oldFlags = std::wcout.flags();
	std::wcout << L"ERROR ("sv << wsvSection << L") [DWORD 0x"sv << std::hex << dwErr << L"]: "sv << (LPWSTR)lpMsgBuf << L"\n"sv;
	std::wcout.flags(oldFlags);
	LocalFree(lpMsgBuf);
}

#define CHECK_HRESULT_GOTO(wsvSection, hr, label) do { if (hr != S_OK) { printHResult((wsvSection), hr); goto label; } } while (false)
#define CHECK_HRESULT(wsvSection, hr) do { if (hr != S_OK) { printHResult((wsvSection), hr); return EXIT_FAILURE; } } while (false)
void printHResult(std::wstring_view wsvSection, HRESULT hr)
{
	if (HRESULT_FACILITY(hr) == FACILITY_WINDOWS)
		hr = HRESULT_CODE(hr);

	WCHAR *szErrMsg;
	auto oldFlags = std::wcout.flags();
	std::wcout << L"ERROR ("sv << wsvSection << L") [HRESULT 0x"sv << std::hex << hr;
	if (FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER
		| FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		hr,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&szErrMsg,
		0, NULL))
	{
		std::wcout << L"]: "sv << (LPWSTR)szErrMsg << L"\n"sv;
		LocalFree(szErrMsg);
	}
	else
	{
		std::wcout << L"]\n"sv;
	}
	std::wcout.flags(oldFlags);
}

#if !CAPTURE_NVIDIA
#define CHECK_AMF_RESULT_GOTO(wsvSection, amfRes, label) do { if (amfRes != AMF_OK) { printAmfResult((wsvSection), amfRes); goto label; } } while (false)
#define CHECK_AMF_RESULT(wsvSection, amfRes) do { if (amfRes != AMF_OK) { printAmfResult((wsvSection), amfRes); return EXIT_FAILURE; } } while (false)
void printAmfResult(std::wstring_view wsvSection, AMF_RESULT amfRes)
{
	auto oldFlags = std::wcout.flags();
	std::wcout << L"ERROR ("sv << wsvSection << L") [AMF_RESULT 0x"sv << std::hex << amfRes << L"]\n"sv;
	std::wcout.flags(oldFlags);
}
#endif

// Add NVIDIA error checking
#if CAPTURE_NVIDIA
#define CHECK_NVENC_RESULT_GOTO(wsvSection, res, label) do { if (res != NV_ENC_SUCCESS) { printNvEncResult((wsvSection), res); goto label; } } while (false)
#define CHECK_NVENC_RESULT(wsvSection, res) do { if (res != NV_ENC_SUCCESS) { printNvEncResult((wsvSection), res); return EXIT_FAILURE; } } while (false)
void printNvEncResult(std::wstring_view wsvSection, NVENCSTATUS res)
{
	const char* errName = nullptr;
	switch(res) {
	case NV_ENC_SUCCESS: errName = "Success"; break;
	case NV_ENC_ERR_NO_ENCODE_DEVICE: errName = "No encode capable devices detected"; break;
	case NV_ENC_ERR_UNSUPPORTED_DEVICE: errName = "Device not supported"; break;
	case NV_ENC_ERR_INVALID_ENCODERDEVICE: errName = "Invalid encoder device"; break;
	case NV_ENC_ERR_INVALID_DEVICE: errName = "Invalid device"; break;
	case NV_ENC_ERR_DEVICE_NOT_EXIST: errName = "Device no longer exists"; break;
	case NV_ENC_ERR_INVALID_PTR: errName = "Invalid pointer"; break;
	case NV_ENC_ERR_INVALID_EVENT: errName = "Invalid event"; break;
	case NV_ENC_ERR_INVALID_PARAM: errName = "Invalid parameter"; break;
	case NV_ENC_ERR_INVALID_CALL: errName = "Invalid API call sequence"; break;
	case NV_ENC_ERR_OUT_OF_MEMORY: errName = "Out of memory"; break;
	case NV_ENC_ERR_ENCODER_NOT_INITIALIZED: errName = "Encoder not initialized"; break;
	case NV_ENC_ERR_UNSUPPORTED_PARAM: errName = "Unsupported parameter"; break;
	case NV_ENC_ERR_LOCK_BUSY: errName = "Lock busy"; break;
	case NV_ENC_ERR_NOT_ENOUGH_BUFFER: errName = "Not enough buffer"; break;
	case NV_ENC_ERR_INVALID_VERSION: errName = "Invalid struct version"; break;
	case NV_ENC_ERR_MAP_FAILED: errName = "Map failed"; break;
	case NV_ENC_ERR_NEED_MORE_INPUT: errName = "Need more input"; break;
	case NV_ENC_ERR_ENCODER_BUSY: errName = "Encoder busy"; break;
	case NV_ENC_ERR_EVENT_NOT_REGISTERD: errName = "Event not registered"; break;
	case NV_ENC_ERR_GENERIC: errName = "Generic error"; break;
	case NV_ENC_ERR_INCOMPATIBLE_CLIENT_KEY: errName = "Incompatible client key"; break;
	case NV_ENC_ERR_UNIMPLEMENTED: errName = "Feature not implemented"; break;
	case NV_ENC_ERR_RESOURCE_REGISTER_FAILED: errName = "Resource registration failed"; break;
	case NV_ENC_ERR_RESOURCE_NOT_REGISTERED: errName = "Resource not registered"; break;
	case NV_ENC_ERR_RESOURCE_NOT_MAPPED: errName = "Resource not mapped"; break;
	case NV_ENC_ERR_NEED_MORE_OUTPUT: errName = "Need more output buffers"; break;
	default: errName = "Unknown error";
	}
	std::wcout << L"NVENC ERROR (" << wsvSection << L"): " << errName << L" (0x" << std::hex << res << L")\n";
}
#endif

// Handle terminal exit
// https://docs.microsoft.com/en-us/windows/console/registering-a-control-handler-function
DWORD s_ThreadId;
volatile bool s_Running = true;
std::mutex s_Closing;
BOOL WINAPI ctrlHandler(DWORD ctrlType)
{
	if (ctrlType == CTRL_CLOSE_EVENT) // CTRL_LOGOFF_EVENT, CTRL_SHUTDOWN_EVENT
	{
		s_Running = false;
		PostThreadMessageW(s_ThreadId, WM_APP + 1, 0, 0);
		std::unique_lock<std::mutex> waitClose(s_Closing);
		return TRUE;
	}
	return FALSE;
}

#ifdef NDEBUG
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
#else
int main()
#endif
{
	HRESULT hr;
#if !CAPTURE_NVIDIA
	AMF_RESULT amfRes;
#endif
	std::unique_lock<std::mutex> waitClose(s_Closing);
	s_ThreadId = GetCurrentThreadId();
	static const auto applicationName = L"Second Capture"sv;
	std::wcout << applicationName << L"\n"sv;

	// Handle terminal closing
	SetConsoleCtrlHandler(ctrlHandler, TRUE);

	// Kick off Win32 message loop
	MSG msg;
	PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);
	CHECK_LAST_ERROR(L"PeekMessageW"sv);

	// Load up AVI
	AVIFileInit();
	CHECK_LAST_ERROR(L"AVIFileInit"sv);
	auto exitAviFile = gsl::finally([&]() -> void { AVIFileExit(); });

#if !CAPTURE_NVIDIA
	// Load up AMF library
	HMODULE hAMFDll = LoadLibraryW(AMF_DLL_NAME);
	if (hAMFDll)
		SetLastError(0);
	CHECK_LAST_ERROR(L"LoadLibraryW"sv);
	auto freeLibrary = gsl::finally([&]() -> void { FreeLibrary(hAMFDll); });
	AMFQueryVersion_Fn amfQueryVersion = (AMFQueryVersion_Fn)GetProcAddress(hAMFDll, AMF_QUERY_VERSION_FUNCTION_NAME);
	CHECK_LAST_ERROR(L"GetProcAddress"sv);
	amf_uint64 amfVersion;
	amfRes = amfQueryVersion(&amfVersion);
	CHECK_AMF_RESULT(L"amfQueryVersion"sv, amfRes);
	AMFInit_Fn amfInit;
	amfInit = (AMFInit_Fn)GetProcAddress(hAMFDll, AMF_INIT_FUNCTION_NAME);
	CHECK_LAST_ERROR(L"GetProcAddress"sv);
	amf::AMFFactory *amfFactory;
	amfRes = amfInit(amfVersion, &amfFactory);
	CHECK_AMF_RESULT(L"amfInit"sv, amfRes);
	int oldFlags;
	oldFlags = std::wcout.flags();
	std::wcout << L"AMF version: "sv << std::hex << amfVersion << L"\n"sv;
	std::wcout.flags(oldFlags);
#endif

#if CAPTURE_NVIDIA
	HMODULE hNvEncLib = LoadLibraryW(L"nvEncodeAPI64.dll");
	if (hNvEncLib)
		SetLastError(0);
	CHECK_LAST_ERROR(L"LoadLibraryW"sv);
	auto freeNvEncLib = gsl::finally([&]() -> void { FreeLibrary(hNvEncLib); });

	// Get address of NvEncodeAPICreateInstance function
	PFN_NvEncodeAPICreateInstance nvEncodeAPICreateInstance = 
		(PFN_NvEncodeAPICreateInstance)GetProcAddress(hNvEncLib, "NvEncodeAPICreateInstance");
	CHECK_LAST_ERROR(L"GetProcAddress"sv);

	// NVIDIA NVENC initialization
	NV_ENCODE_API_FUNCTION_LIST nvFuncs = { NV_ENCODE_API_FUNCTION_LIST_VER };
	NVENCSTATUS nvRes = nvEncodeAPICreateInstance(&nvFuncs);
	CHECK_NVENC_RESULT(L"NvEncodeAPICreateInstance", nvRes);
	std::wcout << L"NVENC API version: "sv << nvFuncs.version << L"\n"sv;
#endif

	IDXGIFactory1 *factory;
	factory = NULL;
	hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)(&factory));
	CHECK_HRESULT(L"CreateDXGIFactory1"sv, hr);
	auto releaseFactory = gsl::finally([&]() -> void { factory->Release(); });

	do
	{
		{
#if CAPTURE_NVIDIA
			// NVIDIA-specific Resource Management Variables
			NV_ENC_REGISTERED_PTR registeredResource = nullptr;
			NV_ENC_INPUT_PTR mappedInputBuffer = nullptr;
			NV_ENC_OUTPUT_PTR outputBitstream = nullptr;
			NV_ENC_CREATE_BITSTREAM_BUFFER bitstreamBufferParams = { NV_ENC_CREATE_BITSTREAM_BUFFER_VER };
#else
			// Create AMF context
			amf::AMFContext *amfContext;
			amfRes = amfFactory->CreateContext(&amfContext);
			CHECK_AMF_RESULT_GOTO(L"amfInit"sv, amfRes, Release);
			auto releaseAmfContext = gsl::finally([&]() -> void { amfContext->Release(); });
			std::wcout << L"AMF context ready.\n"sv;
#endif

			// Find friendly display name
			std::wstring captureDisplay;
			{
				DISPLAY_DEVICEW displayDevice = { 0 };
				DISPLAY_DEVICEW monitor = { 0 };
				displayDevice.cb = sizeof(displayDevice);
				monitor.cb = sizeof(displayDevice);
				DWORD deviceIndex = 0;
				while (EnumDisplayDevicesW(NULL, deviceIndex, &displayDevice, 0))
				{
					if (displayDevice.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
					{
						std::wcout << L"Display ["sv << deviceIndex << L"]: "sv << displayDevice.DeviceName << L" = "sv << displayDevice.DeviceString << L"\n"sv;
						if (EnumDisplayDevices(displayDevice.DeviceName, 0, &monitor, 0))
						{
							// std::wcout << L"Monitor friendly name: "sv << monitor.DeviceString << L"\n"sv;
							std::wcout << L"Monitor DeviceID: "sv << monitor.DeviceID << L"\n"sv;
							if (std::wstring_view(monitor.DeviceID, CAPTURE_MONITOR.size()) == CAPTURE_MONITOR)
							{
								std::wcout << L"Found the display!\n";
								captureDisplay = displayDevice.DeviceName;
								goto FoundDisplay;
							}
						}
					}

					deviceIndex++;
				}
			}
			goto Release;
		FoundDisplay:

			// Enumerate adapters
			UINT i = 0;
			IDXGIAdapter1 *adapter = null;
			IDXGIOutput *output = null;
			DXGI_ADAPTER_DESC1 adapterDesc;
			DXGI_OUTPUT_DESC outputDesc;
			auto releaseAdapter = gsl::finally([&]() -> void { if (adapter) adapter->Release(); adapter = null; });
			auto releaseOutput = gsl::finally([&]() -> void { if (output) output->Release(); output = null; });
			while ((hr = factory->EnumAdapters1(i, &adapter)) != DXGI_ERROR_NOT_FOUND)
			{
				CHECK_HRESULT_GOTO(L"EnumAdapters1"sv, hr, Release);

				hr = adapter->GetDesc1(&adapterDesc);
				CHECK_HRESULT_GOTO(L"GetDesc1"sv, hr, Release);

				std::wcout << L"Adapter ["sv << i << L"]: "sv << adapterDesc.Description << L"\n"sv;

				if (adapterDesc.Description == CAPTURE_ADAPTER)
				{
					std::wcout << L"Found the adapter!\n"sv;

					// Enumerate outputs
					UINT j = 0;
					while ((hr = adapter->EnumOutputs(j, &output)) != DXGI_ERROR_NOT_FOUND)
					{
						CHECK_HRESULT_GOTO(L"EnumOutputs"sv, hr, Release);

						hr = output->GetDesc(&outputDesc);
						CHECK_HRESULT_GOTO(L"GetDesc"sv, hr, Release);

						std::wcout << L"Display ["sv << j << L"]: "sv << outputDesc.DeviceName << L"\n"sv;

						if (outputDesc.DeviceName == captureDisplay)
						{
							std::wcout << L"Found the display!\n"sv;
							goto Found;
						}

						output->Release();
						++j;
					}
					output = NULL;
				}

				adapter->Release();
				++i;
			}
			adapter = NULL;
		Found:
			if (!adapter || !output)
			{
				std::wcout << L"Adapter or display not found!\n"sv;
				goto Release;
			}

			// Create D3D interface
			ID3D11Device *device;
			D3D_FEATURE_LEVEL featureLevels[] = {
				D3D_FEATURE_LEVEL_11_1
			};
			D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
			ID3D11DeviceContext *context;
			hr = D3D11CreateDevice(
				adapter, D3D_DRIVER_TYPE_UNKNOWN,
				NULL, /*D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT |*/ D3D11_CREATE_DEVICE_DEBUG,
				featureLevels, 1, D3D11_SDK_VERSION,
				&device, &featureLevel, &context);
			CHECK_HRESULT_GOTO(L"D3D11CreateDevice"sv, hr, Release);
			auto releaseDeviceContext = gsl::finally([&]() -> void { context->Release(); device->Release(); });
			std::wcout << L"D3D11 interface ready.\n"sv;

#if !CAPTURE_NVIDIA
			// Bind AMF
			amfRes = amfContext->InitDX11(device, amf::AMF_DX11_1);
			CHECK_AMF_RESULT_GOTO(L"InitDX11"sv, amfRes, Release);
#endif

			// Create output duplication interface
			IDXGIOutput1 *output1;
			hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void **)&output1);
			CHECK_HRESULT_GOTO(L"QueryInterface<IDXGIOutput1>"sv, hr, Release);
			auto releaseOutput1 = gsl::finally([&]() -> void { output1->Release(); });
			IDXGIOutputDuplication *duplication;
			bool hasFrame = false;
			hr = output1->DuplicateOutput(device, &duplication);
			CHECK_HRESULT_GOTO(L"DuplicateOutput"sv, hr, Release);
			auto releaseDuplication = gsl::finally([&]() -> void { duplication->Release(); });
			DXGI_OUTDUPL_DESC duplicationDesc;
			duplication->GetDesc(&duplicationDesc);
			std::wcout << L"Output duplication interface ready "sv
				<< ((duplicationDesc.DesktopImageInSystemMemory) ? L"in system memory"sv : L"in graphics memory"sv)
				<< L".\n"sv;

			// Screen resolution
			std::wcout << L"DesktopCoordinates: "sv << outputDesc.DesktopCoordinates.left
				<< L","sv << outputDesc.DesktopCoordinates.top
				<< L","sv << outputDesc.DesktopCoordinates.right
				<< L","sv << outputDesc.DesktopCoordinates.bottom << L"\n";
			LONG w = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
			LONG h = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

#if !CAPTURE_NVIDIA
			// AMFVideoEncoder_HEVC or AMFVideoEncoderVCE_AVC
			amf::AMFComponent *amfEncoder;
#if CAPTURE_HEVC
			amfRes = amfFactory->CreateComponent(amfContext, AMFVideoEncoder_HEVC, &amfEncoder);
#else
			amfRes = amfFactory->CreateComponent(amfContext, AMFVideoEncoderVCE_AVC, &amfEncoder);
#endif
			CHECK_AMF_RESULT_GOTO(L"CreateComponent"sv, amfRes, Release);
			auto releaseAmfEncoder = gsl::finally([&]() -> void { amfEncoder->Release(); });

#if CAPTURE_HEVC
			amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING);
			CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
			amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, AMFConstructSize(w, h));
			CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
			amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, AMFConstructRate(CAPTURE_RECORD_MUL * CAPTURE_NOM, CAPTURE_DENOM)); // AMFConstructRate(1, 1));
			CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
			// amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE, AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN);
			// CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
			// amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER_MAIN);
			// CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
			amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_LEVEL_4);
			CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
#else
			amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING);
			CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
			amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY); // AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED); // 
			CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
			amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_HIGH);
			CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
			amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, 40); // 30); // 
			CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
			amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, AMFConstructSize(w, h));
			CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
			amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, AMFConstructRate(CAPTURE_NOM, CAPTURE_DENOM));
			CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
			amfRes = amfEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, 400000); // 100 000 = 6 Mbps if it were 60Hz, so aiming at 20-25 Mbps go near 400 000
			CHECK_AMF_RESULT_GOTO(L"SetProperty"sv, amfRes, Release);
#endif

			amfRes = amfEncoder->Init(amf::AMF_SURFACE_BGRA, w, h);
			CHECK_AMF_RESULT_GOTO(L"Init"sv, amfRes, Release);
#endif

#if CAPTURE_NVIDIA
			// Create NVENC encoder
			void* nvEncoder = nullptr;
			NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS openParams = { NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER };
			openParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
			openParams.device = device; // Use existing D3D11 device
			openParams.apiVersion = NVENCAPI_VERSION;
			nvRes = nvFuncs.nvEncOpenEncodeSessionEx(&openParams, &nvEncoder);
			CHECK_NVENC_RESULT_GOTO(L"nvEncOpenEncodeSessionEx", nvRes, Release);
			auto releaseNvEncoder = gsl::finally([&]() -> void { nvFuncs.nvEncDestroyEncoder(nvEncoder); });

			// Retrieve preset configuration for HEVC
			NV_ENC_PRESET_CONFIG presetConfig = { 0 };
			presetConfig.version = NV_ENC_PRESET_CONFIG_VER;
			presetConfig.presetCfg.version = NV_ENC_CONFIG_VER;
			nvRes = nvFuncs.nvEncGetEncodePresetConfig(nvEncoder, NV_ENC_CODEC_HEVC_GUID, NV_ENC_PRESET_P4_GUID, &presetConfig);
			CHECK_NVENC_RESULT_GOTO(L"nvEncGetEncodePresetConfig", nvRes, Release);

			// Configure NVENC
			NV_ENC_INITIALIZE_PARAMS initParams = { 0 };
			initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
			NV_ENC_CONFIG &encConfig = presetConfig.presetCfg;
			initParams.encodeConfig = &presetConfig.presetCfg;

			// Set codec-specific parameters
			encConfig.profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;
			// encConfig.encodeCodecConfig.hevcConfig.chromaFormatIDC = 1; // 4:2:0
			
			// Override preset settings if needed
			encConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_VBR;
			// encConfig.rcParams.averageBitRate = 1000000;
			// encConfig.rcParams.maxBitRate = 2000000;
			// encConfig.rcParams.vbvBufferSize = encConfig.rcParams.maxBitRate;
			// encConfig.rcParams.vbvInitialDelay = encConfig.rcParams.vbvBufferSize;
			
			// Initialize encoder with updated parameters
			initParams.encodeGUID = NV_ENC_CODEC_HEVC_GUID;
			initParams.encodeWidth = w;
			initParams.encodeHeight = h;
			initParams.frameRateNum = CAPTURE_NOM * CAPTURE_RECORD_MUL;
			initParams.frameRateDen = CAPTURE_DENOM;
			initParams.enablePTD = 1;

			nvRes = nvFuncs.nvEncInitializeEncoder(nvEncoder, &initParams);
			CHECK_NVENC_RESULT_GOTO(L"nvEncInitializeEncoder", nvRes, Release);

			// Create bitstream buffer
			bitstreamBufferParams.size = 4 * 1024 * 1024; // Adjust based on resolution
			bitstreamBufferParams.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;
			nvRes = nvFuncs.nvEncCreateBitstreamBuffer(nvEncoder, &bitstreamBufferParams);
			CHECK_NVENC_RESULT_GOTO(L"nvEncCreateBitstreamBuffer", nvRes, Release);
			outputBitstream = bitstreamBufferParams.bitstreamBuffer;
			auto releaseBitstreamBuffer = gsl::finally([&]() -> void { nvFuncs.nvEncDestroyBitstreamBuffer(nvEncoder, outputBitstream); });
#endif

			// Target texture
			/*
			D3D11_TEXTURE2D_DESC texDesc;
			texDesc.Width = w;
			texDesc.Height = h;
			texDesc.MipLevels = 1;
			texDesc.ArraySize = 1;
			texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.Usage = D3D11_USAGE_STAGING;
			texDesc.BindFlags = 0;
			texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			texDesc.MiscFlags = 0;
			ID3D11Texture2D *texture = NULL;
			hr = device->CreateTexture2D(&texDesc, NULL, &texture);
			CHECK_HRESULT_GOTO(L"CreateTexture2D"sv, hr, ReleaseNoTexture);
			*/

			// Create timer, we want a screen capture every second
			UINT_PTR timerId = SetTimer(NULL, 0, CAPTURE_TIMER, (TIMERPROC)NULL);
			CHECK_LAST_ERROR_GOTO(L"SetTimer"sv, Release);
			auto releaseTimer = gsl::finally([&]() -> void { KillTimer(NULL, timerId); });

			auto releaseFrame = gsl::finally([&]() -> void {
				if (hasFrame)
				{
					hr = duplication->ReleaseFrame();
					hasFrame = false;
					if (hr != S_OK) printHResult(L"ReleaseFrame"sv, hr);
				}
				});

			PAVIFILE aviFile = NULL;
			PAVISTREAM aviStream = NULL;
			LONG frame;

			auto flushAvi = [&]() -> void {
				if (aviFile)
				{
					AVIStreamRelease(aviStream);
					AVIFileRelease(aviFile);
					aviFile = NULL;
				}
			};

			auto pushAvi = [&](void *buffer, size_t sz, bool keyframe) -> int {
				if (!aviFile)
				{
					time_t t = time(NULL);
					tm now = { 0 };
					localtime_s(&now, &t);
					// now.tm_year - 100, now.tm_mon + 1, now.tm_mday
					std::wstringstream ss;
					ss << CAPTURE_FOLDER;
					ss << std::setfill(L'0') << std::setw(4) << (now.tm_year + 1900) << L"-"sv;
					ss << std::setfill(L'0') << std::setw(2) << (now.tm_mon + 1) << L"-"sv;
					ss << std::setfill(L'0') << std::setw(2) << now.tm_mday << L"-"sv;
					ss << std::setfill(L'0') << std::setw(2) << now.tm_hour << L"-"sv;
					ss << std::setfill(L'0') << std::setw(2) << now.tm_min << L"-"sv;
					ss << std::setfill(L'0') << std::setw(2) << now.tm_sec;
					ss << CAPTURE_SUFFIX;
					ss << ".avi";
					hr = AVIFileOpenW(&aviFile, ss.str().c_str(), OF_WRITE | OF_CREATE, NULL);
					CHECK_HRESULT(L"AVIFileOpenW"sv, hr);
					bool ok = false;
					auto releaseFail = gsl::finally([&]() -> void {
						if (!ok)
						{
							if (aviStream)
							{
								AVIStreamRelease(aviStream);
							}
							AVIFileRelease(aviFile);
							aviFile = NULL;
						}
					});

					AVISTREAMINFOW streamInfo = { 0 };
					streamInfo.fccType = streamtypeVIDEO;
#if CAPTURE_HEVC
					// streamInfo.fccHandler = mmioFOURCC('H', '2', '6', '5');
					streamInfo.fccHandler = mmioFOURCC('H', 'E', 'V', 'C');
#else
					streamInfo.fccHandler = mmioFOURCC('H', '2', '6', '4');
#endif
					streamInfo.dwRate = CAPTURE_NOM * CAPTURE_PLAYBACK_MUL;
					streamInfo.dwScale = CAPTURE_DENOM;
					// streamInfo.dwSuggestedBufferSize = 256 * 1024; // (w * h * 4);
					SetRect(&streamInfo.rcFrame, 0, 0, w, h);
					wcscpy_s(streamInfo.szName, L"Second Capture");

					hr = AVIFileCreateStreamW(aviFile, &aviStream, &streamInfo);
					CHECK_HRESULT(L"AVIFileOpenW"sv, hr);
						
					BITMAPINFOHEADER bmpInfoHdr = { 0 };
					bmpInfoHdr.biSize = sizeof(BITMAPINFOHEADER);
					bmpInfoHdr.biWidth = w;
					bmpInfoHdr.biHeight = h;
					bmpInfoHdr.biBitCount = 24;
#if CAPTURE_HEVC
					// bmpInfoHdr.biCompression = mmioFOURCC('H', '2', '6', '5');
					bmpInfoHdr.biCompression = mmioFOURCC('H', 'E', 'V', 'C');
#else
					bmpInfoHdr.biCompression = mmioFOURCC('H', '2', '6', '4');
#endif
					bmpInfoHdr.biPlanes = 1;
					hr = AVIStreamSetFormat(aviStream, 0, &bmpInfoHdr, sizeof(bmpInfoHdr));
					CHECK_HRESULT(L"AVIStreamSetFormat"sv, hr);

					frame = 0;
					ok = true;
				}
				if (aviFile)
				{
					/*
#if CAPTURE_HEVC
					bool keyframe = false; // No need :)
#else
					char frametype = sz >= 4 ? ((char *)buffer)[4] : 0;
					if (frametype == 9) frametype = sz >= 10 ? (((char *)buffer)[10] & 0x0F) : 0;
					else frametype &= 0x0F;
					bool keyframe = frametype == 0x05;
					if (keyframe) std::wcout << L"Keyframe!\n"sv;
#endif
					*/
					hr = AVIStreamWrite(aviStream,
						frame, 1,
						buffer, (LONG)sz,
						(keyframe ? AVIIF_KEYFRAME : 0),
						NULL, NULL);
					CHECK_HRESULT_GOTO(L"AVIStreamWrite"sv, hr, Fail);
					++frame;
					goto Succeed;
				Fail:
					flushAvi();
				Succeed:
					;
				}
				return EXIT_SUCCESS;
			};

#if !CAPTURE_NVIDIA
			auto fetchEncodedFrames = [&](bool drain) -> int {
				// Fetch encoded frames
				if (drain)
				{
					amfRes = amfEncoder->Drain();
					CHECK_AMF_RESULT(L"Drain"sv, amfRes);
				}
				amf::AMFData *amfData = null;
				amfRes = amfEncoder->QueryOutput(&amfData);
				while (amfRes != AMF_EOF && amfRes != AMF_INVALID_ARG) // AMF_INVALID_ARG is not documented?! Changed since 2022 JAN 19
				{
					// Wait
					if (amfRes == AMF_REPEAT)
					{
						if (drain)
						{
							Sleep(10);
							amfRes = amfEncoder->QueryOutput(&amfData);
							continue;
						}
						else
						{
							break;
						}
					}

					// Fetch
					{
						CHECK_AMF_RESULT(L"QueryOutput"sv, amfRes);
						auto releaseData = gsl::finally([&]() { amfData->Release(); });

						assert(amfData->GetDataType() == amf::AMF_DATA_BUFFER);

						amf::AMFBuffer *amfBuffer;
						amfRes = amfData->QueryInterface(amf::AMFBuffer::IID(), (void **)&amfBuffer);
						CHECK_AMF_RESULT(L"QueryInterface"sv, amfRes);
						auto releaseBuffer = gsl::finally([&]() { amfBuffer->Release(); });

						size_t sz = amfBuffer->GetSize();
						void *buffer = amfBuffer->GetNative();
#if CAPTURE_HEVC
						int dataType;
						amfRes = amfBuffer->GetProperty(AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE, &dataType);
						CHECK_AMF_RESULT(L"GetProperty"sv, amfRes);
						bool keyframe = dataType == AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_IDR;
#else
						int dataType;
						amfRes = amfBuffer->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &dataType);
						CHECK_AMF_RESULT(L"GetProperty"sv, amfRes);
						bool keyframe = dataType == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR;
#endif

						std::wcout << L"Encoded"sv << (keyframe ? L" keyframe"sv : L""sv) << L" ("sv << sz << L" bytes)!\n"sv;

						/*
						FILE *f = fopen("C:\\temp\\second_capture.h264", "wb");
						size_t w = fwrite(buffer, 1, sz, f);
						fclose(f);
						*/

						pushAvi(buffer, sz, keyframe);
					}

					// Next
					amfRes = amfEncoder->QueryOutput(&amfData);
				}
				return EXIT_SUCCESS;
			};
#endif

			// Loop
			DXGI_OUTDUPL_FRAME_INFO frameInfo = { 0 };
			while (BOOL bRet = GetMessageW(&msg, NULL, 0, 0))
			{
				if (bRet == -1)
				{
					std::wcout << L"Main loop error!"sv;
					goto DrainRelease;
				}
				else
				{
					TranslateMessage(&msg);
					if (!msg.hwnd)
					{
						switch (msg.message)
						{
						case WM_TIMER:
							if (msg.wParam == timerId)
							{
								// std::wcout << L"Timer ticks...\n"sv;
								// Fetch frame
								{
									// Release previous frame
									if (hasFrame)
									{
										hr = duplication->ReleaseFrame();
										hasFrame = false;
										CHECK_HRESULT_GOTO(L"ReleaseFrame"sv, hr, DrainRelease);
									}

									// Capture current frame
									IDXGIResource *desktopResource;
									hr = duplication->AcquireNextFrame(1000, &frameInfo, &desktopResource);
									CHECK_HRESULT_GOTO(L"AcquireNextFrame"sv, hr, DrainRelease);
									hasFrame = true;
									auto releaseDesktopResource = gsl::finally([&]() -> void { desktopResource->Release(); });

									// Grab as texture
									ID3D11Texture2D *frameBuffer;
									hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&frameBuffer);
									CHECK_HRESULT_GOTO(L"QueryInterface<ID3D11Texture2D>"sv, hr, DrainRelease);
									auto releaseFrameBuffer = gsl::finally([&]() -> void { frameBuffer->Release(); });
									D3D11_TEXTURE2D_DESC frameBufferDesc;
									frameBuffer->GetDesc(&frameBufferDesc);
									if (frameBufferDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
									{
										std::wcout << L"Format is not DXGI_FORMAT_B8G8R8A8_UNORM!\n"sv;
										goto DrainRelease;
									}

									std::wcout << L"Captured!\n";

#if CAPTURE_NVIDIA
									// Register the DX11 texture
									NV_ENC_REGISTER_RESOURCE registerResParams = { NV_ENC_REGISTER_RESOURCE_VER };
									registerResParams.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
									registerResParams.width = w;
									registerResParams.height = h;
									registerResParams.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;
									registerResParams.resourceToRegister = frameBuffer;
									nvRes = nvFuncs.nvEncRegisterResource(nvEncoder, &registerResParams);
									CHECK_NVENC_RESULT_GOTO(L"nvEncRegisterResource", nvRes, DrainRelease);
									registeredResource = registerResParams.registeredResource;
									auto releaseRegisteredResource = gsl::finally([&]() -> void { nvFuncs.nvEncUnregisterResource(nvEncoder, registeredResource); });

									// Map the resource
									NV_ENC_MAP_INPUT_RESOURCE mapInputResParams = { NV_ENC_MAP_INPUT_RESOURCE_VER };
									mapInputResParams.registeredResource = registeredResource;
									nvRes = nvFuncs.nvEncMapInputResource(nvEncoder, &mapInputResParams);
									CHECK_NVENC_RESULT_GOTO(L"nvEncMapInputResource", nvRes, DrainRelease);
									mappedInputBuffer = mapInputResParams.mappedResource;
									auto releaseMappedInputBuffer = gsl::finally([&]() -> void { nvFuncs.nvEncUnmapInputResource(nvEncoder, mappedInputBuffer); });

									// Submit frame to NVENC
									NV_ENC_PIC_PARAMS picParams = { NV_ENC_PIC_PARAMS_VER };
									picParams.inputWidth = w;
									picParams.inputHeight = h;
									picParams.inputPitch = picParams.inputWidth;
									picParams.inputBuffer = mappedInputBuffer; // Map DX11 resource
									picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_ARGB;
									picParams.outputBitstream = outputBitstream;
									picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

									nvRes = nvFuncs.nvEncEncodePicture(nvEncoder, &picParams);
									CHECK_NVENC_RESULT_GOTO(L"nvEncEncodePicture", nvRes, DrainRelease);

									// Lock and retrieve encoded data
									NV_ENC_LOCK_BITSTREAM lockBitstreamParams = { NV_ENC_LOCK_BITSTREAM_VER };
									lockBitstreamParams.outputBitstream = outputBitstream;

									nvRes = nvFuncs.nvEncLockBitstream(nvEncoder, &lockBitstreamParams);
									CHECK_NVENC_RESULT_GOTO(L"nvEncLockBitstream", nvRes, DrainRelease);
									auto releaseLockBitstream = gsl::finally([&]() -> void { nvFuncs.nvEncUnlockBitstream(nvEncoder, outputBitstream); });

									void *buffer = lockBitstreamParams.bitstreamBufferPtr;
									size_t sz = lockBitstreamParams.bitstreamSizeInBytes;
									bool keyframe = (lockBitstreamParams.pictureType == NV_ENC_PIC_TYPE_IDR);

									pushAvi(buffer, sz, keyframe);
									std::wcout << L"Encoded"sv << (keyframe ? L" keyframe"sv : L""sv) << L" ("sv << sz << L" bytes)!\n"sv;
#else
									amf::AMFSurface *amfSurface;
									amfRes = amfContext->CreateSurfaceFromDX11Native(frameBuffer, &amfSurface, NULL);
									CHECK_AMF_RESULT_GOTO(L"CreateSurfaceFromDX11Native"sv, amfRes, DrainRelease);
									auto releaseAmfSurface = gsl::finally([&]() -> void { amfSurface->Release(); });

									amfRes = amfEncoder->SubmitInput(amfSurface);
									CHECK_AMF_RESULT_GOTO(L"SubmitInput"sv, amfRes, Break);
									// AMF_NOT_INITIALIZED 0xd, AMF_EOF 0x17
#endif
								}
#if !CAPTURE_NVIDIA
								fetchEncodedFrames(false);
#endif
							}
#if !CAPTURE_NVIDIA
						Break:
#endif
							break;
						case WM_APP + 1:
							goto DrainRelease;
						}
					}
					else
					{
						DispatchMessageW(&msg);
					}
				}
			}

		DrainRelease:
#if !CAPTURE_NVIDIA
			std::wcout << L"Drain...\n"sv;
			fetchEncodedFrames(true);
#else
			std::wcout << L"Flush...\n"sv;
#endif
			flushAvi();
		}
	Release:
		if (s_Running)
		{
			std::wcout << L"Done, wait 10 seconds!\n"sv;
			Sleep(10000);
		}
	} while (s_Running);

	return EXIT_SUCCESS;
}

/* end of file */

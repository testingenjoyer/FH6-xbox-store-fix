/**
 * d3d12.dll proxy - Forza Horizon 6 FH201 fix dla RX570/580
 * Dziala z wersja Steam i Xbox Game Pass.
 * Kompilacja: x86_64-w64-mingw32-g++ (patrz build.bat)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static FILE* g_log;

static void log_init() { if (!g_log) fopen_s(&g_log, "FH6Fix_RX570.log", "w"); }

static void logf(const char* fmt, ...) {
    log_init();
    if (!g_log) return;
    va_list va; va_start(va, fmt);
    vfprintf(g_log, fmt, va);
    va_end(va);
    fputc('\n', g_log);
    fflush(g_log);
}

// ---------------------------------------------------------------------------
// Real d3d12.dll z System32
// ---------------------------------------------------------------------------
static HMODULE g_real;

static HMODULE real_dll() {
    if (g_real) return g_real;
    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);
    strcat_s(path, "\\d3d12.dll");
    g_real = LoadLibraryExA(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (g_real) logf("[FH6Fix] Zaladowano %s", path);
    else        logf("[FH6Fix] BLAD: nie mozna zaladowac %s", path);
    return g_real;
}

static void* get_fn(const char* name) {
    HMODULE m = real_dll();
    if (!m) return nullptr;
    void* p = (void*)GetProcAddress(m, name);
    if (!p) logf("[FH6Fix] BRAK eksportu: %s", name);
    return p;
}

// ---------------------------------------------------------------------------
// Wskazniki dla funkcji bez znanych sygnatur (trampoliny)
// ---------------------------------------------------------------------------
// extern "C" zeby inline asm mogl referencowac bez name manglingu
extern "C" {
    void* pfn_CoreCreate   = nullptr;
    void* pfn_CoreGetSize  = nullptr;
    void* pfn_CoreReg      = nullptr;
    void* pfn_DRED         = nullptr;
    void* pfn_GetIface     = nullptr;
    void* pfn_PIXReplace   = nullptr;
    void* pfn_PIXThread    = nullptr;
    void* pfn_PIXWake      = nullptr;
    void* pfn_PIXCounter   = nullptr;
    void* pfn_GetBeh       = nullptr;
    void* pfn_SetApp       = nullptr;
    // D3D12CreateVersionedRootSignatureDeserializer nie jest w naglowkach MinGW
    void* pfn_CreateVRSD   = nullptr;
}

// Trampoliny RIP-relative (dzialaja na x86-64 MinGW)
#define TRAMPOLINE(export_name, ptr_var)                              \
    extern "C" __declspec(dllexport)                                  \
    __attribute__((naked)) void export_name() {                       \
        asm volatile("jmpq *" #ptr_var "(%rip)");                    \
    }

TRAMPOLINE(D3D12CoreCreateLayeredDevice,                    pfn_CoreCreate)
TRAMPOLINE(D3D12CoreGetLayeredDeviceSize,                   pfn_CoreGetSize)
TRAMPOLINE(D3D12CoreRegisterLayers,                         pfn_CoreReg)
TRAMPOLINE(D3D12CreateVersionedRootSignatureDeserializer,   pfn_CreateVRSD)
TRAMPOLINE(D3D12DeviceRemovedExtendedData,                  pfn_DRED)
TRAMPOLINE(D3D12GetInterface,                               pfn_GetIface)
TRAMPOLINE(D3D12PIXEventsReplaceBlock,                      pfn_PIXReplace)
TRAMPOLINE(D3D12PIXGetThreadInfo,                           pfn_PIXThread)
TRAMPOLINE(D3D12PIXNotifyWakeFromFenceSignal,               pfn_PIXWake)
TRAMPOLINE(D3D12PIXReportCounter,                           pfn_PIXCounter)
TRAMPOLINE(GetBehaviorValue,                                pfn_GetBeh)
TRAMPOLINE(SetAppCompatStringPointer,                       pfn_SetApp)

// ---------------------------------------------------------------------------
// Typowane wrappery dla funkcji znanych z d3d12.h
// ---------------------------------------------------------------------------

// Wskazniki (tylko funkcje zadeklarowane w naglowkach MinGW)
static decltype(&D3D12CreateRootSignatureDeserializer) pfn_RootSig  = nullptr;
static decltype(&D3D12EnableExperimentalFeatures)      pfn_Exp      = nullptr;
static decltype(&D3D12GetDebugInterface)               pfn_DbgIface = nullptr;
static decltype(&D3D12SerializeRootSignature)          pfn_SerRS    = nullptr;
static decltype(&D3D12SerializeVersionedRootSignature) pfn_SerRSV   = nullptr;

extern "C" {

__declspec(dllexport)
HRESULT WINAPI D3D12CreateRootSignatureDeserializer(
    const void* data, SIZE_T size, REFIID iid, void** out)
{
    return pfn_RootSig ? pfn_RootSig(data, size, iid, out) : E_NOTIMPL;
}

__declspec(dllexport)
HRESULT WINAPI D3D12EnableExperimentalFeatures(
    UINT count, const IID* iids, void* configs, UINT* sizes)
{
    return pfn_Exp ? pfn_Exp(count, iids, configs, sizes) : E_NOTIMPL;
}

__declspec(dllexport)
HRESULT WINAPI D3D12GetDebugInterface(REFIID iid, void** debug)
{
    return pfn_DbgIface ? pfn_DbgIface(iid, debug) : E_NOTIMPL;
}

__declspec(dllexport)
HRESULT WINAPI D3D12SerializeRootSignature(
    const D3D12_ROOT_SIGNATURE_DESC* desc, D3D_ROOT_SIGNATURE_VERSION ver,
    ID3DBlob** blob, ID3DBlob** err)
{
    return pfn_SerRS ? pfn_SerRS(desc, ver, blob, err) : E_NOTIMPL;
}

__declspec(dllexport)
HRESULT WINAPI D3D12SerializeVersionedRootSignature(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc, ID3DBlob** blob, ID3DBlob** err)
{
    return pfn_SerRSV ? pfn_SerRSV(desc, blob, err) : E_NOTIMPL;
}

} // extern "C"

// ---------------------------------------------------------------------------
// Patch vtable CheckFeatureSupport
// ---------------------------------------------------------------------------
using PFN_CheckFeature = HRESULT(STDMETHODCALLTYPE*)(
    ID3D12Device*, D3D12_FEATURE, void*, UINT);

static PFN_CheckFeature g_orig_check = nullptr;

static HRESULT STDMETHODCALLTYPE hooked_CheckFeatureSupport(
    ID3D12Device* dev, D3D12_FEATURE feature, void* data, UINT size)
{
    HRESULT hr = g_orig_check(dev, feature, data, size);

    // Log wszystkich wywolan - potrzebny do diagnostyki
    logf("[FH6Fix] CheckFeatureSupport(%d) HR=0x%08lX", (int)feature, hr);

    // Spoof feature level - twierdzimy ze karta wspiera 12_1
    if (feature == D3D12_FEATURE_FEATURE_LEVELS
        && data && size >= sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS))
    {
        auto* fl = static_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS*>(data);
        fl->MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_1;
        logf("[FH6Fix] -> Spoof FEATURE_LEVELS -> 12_1");
        return S_OK;
    }

    // FORMAT_SUPPORT: RX570 zwraca E_FAIL dla formatow 12_1 gdy device jest na 12_0.
    if (feature == D3D12_FEATURE_FORMAT_SUPPORT && FAILED(hr)
        && data && size >= sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT))
    {
        auto* fs = static_cast<D3D12_FEATURE_DATA_FORMAT_SUPPORT*>(data);
        fs->Support1 = D3D12_FORMAT_SUPPORT1_NONE;
        fs->Support2 = D3D12_FORMAT_SUPPORT2_NONE;
        logf("[FH6Fix] -> FORMAT_SUPPORT E_FAIL -> spoof S_OK+NONE");
        return S_OK;
    }

    return hr;
}

static void patch_device(ID3D12Device* dev) {
    void** vtable = *reinterpret_cast<void***>(dev);
    constexpr int SLOT = 13; // CheckFeatureSupport w ID3D12Device (7+6)

    if (vtable[SLOT] == (void*)hooked_CheckFeatureSupport) return;

    g_orig_check = (PFN_CheckFeature)vtable[SLOT];
    DWORD old;
    if (!VirtualProtect(&vtable[SLOT], sizeof(void*), PAGE_READWRITE, &old)) {
        logf("[FH6Fix] VirtualProtect BLAD: 0x%08lX", GetLastError());
        return;
    }
    vtable[SLOT] = (void*)hooked_CheckFeatureSupport;
    VirtualProtect(&vtable[SLOT], sizeof(void*), old, &old);
    logf("[FH6Fix] vtable[%d] patchowany", SLOT);
}

// ---------------------------------------------------------------------------
// D3D12CreateDevice - glowny intercept
// ---------------------------------------------------------------------------
using PFN_CreateDevice = decltype(&D3D12CreateDevice);
static PFN_CreateDevice g_real_create = nullptr;

static const D3D_FEATURE_LEVEL k_levels[] = {
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
};

extern "C" __declspec(dllexport)
HRESULT WINAPI D3D12CreateDevice(
    IUnknown* adapter, D3D_FEATURE_LEVEL min_fl, REFIID riid, void** ppDevice)
{
    logf("[FH6Fix] D3D12CreateDevice (FL=0x%X)", (unsigned)min_fl);

    if (!g_real_create) {
        g_real_create = (PFN_CreateDevice)get_fn("D3D12CreateDevice");
        if (!g_real_create) return E_FAIL;
    }

    HRESULT hr = g_real_create(adapter, min_fl, riid, ppDevice);
    if (SUCCEEDED(hr)) {
        logf("[FH6Fix] Urzadzenie OK na FL=0x%X", (unsigned)min_fl);
        if (ppDevice && *ppDevice) patch_device((ID3D12Device*)*ppDevice);
        return hr;
    }

    logf("[FH6Fix] FL=0x%X nie dziala (0x%08lX), probuje fallback...", (unsigned)min_fl, hr);
    for (auto fl : k_levels) {
        hr = g_real_create(adapter, fl, riid, ppDevice);
        logf("[FH6Fix] FL=0x%X -> 0x%08lX", (unsigned)fl, hr);
        if (SUCCEEDED(hr)) {
            if (ppDevice && *ppDevice) patch_device((ID3D12Device*)*ppDevice);
            return hr;
        }
    }

    logf("[FH6Fix] BLAD: zadne FL nie zadziala");
    return hr;
}

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    log_init();
    logf("[FH6Fix] === d3d12.dll proxy zaladowany - FH201 fix RX570/580 ===");

    HMODULE m = real_dll();
    if (!m) return FALSE;

    pfn_CoreCreate  = get_fn("D3D12CoreCreateLayeredDevice");
    pfn_CoreGetSize = get_fn("D3D12CoreGetLayeredDeviceSize");
    pfn_CoreReg     = get_fn("D3D12CoreRegisterLayers");
    pfn_DRED        = get_fn("D3D12DeviceRemovedExtendedData");
    pfn_GetIface    = get_fn("D3D12GetInterface");
    pfn_PIXReplace  = get_fn("D3D12PIXEventsReplaceBlock");
    pfn_PIXThread   = get_fn("D3D12PIXGetThreadInfo");
    pfn_PIXWake     = get_fn("D3D12PIXNotifyWakeFromFenceSignal");
    pfn_PIXCounter  = get_fn("D3D12PIXReportCounter");
    pfn_GetBeh      = get_fn("GetBehaviorValue");
    pfn_SetApp      = get_fn("SetAppCompatStringPointer");

    pfn_CreateVRSD = get_fn("D3D12CreateVersionedRootSignatureDeserializer");
    pfn_RootSig  = (decltype(pfn_RootSig)) get_fn("D3D12CreateRootSignatureDeserializer");
    pfn_Exp      = (decltype(pfn_Exp))     get_fn("D3D12EnableExperimentalFeatures");
    pfn_DbgIface = (decltype(pfn_DbgIface))get_fn("D3D12GetDebugInterface");
    pfn_SerRS    = (decltype(pfn_SerRS))   get_fn("D3D12SerializeRootSignature");
    pfn_SerRSV   = (decltype(pfn_SerRSV))  get_fn("D3D12SerializeVersionedRootSignature");

    logf("[FH6Fix] Wszystkie eksporty gotowe");
    return TRUE;
}

#include "AppPaths.h"
#include "D3D12Context.h"
#include "DlssNrRunner.h"
#include "ImageImport.h"
#include "ModelBootstrap.h"
#include <Windows.h>
#include <filesystem>
#include <algorithm>
#include <cwchar>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
struct Options {
    std::optional<std::filesystem::path> input;
    std::optional<std::filesystem::path> output;
    std::optional<std::filesystem::path> depth;
    std::filesystem::path runtime = app::DefaultRuntimeDll();
    ImageImportSettings import;
    DepthImportSettings depthImport;
    DlssNrSettings dlss;
    bool setupModels = false;
    bool check = false;
    bool help = false;
};

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(0,n)), '\0');
    if(n>0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}
[[noreturn]] void ArgError(const std::wstring& message) { throw std::runtime_error("Argument error: " + Narrow(message)); }
int ParseInt(const wchar_t* value, const wchar_t* name) { wchar_t* end=nullptr; long v=wcstol(value,&end,10); if(!end||*end)ArgError(std::wstring(L"invalid integer for ")+name); return static_cast<int>(v); }
float ParseFloat(const wchar_t* value, const wchar_t* name) { wchar_t* end=nullptr; float v=wcstof(value,&end); if(!end||*end)ArgError(std::wstring(L"invalid number for ")+name); return v; }

Options ParseArgs(int argc, wchar_t** argv) {
    Options o;
    for (int i=1;i<argc;++i) {
        const std::wstring a=argv[i];
        auto need=[&](const wchar_t* n)->const wchar_t*{ if(++i>=argc)ArgError(std::wstring(L"missing value for ")+n); return argv[i]; };
        if(a==L"--help"||a==L"-h"||a==L"/?")o.help=true;
        else if(a==L"--setup-models")o.setupModels=true;
        else if(a==L"--check")o.check=true;
        else if(a==L"-o"||a==L"--output")o.output=std::filesystem::path(need(L"--output"));
        else if(a==L"--runtime")o.runtime=std::filesystem::path(need(L"--runtime"));
        else if(a==L"--depth")o.depth=std::filesystem::path(need(L"--depth"));
        else if(a==L"--exr-layer")o.import.exrLayer=Narrow(need(L"--exr-layer"));
        else if(a==L"--exposure")o.import.exposureEv=ParseFloat(need(L"--exposure"),L"--exposure");
        else if(a==L"--tonemap"){int v=ParseInt(need(L"--tonemap"),L"--tonemap"); if(v<0||v>2)ArgError(L"--tonemap must be 0..2");o.import.toneMap=static_cast<ExrToneMap>(v);}
        else if(a==L"--no-srgb")o.import.srgbEncode=false;
        else if(a==L"--depth-layer")o.depthImport.exrLayer=Narrow(need(L"--depth-layer"));
        else if(a==L"--depth-exr-channel")o.depthImport.exrChannel=Narrow(need(L"--depth-exr-channel"));
        else if(a==L"--depth-channel"){int v=ParseInt(need(L"--depth-channel"),L"--depth-channel");if(v<0||v>4)ArgError(L"--depth-channel must be 0..4");o.depthImport.channel=v;}
        else if(a==L"--depth-no-normalize")o.depthImport.autoNormalize=false;
        else if(a==L"--depth-linear")o.depthImport.inverseDepth=false;
        else if(a==L"--depth-scale")o.depthImport.scale=ParseFloat(need(L"--depth-scale"),L"--depth-scale");
        else if(a==L"--depth-offset")o.depthImport.offset=ParseFloat(need(L"--depth-offset"),L"--depth-offset");
        else if(a==L"--iterations"){int v=ParseInt(need(L"--iterations"),L"--iterations");if(v<1||v>64)ArgError(L"--iterations must be 1..64");o.dlss.iterations=static_cast<uint32_t>(v);}
        else if(a==L"--preset"){o.dlss.preset=ParseInt(need(L"--preset"),L"--preset");if(o.dlss.preset<0||o.dlss.preset>3)ArgError(L"--preset must be 0..3");}
        else if(a==L"--style"){o.dlss.style=ParseInt(need(L"--style"),L"--style");if(o.dlss.style<0||o.dlss.style>2)ArgError(L"--style must be 0..2");}
        else if(a==L"--intensity")o.dlss.intensity=ParseFloat(need(L"--intensity"),L"--intensity");
        else if(a==L"--local-tone")o.dlss.localToneStrength=ParseFloat(need(L"--local-tone"),L"--local-tone");
        else if(a==L"--local-structure")o.dlss.localStructureStrength=ParseFloat(need(L"--local-structure"),L"--local-structure");
        else if(a==L"--skin-structure")o.dlss.skinStructureStrength=ParseFloat(need(L"--skin-structure"),L"--skin-structure");
        else if(a==L"--auto-mask")o.dlss.autoMask=true;
        else if(a==L"--ui-correction")o.dlss.uiCorrection=true;
        else if(!a.empty()&&a[0]==L'-')ArgError(L"unknown option: "+a);
        else if(!o.input)o.input=std::filesystem::path(a);
        else ArgError(L"only one source image is supported");
    }
    return o;
}

void PrintHelp(){std::cout<<R"(Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - Native NVOF D3D12 Execute / Adaptive Stable Motion

GUI:
  Run "Crow-DLSS5-Video-Image-Converter-Image.exe"

CLI:
  Crow-DLSS5-Video-Image-Converter-CLI <image> [options]

Image import:
  --exr-layer <name>       Blender/EXR layer (e.g. ViewLayer.Combined)
  --exposure <EV>          EXR scene-linear exposure before tone map
  --tonemap <0|1|2>        0 Clamp, 1 Reinhard, 2 ACES Fitted
  --no-srgb                Do not sRGB encode after EXR tone mapping

Manual depth:
  --depth <file>           PNG/JPG/TIFF/EXR depth image
  --depth-layer <name>     EXR depth layer (RGB/A helper path)
  --depth-exr-channel <n>  Exact EXR channel, e.g. ViewLayer.Depth.Z
  --depth-channel <0..4>   R,G,B,A,Luminance for normal images/RGBA EXR
  --depth-no-normalize     Keep source numeric range before scale/offset clamp
  --depth-linear           Black=near / DepthInverted=0 (default white=near)
  --depth-scale <float>
  --depth-offset <float>

DLSSNR (ReShade/RenoDX-style core controls):
  --preset <0..3>
  --style <0..2>
  --intensity <float>
  --local-tone <float>
  --local-structure <float>
  --skin-structure <float>
  --auto-mask
  --ui-correction
  --iterations <1..64>

Output:
  -o, --output <file.png|file.exr>

Note: V0.3 EXR input is converted to the already validated SDR RGBA8 Feature-18 contract.
EXR output stores that SDR result as float/half EXR; it is not scene-linear Cycles radiance.
)";}

std::filesystem::path DefaultOutput(const std::filesystem::path& input){return input.parent_path()/(input.stem().wstring()+L"_DLSS5.png");}
void PrintDependencyStatus(const std::filesystem::path& runtime){
    std::cout<<"Runtime DLL: "<<runtime.string()<<" -> "<<(std::filesystem::exists(runtime)?"FOUND":"MISSING")<<"\n";
    try{D3D12Context d3d;DXGI_ADAPTER_DESC1 desc{};if(d3d.Adapter())d3d.Adapter()->GetDesc1(&desc);std::cout<<"D3D12 NVIDIA adapter: "<<Narrow(desc.Description)<<" -> READY\n";}catch(const std::exception&e){std::cout<<"D3D12 NVIDIA adapter: ERROR -> "<<e.what()<<"\n";}
}
}

int wmain(int argc,wchar_t**argv){
    const HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);const bool uninit=SUCCEEDED(co);
    try{
        const Options o=ParseArgs(argc,argv);
        if(o.help||argc==1){PrintHelp();if(uninit)CoUninitialize();return 0;}
        if(o.setupModels){std::cout<<"[Models] "<<models::EnsureDepthAnythingV2().string()<<"\n";}
        if(o.check)PrintDependencyStatus(o.runtime);
        if(!o.input){if(uninit)CoUninitialize();return 0;}
        if(!std::filesystem::exists(*o.input))throw std::runtime_error("Input image not found: "+o.input->string());
        if(!std::filesystem::exists(o.runtime))throw std::runtime_error("nvngx_dlssnr.dll missing: "+o.runtime.string());
        const auto imported=LoadImageForDlss(*o.input,o.import);
        std::optional<DepthMap> depth;
        auto dlss=o.dlss;
        if(o.depth){if(!std::filesystem::exists(*o.depth))throw std::runtime_error("Depth image not found: "+o.depth->string());depth=LoadDepthMap(*o.depth,o.depthImport,imported.display.width,imported.display.height);dlss.depthInverted=o.depthImport.inverseDepth;}
        D3D12Context d3d;DlssNrRunner runner(d3d,o.runtime,dlss);
        const auto result=runner.Process(imported.display,depth?&depth->values:nullptr);
        const auto output=o.output.value_or(DefaultOutput(*o.input));
        SaveOutputImage(output,result);
        std::cout<<"[Output] "<<output.string()<<"\n";
        if(uninit)CoUninitialize();return 0;
    }catch(const std::exception&e){std::cerr<<"[ERROR] "<<e.what()<<"\n";if(uninit)CoUninitialize();return 1;}
}

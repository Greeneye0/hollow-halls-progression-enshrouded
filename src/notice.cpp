#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "notice.h"
#include <wincodec.h>
#include <cwctype>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")

namespace hhp::notice {
namespace {
struct Text {const char* bytes;size_t length;uint64_t hash;};
Text MakeText(const char* s){Text t{s,strlen(s),0xcbf29ce484222325ull};for(size_t i=0;i<t.length;++i)t.hash=(t.hash^uint8_t(s[i]))*0x100000001b3ull;return t;}
template<class T>T& at(void* p,size_t n){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+n);}
using Begin=void(*)(void*,uint64_t);using End=void(*)(void*);using Size=void(*)(void*,uint64_t);
using Layer=void*(*)(void*,const Text*,uint8_t,uint32_t,int32_t,uint32_t);
using Named=void*(*)(void*,const Text*);using Opaque=void*(*)(void*);
using Label=void(*)(uint8_t*,const Text*,const uint8_t*,uint8_t);
using DrawImage=void(*)(void*,const float*,const uint32_t*,const uint64_t*);
using LocaText=Text*(*)(Text*,uint32_t);LocaText loca{};
// FbUiLocaLinks (assets + 0x10) -> rpgTerms (+0x40) -> rarityLegendary (+0x1dd0): a 16-byte key.
constexpr size_t kLocaLegendary=0x10+0x40+0x1dd0;
std::atomic<int> forced{-1},detected{-1}; // -1: not known yet (English)
uint64_t nextDetect=0;
Begin begin{};End end{},endLayer{},endLabel{};Size size{};Layer layer{};Named named{};Opaque opaque{};Label label{};DrawImage draw{};
void** root=nullptr;
std::mutex mutex;std::string heading,body;uint64_t until=0,started=0,revealMs=0;bool ready=false,showBanners=true;
uint64_t Pack(float x,float y){float xy[]={x,y};uint64_t value;memcpy(&value,xy,8);return value;}
struct Widget {alignas(16) uint8_t raw[0x80]{};void* handle(){return at<void*>(raw,8);}};
struct Box {
    Widget widget;
    Box(float x,float y,float w,float h){begin(widget.raw,0);size(widget.handle(),Pack(w,h));at<uint64_t>(widget.handle(),0x70)=Pack(x,y);}
    ~Box(){end(widget.raw);}
};
// A named layer and its parent widget, closed again however the scope ends: a fault while drawing
// (caught in main.cpp, destructors run under /EHa) must not leave the game's UI stack open.
struct OpenLayer {
    alignas(16) uint8_t frame[0x400]{};Widget parent;
    explicit OpenLayer(const char* id){auto name=MakeText(id);layer(frame,&name,1,0x16,-1,0);named(parent.raw,&name);opaque(parent.raw);}
    ~OpenLayer(){end(parent.raw);endLayer(frame);}
    OpenLayer(const OpenLayer&)=delete;OpenLayer& operator=(const OpenLayer&)=delete;
    float* rect(){return static_cast<float*>(static_cast<void*>(static_cast<uint8_t*>(parent.handle())+0x114));}
};
void Fill(Box& box,uint32_t color) {
    uint32_t image=0;struct Color {uint32_t rgba;float brightness;} c{color,1};uint64_t packed;memcpy(&packed,&c,8);
    draw(box.widget.raw,static_cast<float*>(static_cast<void*>(static_cast<uint8_t*>(box.widget.handle())+0x114)),&image,&packed);
}
void Line(const std::string& value,void* assets,uint32_t color,uint64_t visible=~uint64_t(0)) {
    auto text=MakeText(value.c_str());alignas(16) uint8_t buffer[0x400]{};
    label(buffer,&text,static_cast<uint8_t*>(assets)+0x10bbc,0);
    buffer[0x3d]=1;at<uint32_t>(buffer,0xb0)=color;at<float>(buffer,0xb4)=1;
    // Native EndLabel uses +0xa0 as its visible-glyph limit (normally -1).
    // Shape and measure the complete line once, then reveal glyphs without
    // changing its geometry, kerning, baseline or widget identity each frame.
    at<uint64_t>(buffer,0xa0)=visible;
    endLabel(buffer);
}
void Log(const char* s){if(auto* host=greeneye::Host())host->log(s);}
uint32_t Fnv1a32(const uint8_t* p,size_t n){uint32_t h=0x811c9dc5u;for(size_t i=0;i<n;++i)h=(h^p[i])*0x1000193u;return h;}
// The game's language can change while it runs; look at its word for "Legendary" once a second.
void DetectLanguage(void* assets) {
    auto now=GetTickCount64();
    if(!loca||!assets||now<nextDetect)return;
    nextDetect=now+1000;
    Text result=MakeText("");loca(&result,Fnv1a32(static_cast<uint8_t*>(assets)+kLocaLegendary,16));
    if(!result.bytes||!result.length)return;
    std::string word(result.bytes,(std::min)(result.length,size_t(64)));text::Lang lang;
    if(!text::Detect(word,lang)||int(lang)==detected.load())return;
    detected=int(lang);
    char line[160];sprintf_s(line,"Hollow Halls Progression: notices in %s (the game's \"Legendary\": %s)",text::kCodes[size_t(lang)],word.c_str());Log(line);
}
template<class T>bool Bind(const GreeneyeHost* host,T& out,const char* pattern){out=reinterpret_cast<T>(host->find(pattern,nullptr));return out!=nullptr;}

// --- The loading screen (drawn as Town Portal's arrival screen is) ---
// The game's dynamic RGBA8 UI texture uploader: asked every frame with a stable key, it only
// refreshes a resident texture and returns its id; an evicted one is uploaded again.
struct TextureDesc {uint8_t flags,pad0[7];const void* pixels;uint64_t byteSize,keyLo,keyHi,keySub;uint32_t width,height;uint8_t format,pad1[7];};
static_assert(sizeof(TextureDesc)==0x40,"the uploader takes a 0x40-byte descriptor");
using UiCtx=void*(*)();using Upload=uint32_t(*)(void*,const TextureDesc*);
using DrawSub=void(*)(void*,const float*,uint32_t,uint64_t,uint64_t,uint64_t);
UiCtx uiCtx{};Upload upload{};DrawSub drawSub{};
const uint64_t textureSalt=GetTickCount64(); // a reloaded copy keys its own textures
std::wstring pictureDir;
struct Picture {int hall=-1;uint32_t width{},height{};std::vector<uint8_t> rgba;};
std::mutex pictureMutex;Picture picture; // the decoded picture, replaced by the worker
std::mutex workerMutex;std::thread* worker{};int wanted=-1; // one decode at a time
std::atomic<bool> loading{false};std::atomic<uint64_t> loadingChanged{0},loadingSince{0};
std::atomic<int> loadingHall{-1};std::atomic<float> loadingTarget{0};std::string loadingCaption; // caption under mutex
float shownProgress=0;uint64_t shownAt=0;
constexpr float kInMs=350,kOutMs=600;
constexpr const wchar_t* kPictures[]={L"springlands",L"revelwood",L"nomad",L"veilwater",L"albaneve",L"kindlewastes"}; // kHalls order

bool LoadRgba(const wchar_t* path,uint32_t& width,uint32_t& height,std::vector<uint8_t>& rgba) {
    // S_OK / S_FALSE: this call initialised COM and must balance it; a thread already in
    // another apartment (RPC_E_CHANGED_MODE) can use WIC as it is.
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IWICImagingFactory* factory{};IWICBitmapDecoder* decoder{};IWICBitmapFrameDecode* frame{};IWICFormatConverter* converter{};
    UINT w{},h{};
    bool ok=SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&
        SUCCEEDED(factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))&&
        SUCCEEDED(decoder->GetFrame(0,&frame))&&SUCCEEDED(frame->GetSize(&w,&h))&&w&&h&&w<=4096&&h<=4096&&
        SUCCEEDED(factory->CreateFormatConverter(&converter))&&
        SUCCEEDED(converter->Initialize(frame,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    if(ok){rgba.resize(size_t(w)*h*4);ok=SUCCEEDED(converter->CopyPixels(nullptr,w*4,UINT(rgba.size()),rgba.data()));}
    if(converter)converter->Release();if(frame)frame->Release();if(decoder)decoder->Release();if(factory)factory->Release();
    if(SUCCEEDED(com))CoUninitialize();
    width=w;height=h;return ok;
}
void Decode(int hall) {
    Picture p;p.hall=hall;
    auto path=pictureDir+kPictures[hall]+L".jpg";
    if(!LoadRgba(path.c_str(),p.width,p.height,p.rgba)){
        char line[200];sprintf_s(line,"Hollow Halls Progression: loading\\%ls.jpg missing or unreadable; the loading screen stays dark",kPictures[hall]);Log(line);
        p.rgba.clear();
    }
    std::lock_guard guard(pictureMutex);picture=std::move(p);
}
void Request(int hall) {
    std::lock_guard guard(workerMutex);
    if(hall<0||hall>=int(std::size(kPictures))||hall==wanted||pictureDir.empty())return;
    if(worker){worker->join();delete worker;worker=nullptr;}
    wanted=hall;worker=new std::thread(Decode,hall);
}
uint32_t SafeUpload(void* repo,const TextureDesc* desc){__try{return upload(repo,desc);}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}}
void* AssetRepo() {
    auto* ctx=uiCtx?static_cast<uint8_t*>(uiCtx()):nullptr;
    auto* a=ctx?at<uint8_t*>(ctx,0x10):nullptr;auto* b=a?at<uint8_t*>(a,0x58):nullptr;
    return b?at<void*>(b,0xe98):nullptr;
}
uint32_t Fade(uint32_t abgr,float alpha){return (abgr&0x00ffffffu)|(uint32_t(float(abgr>>24)*std::clamp(alpha,0.f,1.f)+.5f)<<24);}
uint64_t Color(uint32_t abgr){struct {uint32_t rgba;float brightness;} c{abgr,1};uint64_t packed;memcpy(&packed,&c,8);return packed;}
float* RectOf(Box& box){return static_cast<float*>(static_cast<void*>(static_cast<uint8_t*>(box.widget.handle())+0x114));}
float LoadingAlpha(uint64_t now) {
    const float since=float(now-(std::min)(now,loadingChanged.load()));
    return loading?(std::min)(1.f,since/kInMs):(std::max)(0.f,1.f-since/kOutMs);
}
// A label centred in the current box.
// A label centred in the box x, y, w, h (screen pixels), as Town Portal's DrawLabel: the label gets
// its own rectangle (centred text needs a width to centre in).
void Centred(float x,float y,float w,float h,const std::string& value,const uint8_t* style,uint32_t color) {
    auto text=MakeText(value.c_str());alignas(16) uint8_t buffer[0x400]{};
    label(buffer,&text,style,0);
    if(auto* handle=at<uint8_t*>(buffer,8)){at<float>(handle,0x114)=x;at<float>(handle,0x118)=y-1.5f;at<float>(handle,0x11c)=w;at<float>(handle,0x120)=h;}
    buffer[0x38]=2;buffer[0x39]=2;buffer[0x3c]=0;buffer[0x3d]=1;
    at<uint32_t>(buffer,0xb0)=color|0xff000000u;at<float>(buffer,0xb4)=float(color>>24)/255.f;
    endLabel(buffer);
}
// Two passes on two layers: the game draws a layer's text before its images, so the caption and the
// percentage go on a second layer over the picture.
void DrawLoadingPicture(float w,float h,uint64_t now,float alpha) {
    {Box all(0,0,w,h);
        Fill(all,Fade(0xff0a0705u,alpha));
        // The hall's picture, cover-cropped to the screen and slowly closing in.
        uint32_t id=0;float aspect=0;
        if(upload&&drawSub){
            std::lock_guard guard(pictureMutex);
            void* repo=AssetRepo();
            if(repo&&picture.hall==loadingHall.load()&&!picture.rgba.empty()){
                TextureDesc desc{};desc.pixels=picture.rgba.data();desc.byteSize=picture.rgba.size();
                desc.keyLo=0x4848504c4f414431ull;desc.keyHi=textureSalt;desc.keySub=uint64_t(picture.hall+1);
                desc.width=picture.width;desc.height=picture.height;desc.format=0x25;
                id=SafeUpload(repo,&desc);aspect=float(picture.width)/float(picture.height);
            }
        }
        if(id){
            const float screen=w/h,zoom=1.f+.05f*(std::min)(1.f,float(now-loadingSince.load())/20000.f);
            const float cu=(screen<aspect?screen/aspect:1.f)/zoom,cv=(screen<aspect?1.f:aspect/screen)/zoom;
            drawSub(all.widget.raw,RectOf(all),id,Pack((1-cu)*.5f,(1-cv)*.5f),Pack((1+cu)*.5f,(1+cv)*.5f),Color(Fade(0xffffffffu,alpha)));
        }
        // Darker toward the bottom, under the text: drawn in the picture's own widget after it (so
        // over it), in bands on whole pixels that meet edge to edge.
        const float* origin=RectOf(all);
        constexpr int kBands=24;
        for(int i=0;i<kBands;++i){
            const float from=.55f+.45f*float(i)/kBands,to=.55f+.45f*float(i+1)/kBands;
            const float top=std::floor(h*from),bottom=i==kBands-1?h:std::floor(h*to),t=(float(i)+.5f)/kBands;
            float band[4]={origin[0],origin[1]+top,w,bottom-top};uint32_t image=0;
            const uint64_t color=Color(Fade(0xff000000u,alpha*.75f*t*t));
            draw(all.widget.raw,band,&image,&color);
        }
    }
}
void DrawLoadingText(void* assets,float x,float y,float w,float h,uint64_t now,float alpha) {
    // Progress: eased toward the estimate, and on to 100% as the screen leaves.
    const float target=loading?std::clamp(loadingTarget.load(),0.f,1.f):1.f;
    const float dt=(std::min)(.1f,float(now-(std::min)(now,shownAt))*.001f);shownAt=now;
    shownProgress+=(target-shownProgress)*(std::min)(1.f,dt*(loading?4.f:9.f));
    if(target-shownProgress<.002f)shownProgress=target;
    std::string caption;{std::lock_guard guard(mutex);caption=loadingCaption;}
    // The game's outlined text for the caption, used in place: a style holds self-relative offsets
    // (its font name), so a copy elsewhere points the game's text code at garbage.
    const auto* style=static_cast<uint8_t*>(assets)+0x10bbc+0x58;
    if(!caption.empty())Centred(x,y+h-170,w,60,caption,style,Fade(0xffe8e2d8u,alpha));
    char percent[96];sprintf_s(percent,text::Get(Language(),text::LoadingPercent),int(shownProgress*100.f+.5f));
    Centred(x,y+h-100,w,40,percent,static_cast<uint8_t*>(assets)+0x10bbc,Fade(0xffc8c0b4u,alpha*alpha));
}
}
bool Init(const GreeneyeHost* host,void** uiRoot) {
    if(host->modDir){
        auto ini=std::wstring(host->modDir)+L"hollow-halls-progression.ini";
        wchar_t value[32]{};
        GetPrivateProfileStringW(L"Messages",L"ShowNotices",L"true",value,32,ini.c_str());
        std::wstring v(value);for(auto& c:v)c=wchar_t(towlower(c));
        showBanners=!(v==L"false"||v==L"off"||v==L"no"||v==L"0");
        GetPrivateProfileStringW(L"Messages",L"Language",L"auto",value,32,ini.c_str());
        std::string code;for(auto* c=value;*c;++c)code+=*c<128?char(*c):'?';
        forced=text::ParseCode(code);
        pictureDir=std::wstring(host->modDir)+L"loading\\";
    }
    root=uiRoot;bool ok=true;
    ok&=Bind(host,begin,"40 53 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 48 8B D9 45 33 C0");
    ok&=Bind(host,end,"48 89 5C 24 18 48 89 74 24 20 57 48 83 EC 60 80 79 20 00");
    ok&=Bind(host,size,"48 89 54 24 08 0F 57 C0 F3 0F 10 54 24 08");
    ok&=Bind(host,layer,"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 50 48 8B 05 ?? ?? ?? ?? 45 8B D9 44 8B 8C 24 80 00 00 00 41 0F B6 D8");
    ok&=Bind(host,endLayer,"40 53 48 83 EC 20 48 89 74 24 38 48 8B D9 48 8B 35 ?? ?? ?? ?? 48 89 7C 24 40 48 8B 79 10 48 8B 09 E8");
    ok&=Bind(host,named,"48 89 5C 24 08 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 48 8B F9 48 8B DA 45 33 C0 48 8B 52 10 48 8B 48 18 48 89 0F 48 C7 47");
    ok&=Bind(host,opaque,"40 53 48 83 EC 20 F3 0F 10 0D ?? ?? ?? ?? 48 8B D9 48 8B 49 08 0F 28 C1 0F 14 C1 66 48 0F 7E C2 E8");
    ok&=Bind(host,label,"48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 50 48 8B DA 41 0F B6 F9");
    ok&=Bind(host,endLabel,"40 55 56 57 48 8D 6C 24 B9 48 81 EC F0 00 00 00 0F 29 BC 24 B0 00 00 00");
    ok&=Bind(host,draw,"4D 8B 09 45 8B 00 E9 ?? ?? ?? ?? CC");
    // Optional: without it the notices stay in English (or the ini's language).
    if(!Bind(host,loca,"48 89 5C 24 10 48 89 74 24 18 55 57 41 55 41 56 41 57 48 8D AC 24 F0 F8 FF FF"))
        Log("Hollow Halls Progression: game text lookup not found; notices use English or [Messages] Language");
    // Optional: without them the loading screen is dark, with its text.
    bool pictures=Bind(host,uiCtx,"48 8B 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC 4C 8B 41 28");
    pictures&=Bind(host,upload,"40 53 56 57 41 56 41 57 48 81 EC F0 00 00 00 48 83 7A 18 00 48 8B F2 48 8B F9");
    pictures&=Bind(host,drawSub,"48 83 EC 48 48 8B 41 08 48 8B 48 50 48 8B 49 18 48 85 C9 74 36 83 79 08 04");
    if(!pictures){upload=nullptr;Log("Hollow Halls Progression: UI picture upload not found; the loading screen has no picture");}
    ready=ok;return ok;
}
std::string GameText(uint32_t id){
    if(!loca||!id)return {};
    Text result=MakeText("");loca(&result,id);
    if(!result.bytes||!result.length||result.length>256)return {};
    return std::string(result.bytes,result.length);
}
text::Lang Language(){int f=forced.load();if(f<0)f=detected.load();return text::Lang(f>=0?f:0);}
void Show(std::string title,std::string message,uint64_t durationMs,bool typewriter){
    if(!showBanners)return;
    std::lock_guard guard(mutex);heading=std::move(title);body=std::move(message);
    started=GetTickCount64();until=started+(std::min)(durationMs,60000ULL);
    revealMs=typewriter?(std::min)(uint64_t(Codepoints(body))*16,durationMs/2):0;
}
void Clear(){std::lock_guard guard(mutex);until=0;heading.clear();body.clear();}
bool Visible(){std::lock_guard guard(mutex);return ready&&GetTickCount64()<until;}
void Draw() {
    if(!ready||!root||!*root)return;
    DetectLanguage(at<void*>(*root,200));
    if(const auto now=GetTickCount64();const float fade=LoadingAlpha(now)){
        auto assets=at<void*>(*root,200);if(!assets)return;
        for(int pass=0;pass<2;++pass){
            // The text goes on the notices' own layer (no notice is drawn meanwhile): one new layer only.
            OpenLayer frame(pass?"hollowHallsProgressionNotice":"hollowHallsProgressionLoading");
            auto rect=frame.rect();
            if(rect[2]>64&&rect[3]>64){
                if(pass)DrawLoadingText(assets,rect[0],rect[1],rect[2],rect[3],now,fade);
                else DrawLoadingPicture(rect[2],rect[3],now,fade);
            }
        }
        return; // the notices wait under the loading screen
    }
    if(!showBanners)return;
    std::string title,message;size_t revealed=0; // in code points
    {std::lock_guard guard(mutex);auto now=GetTickCount64();if(now>=until)return;title=heading;message=body;
        auto total=Codepoints(message);revealed=revealMs?(std::min)(total,size_t((now-started)*total/revealMs)):total;}
    auto assets=at<void*>(*root,200);if(!assets)return;
    OpenLayer frame("hollowHallsProgressionNotice");
    auto rect=frame.rect();
    float width=(std::min)(840.f,rect[2]-40.f),left=(rect[2]-width)*.5f;
    if(width>200) {
        auto lines=Wrap(message,(std::max)(size_t(10),size_t((width-48.f)/14.f)));
        {Box background(left,90,width,90+float(lines.size())*30);Fill(background,0xf0201711);}
        {Box accent(left,90,width,3);Fill(accent,0xff68cce8);}
        {Box row(left+24,110,width-48,36);Line(title,assets,0xff68cce8);}
        // Wrap the complete message first, keeping the window and words in
        // place while the body prints one character at a time.
        size_t offset=0,before=0,counted=0; // bytes; code points before offset; bytes counted
        for(size_t i=0;i<lines.size();++i){
            offset=message.find(lines[i],offset);if(offset==std::string::npos)break;
            before+=Codepoints(std::string_view(message).substr(counted,offset-counted));counted=offset;
            auto length=Codepoints(lines[i]);
            auto count=revealed>before?(std::min)(length,revealed-before):0;
            {Box row(left+24,156+float(i)*30,width-48,30);Line(lines[i],assets,0xffffffff,count);}
            offset+=lines[i].size();
        }
    }
}
void PrepareLoading(int hall){Request(hall);}
void ShowLoading(int hall,std::string caption){
    Request(hall);
    {std::lock_guard guard(mutex);loadingCaption=std::move(caption);}
    loadingHall=hall;loadingTarget=0;
    const auto now=GetTickCount64();
    if(!loading.exchange(true)){
        // Still fading out from an earlier journey: carry on from that darkness.
        const float left=LoadingAlpha(now);loadingChanged=now-uint64_t(left*kInMs);
        shownProgress=0;loadingSince=now;
    }
}
void SetLoadingProgress(float progress){if(progress>loadingTarget.load())loadingTarget=progress;}
void HideLoading(){if(loading.exchange(false))loadingChanged=GetTickCount64();}
bool LoadingShown(){return LoadingAlpha(GetTickCount64())>0;}
void StopLoading(){
    std::lock_guard guard(workerMutex);
    if(worker){worker->join();delete worker;worker=nullptr;}
    wanted=-1;loading=false;loadingChanged=0;
}
}

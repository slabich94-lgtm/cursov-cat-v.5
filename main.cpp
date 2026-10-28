#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <chrono>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"shell32.lib")

struct V3{float x,y,z;};
static V3 add(V3 a,V3 b){return{a.x+b.x,a.y+b.y,a.z+b.z};}
static V3 sub(V3 a,V3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
static V3 mul(V3 a,float s){return{a.x*s,a.y*s,a.z*s};}
static float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static V3 cross(V3 a,V3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static V3 norm(V3 a){float l=sqrtf(dot(a,a));return l>1e-6f?mul(a,1.0f/l):V3{0,1,0};}

struct M4{float m[16];};
static M4 ident(){M4 r{};r.m[0]=r.m[5]=r.m[10]=r.m[15]=1;return r;}
static M4 mulm(const M4&a,const M4&b){M4 r{};for(int c=0;c<4;c++)for(int row=0;row<4;row++)r.m[c*4+row]=a.m[0*4+row]*b.m[c*4+0]+a.m[1*4+row]*b.m[c*4+1]+a.m[2*4+row]*b.m[c*4+2]+a.m[3*4+row]*b.m[c*4+3];return r;}
static M4 scale(float x,float y,float z){M4 r=ident();r.m[0]=x;r.m[5]=y;r.m[10]=z;return r;}
static M4 trans(float x,float y,float z){M4 r=ident();r.m[12]=x;r.m[13]=y;r.m[14]=z;return r;}
static M4 rotX(float a){M4 r=ident();float c=cosf(a),s=sinf(a);r.m[5]=c;r.m[6]=s;r.m[9]=-s;r.m[10]=c;return r;}
static M4 rotY(float a){M4 r=ident();float c=cosf(a),s=sinf(a);r.m[0]=c;r.m[2]=-s;r.m[8]=s;r.m[10]=c;return r;}
static M4 rotZ(float a){M4 r=ident();float c=cosf(a),s=sinf(a);r.m[0]=c;r.m[1]=s;r.m[4]=-s;r.m[5]=c;return r;}
static M4 perspective(float fov,float aspect,float zn,float zf){M4 r{};float y=1/tanf(fov*.5f),x=y/aspect;r.m[0]=x;r.m[5]=y;r.m[10]=zf/(zf-zn);r.m[11]=1;r.m[14]=-zn*zf/(zf-zn);return r;}

struct Vertex{V3 p,n;};
struct Mesh{ID3D11Buffer* vb=nullptr;ID3D11Buffer* ib=nullptr;UINT count=0;};
struct Mat{float r,g,b,a;};

static ID3D11Device* dev=nullptr; static ID3D11DeviceContext* ctx=nullptr;
static ID3D11Texture2D* target=nullptr; static ID3D11Texture2D* staging=nullptr; static ID3D11RenderTargetView* rtv=nullptr;
static ID3D11VertexShader* vs=nullptr; static ID3D11PixelShader* ps=nullptr; static ID3D11InputLayout* layout=nullptr; static ID3D11Buffer* cb=nullptr; static ID3D11BlendState* blend=nullptr;
static Mesh sphere, cyl, coneMeshObj;
static HWND hwnd=nullptr; static NOTIFYICONDATAW tray{};
static const int W=96,H=96; static uint8_t pixels[W*H*4];
static float anim=0, posx=0, vel=0; static bool jumping=false; static float jumpT=0; static int jumpFrom=0,jumpTo=0; static DWORD lastTick=0; static std::vector<RECT> bars;

struct CBuf{M4 mvp;M4 model;float color[4];float light[3];float pad;};
static void releaseAll(){if(blend)blend->Release();if(cb)cb->Release();if(layout)layout->Release();if(vs)vs->Release();if(ps)ps->Release();if(rtv)rtv->Release();if(staging)staging->Release();if(target)target->Release();if(sphere.vb)sphere.vb->Release();if(sphere.ib)sphere.ib->Release();if(cyl.vb)cyl.vb->Release();if(cyl.ib)cyl.ib->Release();if(coneMeshObj.vb)coneMeshObj.vb->Release();if(coneMeshObj.ib)coneMeshObj.ib->Release();if(ctx)ctx->Release();if(dev)dev->Release();}

static Mesh makeMesh(const std::vector<Vertex>&v,const std::vector<uint32_t>&i){Mesh m{};D3D11_BUFFER_DESC bd{};bd.ByteWidth=(UINT)(v.size()*sizeof(Vertex));bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;D3D11_SUBRESOURCE_DATA sd{v.data(),0,0};if(FAILED(dev->CreateBuffer(&bd,&sd,&m.vb)))return{};bd.ByteWidth=(UINT)(i.size()*sizeof(uint32_t));bd.BindFlags=D3D11_BIND_INDEX_BUFFER;sd.pSysMem=i.data();if(FAILED(dev->CreateBuffer(&bd,&sd,&m.ib)))return{};m.count=(UINT)i.size();return m;}
static Mesh uvSphere(int seg,int rings){std::vector<Vertex>v;std::vector<uint32_t>i;for(int y=0;y<=rings;y++){float p=3.14159265f*y/rings;for(int x=0;x<=seg;x++){float t=6.2831853f*x/seg;V3 n{cosf(t)*sinf(p),cosf(p),sinf(t)*sinf(p)};v.push_back({n,n});}}for(int y=0;y<rings;y++)for(int x=0;x<seg;x++){uint32_t a=y*(seg+1)+x,b=a+1,c=a+(seg+1),d=c+1;i.insert(i.end(),{a,c,b,b,c,d});}return makeMesh(v,i);}
static Mesh cylinder(int seg){std::vector<Vertex>v;std::vector<uint32_t>i;for(int y=0;y<=1;y++){float yy=y-.5f;for(int x=0;x<=seg;x++){float t=6.2831853f*x/seg;V3 n{cosf(t),0,sinf(t)};v.push_back({{n.x,yy,n.z},n});}}for(int x=0;x<seg;x++){uint32_t a=x,b=x+1,c=seg+1+x,d=c+1;i.insert(i.end(),{a,c,b,b,c,d});}return makeMesh(v,i);}
static Mesh makeCone(int seg){std::vector<Vertex>v;std::vector<uint32_t>i;v.push_back({{0,.5f,0},{0,1,0}});for(int x=0;x<=seg;x++){float t=6.2831853f*x/seg;V3 n=norm({cosf(t),.65f,sinf(t)});v.push_back({{.5f*cosf(t),-.5f,.5f*sinf(t)},n});}for(int x=0;x<seg;x++)i.insert(i.end(),{0,(uint32_t)(1+x),(uint32_t)(1+x+1)});return makeMesh(v,i);}

static bool initD3D(){UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;D3D_FEATURE_LEVEL fl;D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,levels,2,D3D11_SDK_VERSION,&dev,&fl,&ctx)))return false;
 const char* HLSL="cbuffer C:register(b0){float4x4 mvp;float4x4 model;float4 color;float3 light;float pad;} struct VSIN{float3 p:POSITION;float3 n:NORMAL;};struct VSOUT{float4 p:SV_POSITION;float3 n:NORMAL;};VSOUT VS(VSIN v){VSOUT o;o.p=mul(mvp,float4(v.p,1));o.n=mul(model,float4(v.n,0)).xyz;return o;}float4 PS(VSOUT i):SV_Target{float3 n=normalize(i.n);float d=max(0,dot(n,normalize(light)));float rim=.25+(.75*d);return float4(color.rgb*rim,color.a);} ";ID3DBlob*b=nullptr,*e=nullptr;if(FAILED(D3DCompile(HLSL,strlen(HLSL),nullptr,nullptr,nullptr,"VS","vs_4_0",0,0,&b,&e)))return false;dev->CreateVertexShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&vs);D3D11_INPUT_ELEMENT_DESC id[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};dev->CreateInputLayout(id,2,b->GetBufferPointer(),b->GetBufferSize(),&layout);b->Release();if(e)e->Release();if(FAILED(D3DCompile(HLSL,strlen(HLSL),nullptr,nullptr,nullptr,"PS","ps_4_0",0,0,&b,&e)))return false;dev->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&ps);b->Release();if(e)e->Release();
 D3D11_BUFFER_DESC cbd{};cbd.ByteWidth=sizeof(CBuf);cbd.Usage=D3D11_USAGE_DYNAMIC;cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cbd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;dev->CreateBuffer(&cbd,nullptr,&cb);
 D3D11_TEXTURE2D_DESC td{};td.Width=W;td.Height=H;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;td.MiscFlags=D3D11_RESOURCE_MISC_GDI_COMPATIBLE; if(FAILED(dev->CreateTexture2D(&td,nullptr,&target))) {td.MiscFlags=0;if(FAILED(dev->CreateTexture2D(&td,nullptr,&target)))return false;} dev->CreateRenderTargetView(target,nullptr,&rtv);
 td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;td.MiscFlags=0;if(FAILED(dev->CreateTexture2D(&td,nullptr,&staging)))return false;
 D3D11_BLEND_DESC bd{};bd.RenderTarget[0].BlendEnable=TRUE;bd.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bd.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;dev->CreateBlendState(&bd,&blend);
 sphere=uvSphere(24,16);cyl=cylinder(16);coneMeshObj=makeCone(16);return sphere.vb&&cyl.vb&&coneMeshObj.vb;}

static M4 orientBetween(V3 a,V3 b,float radius,float length){V3 d=norm(sub(b,a));V3 up=fabsf(d.y)>.92f?V3{1,0,0}:V3{0,1,0};V3 x=norm(cross(up,d));V3 z=cross(d,x);M4 r=ident();r.m[0]=x.x*radius;r.m[1]=x.y*radius;r.m[2]=x.z*radius;r.m[4]=d.x*length;r.m[5]=d.y*length;r.m[6]=d.z*length;r.m[8]=z.x*radius;r.m[9]=z.y*radius;r.m[10]=z.z*radius;r.m[12]=(a.x+b.x)/2;r.m[13]=(a.y+b.y)/2;r.m[14]=(a.z+b.z)/2;return r;}
static M4 sphereX(V3 p,V3 s){return mulm(trans(p.x,p.y,p.z),scale(s.x,s.y,s.z));}
static void draw(const Mesh&m,const M4&model,const M4&vp,Mat mat){D3D11_MAPPED_SUBRESOURCE mr{};if(FAILED(ctx->Map(cb,0,D3D11_MAP_WRITE_DISCARD,0,&mr)))return;CBuf c{};c.mvp=mulm(model,vp);c.model=model;c.color[0]=mat.r;c.color[1]=mat.g;c.color[2]=mat.b;c.color[3]=mat.a;c.light[0]=-.35f;c.light[1]=.8f;c.light[2]=-.45f;memcpy(mr.pData,&c,sizeof(c));ctx->Unmap(cb,0);UINT s=sizeof(Vertex),off=0;ctx->IASetVertexBuffers(0,1,&m.vb,&s,&off);ctx->IASetIndexBuffer(m.ib,DXGI_FORMAT_R32_UINT,0);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vs,nullptr,0);ctx->PSSetShader(ps,nullptr,0);ctx->VSSetConstantBuffers(0,1,&cb);ctx->PSSetConstantBuffers(0,1,&cb);ctx->DrawIndexed(m.count,0,0);}

static void taskbars(){
 bars.clear();
 auto add=[&](HWND h){RECT r{};if(h&&IsWindowVisible(h)&&GetWindowRect(h,&r)&&r.right>r.left&&r.bottom>r.top)bars.push_back(r);};
 add(FindWindowW(L"Shell_TrayWnd",nullptr));
 HWND prev=nullptr;for(int i=0;i<16;i++){HWND h=FindWindowExW(nullptr,prev,L"Shell_SecondaryTrayWnd",nullptr);if(!h)break;add(h);prev=h;}
 if(bars.empty()){EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR, HDC, LPRECT r, LPARAM p)->BOOL{auto*v=(std::vector<RECT>*)p;RECT w=*r;v->push_back({w.left,w.bottom-48,w.right,w.bottom});return TRUE;},(LPARAM)&bars);}
 std::sort(bars.begin(),bars.end(),[](const RECT&a,const RECT&b){if(a.top!=b.top)return a.top<b.top;return a.left<b.left;});
}
static int barIndexForX(float x){for(int i=0;i<(int)bars.size();i++)if(x>=bars[i].left&&x<=bars[i].right)return i;float best=1e30f;int bi=0;for(int i=0;i<(int)bars.size();i++){float c=(bars[i].left+bars[i].right)*.5f,d=fabsf(x-c);if(d<best){best=d;bi=i;}}return bi;}
static void positionWindow(int bx){int i=std::clamp(barIndexForX(posx),0,(int)bars.size()-1);RECT b=bars[i];int x=(int)posx-W/2;int y=b.top-H+5;if(b.top>b.bottom-60)y=b.top-H+2;SetWindowPos(hwnd,HWND_TOPMOST,x,y,W,H,SWP_NOACTIVATE|SWP_SHOWWINDOW);}

static void render(){float t=anim;ctx->OMSetRenderTargets(1,&rtv,nullptr);float clear[4]={0,0,0,0};ctx->ClearRenderTargetView(rtv,clear);D3D11_VIEWPORT vpv{0,0,(float)W,(float)H,0,1};ctx->RSSetViewports(1,&vpv);float aspect=1;M4 proj=perspective(.72f,aspect,.1f,20);M4 view=mulm(trans(0,-.25f,-5.2f),rotY(0.0f));M4 vp=mulm(view,proj);
 Mat fur{.62f,.50f,.38f,1};Mat lightfur{.72f,.60f,.46f,1};Mat dark{.10f,.065f,.045f,1};Mat white{.95f,.92f,.86f,1};Mat pink{.85f,.42f,.45f,1};Mat eye{.015f,.012f,.009f,1};
 float gait=sinf(t*9.0f), gait2=sinf(t*9.0f+3.14159f); if(jumping){gait*=.2f;gait2*=.2f;}
 // Body/head
 float bob=(fabsf(vel)>.7f && !jumping)?fabsf(sinf(t*9.0f))*.045f:0;
 float face=(vel>=0?1.0f:-1.0f);
 draw(sphere,mulm(trans(0,bob,.0f),sphereX({0,.20f,0},{1.18f,.72f,.52f})),vp,fur);
 draw(sphere,mulm(trans(0,bob+.86f-.86f,0),mulm(trans(0,.86f,0),mulm(rotY(-face*.12f),scale(.67f,.62f,.60f)))),vp,fur);
 draw(sphere,sphereX({-.24f,.73f,.49f},{.29f,.23f,.18f}),vp,lightfur);draw(sphere,sphereX({.24f,.73f,.49f},{.29f,.23f,.18f}),vp,lightfur);draw(sphere,sphereX({0,.66f,.60f},{.11f,.08f,.07f}),vp,dark);
 // ears
 draw(coneMeshObj,mulm(trans(-.43f,1.30f,.02f),mulm(rotZ(-.18f),scale(.34f,.55f,.30f))),vp,fur);draw(coneMeshObj,mulm(trans(.43f,1.30f,.02f),mulm(rotZ(.18f),scale(.34f,.55f,.30f))),vp,fur);
 draw(coneMeshObj,mulm(trans(-.43f,1.28f,.27f),mulm(rotZ(-.18f),scale(.20f,.33f,.18f))),vp,pink);draw(coneMeshObj,mulm(trans(.43f,1.28f,.27f),mulm(rotZ(.18f),scale(.20f,.33f,.18f))),vp,pink);
 // eyes
 draw(sphere,sphereX({-.23f,.91f,.55f},{.075f,.075f,.055f}),vp,eye);draw(sphere,sphereX({.23f,.91f,.55f},{.075f,.075f,.055f}),vp,eye);draw(sphere,sphereX({-.25f,.935f,.598f},{.018f,.018f,.012f}),vp,white);draw(sphere,sphereX({.21f,.935f,.598f},{.018f,.018f,.012f}),vp,white);
 // legs, animated
 float stride=.20f;V3 hips[4]={{-.68f,.02f,-.30f},{.68f,.02f,-.30f},{-.68f,.02f,.28f},{.68f,.02f,.28f}};float ph[4]={gait,gait2,gait2,gait};for(int k=0;k<4;k++){float s=ph[k]*stride;V3 a=hips[k];V3 b{a.x+s,-.42f,a.z};V3 c{a.x+s*1.35f,-.72f,a.z+(k<2?-.05f:.05f)};draw(cyl,orientBetween(a,b,.115f,.48f),vp,fur);draw(cyl,orientBetween(b,c,.10f,.36f),vp,lightfur);draw(sphere,sphereX(c,{.15f,.09f,.20f}),vp,dark);}
 // tail segmented and swinging
 float sw=sinf(t*2.2f)*.25f;V3 tail0{1.00f,.35f,-.02f},tail1{1.35f,.55f+sw*.3f,-.02f},tail2{1.55f,.83f+sw,-.04f},tail3{1.65f,1.08f+sw*.8f,.02f};draw(cyl,orientBetween(tail0,tail1,.13f,.45f),vp,fur);draw(cyl,orientBetween(tail1,tail2,.11f,.38f),vp,fur);draw(cyl,orientBetween(tail2,tail3,.085f,.32f),vp,lightfur);
 ctx->CopyResource(staging,target);D3D11_MAPPED_SUBRESOURCE map{};if(SUCCEEDED(ctx->Map(staging,0,D3D11_MAP_READ,0,&map))){for(int y=0;y<H;y++)memcpy(pixels+y*W*4,(uint8_t*)map.pData+y*map.RowPitch,W*4);ctx->Unmap(staging,0);}
 // Soft CPU shadow at bottom, only where transparent. 
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){int dx=x-W/2,dy=y-(H-7);float e=(dx*dx)/(34.f*34.f)+(dy*dy)/(6.f*6.f);if(e<1){float a=(1-e)*55;uint8_t* p=&pixels[(y*W+x)*4];float na=a/255.f;float oa=p[3]/255.f;p[0]=(uint8_t)(p[0]*(1-na));p[1]=(uint8_t)(p[1]*(1-na));p[2]=(uint8_t)(p[2]*(1-na));p[3]=(uint8_t)std::min(255.f,255.f*(oa+na*(1-oa)));}}
}
static void present(){POINT pt;GetCursorPos(&pt);RECT rc;GetWindowRect(hwnd,&rc);int bx=barIndexForX((float)pt.x); // fast cursor response: react every frame, no polling delay
 int targetX=pt.x;int center=(bars[bx].left+bars[bx].right)/2;float dist=fabsf(pt.x-(rc.left+W/2));if(dist<520){float desired=std::clamp((float)pt.x,(float)bars[bx].left+W*.55f,(float)bars[bx].right-W*.55f);vel+=(desired-posx)*0.018f;vel=std::clamp(vel,-7.0f,7.0f);}else{vel+=sinf(anim*.65f)*.015f;}
 vel*=.88f;posx+=vel;RECT b=bars[barIndexForX(posx)];if(posx<b.left+W/2){posx=b.left+W/2;vel=fabsf(vel);}if(posx>b.right-W/2){posx=b.right-W/2;vel=-fabsf(vel);}static float lastX=posx;float dx=posx-lastX;lastX=posx; if(fabsf(dx)>.7f && !jumping)anim+=.018f; else anim+=.010f;
 if(!jumping && bars.size()>1 && fabsf(pt.x-(int)posx)<45 && (GetAsyncKeyState(VK_LBUTTON)&0x8000)){jumping=true;jumpT=0;jumpFrom=barIndexForX(posx);jumpTo=(jumpFrom+1)%(int)bars.size();}
 if(jumping){jumpT+=.028f;float u=jumpT;if(u>=1){u=1;jumping=false;}float a=bars[jumpFrom].left+W/2,b2=bars[jumpTo].left+W/2;posx=a+(b2-a)*u;if(u<.5f)posx=a+(b2-a)*(u*2);else posx=a+(b2-a)*((u));}
 render();positionWindow(0);
 POINT dst{rc.left,rc.top};SIZE sz{W,H};POINT src{0,0};HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen);BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=W;bi.bmiHeader.biHeight=-H;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;void* bits=nullptr;HBITMAP bm=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);memcpy(bits,pixels,sizeof(pixels));HBITMAP old=(HBITMAP)SelectObject(mem,bm);BLENDFUNCTION bf{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(hwnd,screen,&dst,&sz,mem,&src,0,&bf,ULW_ALPHA);SelectObject(mem,old);DeleteObject(bm);DeleteDC(mem);ReleaseDC(nullptr,screen);
}
static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_DESTROY){Shell_NotifyIconW(NIM_DELETE,&tray);PostQuitMessage(0);return 0;}if(m==WM_CONTEXTMENU){DestroyWindow(h);return 0;}return DefWindowProcW(h,m,w,l);}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE, PWSTR,int){SetProcessDPIAware();taskbars();if(bars.empty()){MessageBoxW(nullptr,L"CursorCat: не найдена панель задач.",L"CursorCat v5",MB_ICONERROR|MB_OK);return 3;}posx=(bars[0].left+bars[0].right)/2;lastTick=GetTickCount();WNDCLASSW wc{};wc.hInstance=hi;wc.lpfnWndProc=WndProc;wc.lpszClassName=L"CursorCatV5";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"CursorCatV5",L"CursorCat v5",WS_POPUP,0,0,W,H,nullptr,nullptr,hi,nullptr);if(!hwnd){MessageBoxW(nullptr,L"CursorCat: не удалось создать окно.",L"CursorCat v5",MB_ICONERROR|MB_OK);return 4;}SetWindowLongPtrW(hwnd,GWLP_USERDATA,1);tray.cbSize=sizeof(tray);tray.hWnd=hwnd;tray.uID=1;tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;tray.hIcon=LoadIcon(nullptr,IDI_APPLICATION);wcscpy_s(tray.szTip,L"CursorCat v5");Shell_NotifyIconW(NIM_ADD,&tray);if(!initD3D()){Shell_NotifyIconW(NIM_DELETE,&tray);MessageBoxW(nullptr,L"CursorCat: не удалось инициализировать Direct3D 11.

Скорее всего, проблема в графическом драйвере или создании 3D-ресурса.

Запусти exe ещё раз — теперь будет показано это сообщение вместо тихого выхода.",L"CursorCat v5",MB_ICONERROR|MB_OK);DestroyWindow(hwnd);return 2;}ShowWindow(hwnd,SW_SHOWNOACTIVATE);SetTimer(hwnd,1,8,nullptr);MSG msg;while(GetMessageW(&msg,nullptr,0,0)){if(msg.message==WM_TIMER){present();}TranslateMessage(&msg);DispatchMessageW(&msg);}releaseAll();return 0;}

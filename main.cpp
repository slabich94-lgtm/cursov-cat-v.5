#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include <cmath>

static HWND g_hwnd=nullptr; static NOTIFYICONDATAW g_tray{};
static std::vector<RECT> g_bars; static float g_x=400, g_v=0, g_phase=0; static DWORD g_last=0;
static const int W=180,H=140;
static bool g_follow=false;

static void GetBars(){
 g_bars.clear();
 auto add=[&](HWND h){RECT r{}; if(h&&IsWindowVisible(h)&&GetWindowRect(h,&r)&&r.right>r.left&&r.bottom>r.top) g_bars.push_back(r);};
 add(FindWindowW(L"Shell_TrayWnd",nullptr));
 HWND p=nullptr; for(int n=0;n<20;n++){HWND h=FindWindowExW(nullptr,p,L"Shell_SecondaryTrayWnd",nullptr); if(!h) break; add(h); p=h;}
 if(g_bars.empty()) EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR, HDC, LPRECT r, LPARAM p)->BOOL{auto*v=(std::vector<RECT>*)p;v->push_back({r->left,r->bottom-48,r->right,r->bottom});return TRUE;},(LPARAM)&g_bars);
 std::sort(g_bars.begin(),g_bars.end(),[](const RECT&a,const RECT&b){return a.left<b.left;});
}
static int BarForX(float x){int best=0;float bd=1e30f;for(int i=0;i<(int)g_bars.size();i++){if(x>=g_bars[i].left&&x<=g_bars[i].right)return i;float c=(g_bars[i].left+g_bars[i].right)*.5f,d=fabsf(x-c);if(d<bd){bd=d;best=i;}}return best;}
static void MoveWindowToBar(){if(g_bars.empty())return;int bi=BarForX(g_x);RECT b=g_bars[bi];int x=(int)g_x-W/2;int y=b.top-H+12;SetWindowPos(g_hwnd,HWND_TOPMOST,x,y,W,H,SWP_NOACTIVATE|SWP_SHOWWINDOW);}

static void EllipseFill(HDC dc,int l,int t,int r,int b,COLORREF c){HBRUSH br=CreateSolidBrush(c);HBRUSH old=(HBRUSH)SelectObject(dc,br);Ellipse(dc,l,t,r,b);SelectObject(dc,old);DeleteObject(br);}
static void Poly(HDC dc,POINT* p,int n,COLORREF c){HBRUSH br=CreateSolidBrush(c);HPEN pen=CreatePen(PS_SOLID,1,c);HBRUSH ob=(HBRUSH)SelectObject(dc,br);HPEN op=(HPEN)SelectObject(dc,pen);Polygon(dc,p,n);SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(br);DeleteObject(pen);}
static void Line(HDC dc,int x1,int y1,int x2,int y2,int w,COLORREF c){HPEN p=CreatePen(PS_SOLID,w,c);HPEN o=(HPEN)SelectObject(dc,p);MoveToEx(dc,x1,y1,nullptr);LineTo(dc,x2,y2);SelectObject(dc,o);DeleteObject(p);}

static void DrawCat(HDC dc){
 SetBkMode(dc,TRANSPARENT); RECT rr{0,0,W,H}; FillRect(dc,&rr,(HBRUSH)GetStockObject(BLACK_BRUSH));
 // body shadow
 EllipseFill(dc,38,73,143,124,RGB(12,12,12));
 // tail, behind body
 int wag=(int)(sinf(g_phase*1.7f)*7); Line(dc,130,88,158,72+wag,10,RGB(70,70,68)); Line(dc,132,89,158,72+wag,6,RGB(130,130,125));
 // body
 EllipseFill(dc,43,48,143,119,RGB(112,112,108));
 // chest highlight
 EllipseFill(dc,65,67,116,116,RGB(145,145,139));
 // legs with alternating animation
 int a=(int)(sinf(g_phase*7.0f)*5), b=(int)(sinf(g_phase*7.0f+3.14159f)*5);
 EllipseFill(dc,53+a,94,72+a,128,RGB(92,92,88)); EllipseFill(dc,106+b,94,125+b,128,RGB(92,92,88));
 EllipseFill(dc,72+b,94,91+b,127,RGB(125,125,120)); EllipseFill(dc,91+a,94,110+a,127,RGB(125,125,120));
 // head
 EllipseFill(dc,45,25,119,91,RGB(118,118,114));
 // ears
 POINT le[3]={{49,40},{52,12},{72,31}}; POINT re[3]={{92,31},{113,12},{115,43}}; Poly(dc,le,3,RGB(103,103,100)); Poly(dc,re,3,RGB(103,103,100));
 POINT li[3]={{55,34},{57,21},{68,33}}; POINT ri[3]={{96,33},{109,21},{110,37}}; Poly(dc,li,3,RGB(170,125,125)); Poly(dc,ri,3,RGB(170,125,125));
 // muzzle
 EllipseFill(dc,61,53,102,78,RGB(166,166,160));
 // eyes
 EllipseFill(dc,60,40,75,56,RGB(24,24,22)); EllipseFill(dc,89,40,104,56,RGB(24,24,22));
 EllipseFill(dc,65,43,70,51,RGB(205,220,175)); EllipseFill(dc,94,43,99,51,RGB(205,220,175));
 // nose + mouth
 Poly(dc,(POINT[]){{79,55},{86,55},{82,60}},3,RGB(55,42,40)); Line(dc,82,60,82,67,2,RGB(45,40,38)); Line(dc,82,67,74,70,2,RGB(45,40,38)); Line(dc,82,67,90,70,2,RGB(45,40,38));
 // whiskers
 Line(dc,65,62,35,58,1,RGB(205,205,198)); Line(dc,65,67,31,68,1,RGB(205,205,198)); Line(dc,99,62,128,58,1,RGB(205,205,198)); Line(dc,99,67,132,69,1,RGB(205,205,198));
 // collar
 Line(dc,62,78,103,78,4,RGB(55,75,85)); EllipseFill(dc,80,79,87,87,RGB(190,165,65));
}

static void Paint(){
 HDC screen=GetDC(nullptr), mem=CreateCompatibleDC(screen); HBITMAP bm=CreateCompatibleBitmap(screen,W,H); HBITMAP old=(HBITMAP)SelectObject(mem,bm); 
 // transparent bitmap via color key: use black as transparent, cat never needs black background
 HBRUSH bg=CreateSolidBrush(RGB(1,2,3)); RECT r{0,0,W,H};FillRect(mem,&r,bg);DeleteObject(bg); DrawCat(mem);
 // remove black-ish background by layered color key
 int bi=g_bars.empty()?0:BarForX(g_x); RECT bar=g_bars.empty()?RECT{0,0,1920,48}:g_bars[bi]; POINT dst{(LONG)(g_x-W/2),(LONG)(bar.top-H+12)}, src{0,0}; SIZE sz{W,H}; BLENDFUNCTION bf{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
 // We used an opaque bitmap; use color-key API instead of alpha
 SetLayeredWindowAttributes(g_hwnd,RGB(1,2,3),0,LWA_COLORKEY); UpdateLayeredWindow(g_hwnd,screen,&dst,&sz,mem,&src,0,&bf,ULW_COLORKEY);
 SelectObject(mem,old);DeleteObject(bm);DeleteDC(mem);ReleaseDC(nullptr,screen);
}

static void Tick(){
 DWORD now=GetTickCount(); float dt=(now-g_last)/1000.0f; if(dt<=0||dt>.05f)dt=.016f; g_last=now; g_phase+=dt;
 POINT p;GetCursorPos(&p); float dx=p.x-g_x; g_follow=fabsf(dx)<500;
 if(g_follow){float target=std::clamp((float)p.x,g_x-18.0f,g_x+18.0f);g_v+=(target-g_x)*18.0f*dt;}
 else g_v+=sinf(g_phase*.8f)*3.0f*dt;
 g_v*=powf(.001f,dt); g_v=std::clamp(g_v,-500.0f,500.0f); g_x+=g_v*dt;
 if(!g_bars.empty()){int bi=BarForX(g_x);RECT b=g_bars[bi];g_x=std::clamp(g_x,(float)b.left+W*.5f,(float)b.right-W*.5f);}
 MoveWindowToBar();Paint();
}
static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_TIMER){Tick();return 0;} if(m==WM_CONTEXTMENU){DestroyWindow(h);return 0;} if(m==WM_DESTROY){KillTimer(h,1);Shell_NotifyIconW(NIM_DELETE,&g_tray);PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE, PWSTR,int){SetProcessDPIAware();GetBars();if(g_bars.empty())return 2;RECT b=g_bars[0];g_x=(b.left+b.right)*.5f;WNDCLASSW wc{};wc.hInstance=hi;wc.lpfnWndProc=WndProc;wc.lpszClassName=L"CursorCatV6";RegisterClassW(&wc);g_hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"CursorCatV6",L"CursorCat v6",WS_POPUP,0,0,W,H,nullptr,nullptr,hi,nullptr);if(!g_hwnd)return 3;g_tray.cbSize=sizeof(g_tray);g_tray.hWnd=g_hwnd;g_tray.uID=6;g_tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;g_tray.hIcon=LoadIconW(nullptr,IDI_APPLICATION);lstrcpyW(g_tray.szTip,L"CursorCat v6");Shell_NotifyIconW(NIM_ADD,&g_tray);g_last=GetTickCount();MoveWindowToBar();ShowWindow(g_hwnd,SW_SHOWNOACTIVATE);SetTimer(g_hwnd,1,16,nullptr);Paint();MSG msg;while(GetMessageW(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;}

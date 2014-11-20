/****************************************************************************
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2013-2014 Chukong Technologies Inc.

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#include "base/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_WIN32

#include "platform/CCDevice.h"
#include "platform/CCFileUtils.h"
#include "CCStdC.h"
#include <unordered_map>

// Workaround for GdiPlus.h
using std::max;
using std::min;
#include <GdiPlus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace std;
using namespace Gdiplus;

NS_CC_BEGIN

int Device::getDPI()
{
    static int dpi = -1;
    if (dpi == -1)
    {
        HDC hScreenDC = GetDC( nullptr );
        int PixelsX = GetDeviceCaps( hScreenDC, HORZRES );
        int MMX = GetDeviceCaps( hScreenDC, HORZSIZE );
        ReleaseDC( nullptr, hScreenDC );   
        dpi = 254.0f*PixelsX/MMX/10;
    }
    return dpi;
}

void Device::setAccelerometerEnabled(bool isEnabled)
{}

void Device::setAccelerometerInterval(float interval)
{}

/*
class BitmapDC
{
public:


    bool setFont(const char * pFontName = nullptr, int nSize = 0)
    {
        bool bRet = false;
        do 
        {
            std::string fontName = pFontName;
            std::string fontPath;
            HFONT       hDefFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            LOGFONTA    tNewFont = {0};
            LOGFONTA    tOldFont = {0};
            GetObjectA(hDefFont, sizeof(tNewFont), &tNewFont);
            if (fontName.c_str())
            {    
                // create font from ttf file
                int nFindttf = fontName.find(".ttf");
                int nFindTTF = fontName.find(".TTF");
                if (nFindttf >= 0 || nFindTTF >= 0)
                {
                    fontPath = FileUtils::getInstance()->fullPathForFilename(fontName.c_str());
                    int nFindPos = fontName.rfind("/");
                    fontName = &fontName[nFindPos+1];
                    nFindPos = fontName.rfind(".");
                    fontName = fontName.substr(0,nFindPos);                
                }
                else
                {
                    auto nFindPos = fontName.rfind("/");
                    if (nFindPos != fontName.npos)
                    {
                        if (fontName.length() == nFindPos + 1)
                        {
                            fontName = "";
                        } 
                        else
                        {
                            fontName = &fontName[nFindPos+1];
                        }
                    }
                }
                tNewFont.lfCharSet = DEFAULT_CHARSET;
                strcpy_s(tNewFont.lfFaceName, LF_FACESIZE, fontName.c_str());
            }
            if (nSize)
            {
                tNewFont.lfHeight = -nSize;
            }
            GetObjectA(_font,  sizeof(tOldFont), &tOldFont);

            if (tOldFont.lfHeight == tNewFont.lfHeight
                && 0 == strcmp(tOldFont.lfFaceName, tNewFont.lfFaceName))
            {
                // already has the font 
                bRet = true;
                break;
            }

            // delete old font
            removeCustomFont();

            if (fontPath.size() > 0)
            {
                _curFontPath = fontPath;
                wchar_t * pwszBuffer = utf8ToUtf16(_curFontPath);
                if (pwszBuffer)
                {
                    if(AddFontResource(pwszBuffer))
                    {
                        SendMessage( _wnd, WM_FONTCHANGE, 0, 0);
                    }						
                    delete [] pwszBuffer;
                    pwszBuffer = nullptr;
                }
            }

            _font = nullptr;

            // disable Cleartype
            tNewFont.lfQuality = ANTIALIASED_QUALITY;

            // create new font
            _font = CreateFontIndirectA(&tNewFont);
            if (! _font)
            {
                // create failed, use default font
                _font = hDefFont;
                break;
            }

            bRet = true;
        } while (0);
        return bRet;
    }

    CC_SYNTHESIZE_READONLY(HDC, _DC, DC);
    CC_SYNTHESIZE_READONLY(HBITMAP, _bmp, Bitmap);
private:

    friend class Image;
    HFONT   _font;
    HWND    _wnd;
    std::string _curFontPath;

    void removeCustomFont() 
    {
        HFONT hDefFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        if (hDefFont != _font)
        {
            DeleteObject(_font);
            _font = hDefFont;
        }
        // release temp font resource	
        if (_curFontPath.size() > 0)
        {
            wchar_t * pwszBuffer = utf8ToUtf16(_curFontPath);
            if (pwszBuffer)
            {
                RemoveFontResource(pwszBuffer);
                SendMessage( _wnd, WM_FONTCHANGE, 0, 0);
                delete [] pwszBuffer;
                pwszBuffer = nullptr;
            }
            _curFontPath.clear();
        }	
    }
};

static BitmapDC& sharedBitmapDC()
{
    static BitmapDC s_BmpDC;
    return s_BmpDC;
}
*/


namespace {
    wstring utf8ToUtf16(const string & str)
    {
        wstring ret;
        do
        {
            if (str.empty())
            {
                break;
            }
            // utf-8 to utf-16
            int nLen = str.size();
            // utf-16 code units <= utf-8 code units
            ret.resize(nLen);
            nLen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), nLen, &ret[0], nLen + 1);		
            ret.resize(nLen);
        } while (0);
        return ret;
    }


    class FontCache {
    public:
        ~FontCache() {
            for (auto & f : _cache) {
                delete f.second;
            }
        }

        Font * getFont(const string & name, float size) {
            char buf[32];
            snprintf(buf, sizeof(buf), "@%.4g", size);
            string key = name + buf;

            auto it = _cache.find(key);
            if (it == _cache.end()) {
                // TODO: create font from file?
                Font * font = new Font(utf8ToUtf16(name).c_str(), size, FontStyleRegular, UnitPixel);
                _cache.insert(make_pair(key, font));
                return font;
            } else {
                return it->second;
            }
        }

    protected:
        unordered_map<string, Font *> _cache;
    };


    FontCache & sharedFontCache() {
        static FontCache cache;
        return cache;
    }
}


Data Device::getTextureDataForText(const char * text, const FontDefinition& textDefinition, TextAlign align, int &width, int &height, bool& hasPremultipliedAlpha)
{
    // FIXME: move to more suitable place?
    static struct GdiplusInit {
        ULONG_PTR gdiplusToken;
        GdiplusInit() {
            GdiplusStartupInput gdiplusStartupInput;
            GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
        }
        ~GdiplusInit() {
            GdiplusShutdown(gdiplusToken);
        }
    } gdiplusInit;

    Data ret;
    do
    {
        wstring wstr = utf8ToUtf16(text);

        Bitmap measureBitmap(1, 1, PixelFormat32bppARGB);
        Graphics measureGraphics(&measureBitmap);
        measureGraphics.SetTextRenderingHint(TextRenderingHintAntiAlias);

        Font * font = sharedFontCache().getFont(textDefinition._fontName, textDefinition._fontSize);
        SolidBrush brush(Gdiplus::Color::White);
        StringFormat stringFormat;
        Gdiplus::RectF layoutRect(0.0f, 0.0f, textDefinition._dimensions.width, textDefinition._dimensions.height);
        Gdiplus::RectF boundRect;

        switch (static_cast<unsigned>(align) & 0x03) {
        case 0x01: // left
            stringFormat.SetAlignment(StringAlignmentNear);
            layoutRect.X = 0.0f;
            break;
        case 0x02: // right
            stringFormat.SetAlignment(StringAlignmentFar);
            layoutRect.X = -layoutRect.Width;
            break;
        case 0x03: // center
        default:
            stringFormat.SetAlignment(StringAlignmentCenter);
            layoutRect.X = layoutRect.Width * -0.5f;
            break;
        }
        // y axis in GDI+ points downward
        switch (static_cast<unsigned>(align) & 0x30) {
        case 0x10: // top
            stringFormat.SetLineAlignment(StringAlignmentNear);
            layoutRect.Y = -layoutRect.Height;
            break;
        case 0x20: // bottom
            stringFormat.SetLineAlignment(StringAlignmentFar);
            layoutRect.Y = 0.0f;
            break;
        case 0x30: // center
        default:
            stringFormat.SetLineAlignment(StringAlignmentCenter);
            layoutRect.Y = textDefinition._dimensions.height * -0.5f;
            break;
        }

        measureGraphics.MeasureString(wstr.c_str(), -1, font, layoutRect, &stringFormat, &boundRect);
        CC_BREAK_IF(!(boundRect.Width > 0.0f && boundRect.Height > 0.0f));

        width = int(ceil(boundRect.Width));
        height = int(ceil(boundRect.Height));
        Bitmap bitmap(width, height, PixelFormat32bppARGB);
        Graphics graphics(&bitmap);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);

        graphics.DrawString(wstr.c_str(), -1, font, RectF(0.0f, 0.0f, width, height), &stringFormat, &brush);

        ssize_t dataLen = width * height * 4;
        Gdiplus::Rect rect(0, 0, width, height);
        BitmapData bitmapData;
        bitmap.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &bitmapData);
        auto dataBuf = new unsigned char[dataLen];
        for (int y = 0; y < height; y++) {
            auto lineIn = static_cast<const unsigned char *>(bitmapData.Scan0) + y * bitmapData.Stride;
            auto lineOut = dataBuf + y * (width * 4);
            memcpy(lineOut, lineIn, width * 4);
        }

        ret.fastSet(dataBuf,dataLen);
        hasPremultipliedAlpha = false;
    } while (0);

    return ret;
}

NS_CC_END

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_WIN32

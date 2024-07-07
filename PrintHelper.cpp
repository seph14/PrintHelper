#include "Postcard.h"
#include <iostream>
#include <winspool.h>
#include <atlbase.h>
#include <thread>
#include <functional>

using namespace std;

PrintHelperRef PrintHelper::create(HWND handle) {
	return PrintHelperRef(new PrintHelper(handle));
}

PrintHelper::PrintHelper(HWND handle) {
    mAppHandle  = handle;
    mInited     = false;
    mHDCPrint   = NULL;
    mDevMode    = NULL;
    mDriverName = NULL;
    mPortName   = NULL;
}

PrintHelper::~PrintHelper() {
    free(mDevMode);
    release();
}

void PrintHelper::release() {
    if (mHDCPrint != NULL) DeleteDC(mHDCPrint);
    mHDCPrint = NULL;
}

bool PrintHelper::createHDC() {
    if (!mInited) return false;

    LPSTR printername = const_cast<char*>(mPrinterName.c_str());
    mHDCPrint         = CreateDC(mDriverName, printername, mPortName, mDevMode);
    return mHDCPrint != NULL;
}

void PrintHelper::initPrinter(const std::string& targetPrinterName) {
    // reset init flag
    release ();
    free    (mDevMode);
    mPapers.clear();

    // windows should always have 1 Document to PDF printer available, no need to check against 0
    auto names   = getAllPrinterNames();
    mPrinterName = names.back();
    for (auto& name : names) {
        if (name.find(targetPrinterName) != std::string::npos) {
            mPrinterName = name;
            break;
        }
    }

    // fetch printer instance and devmode
    BOOL        bStatus  = FALSE;
    HANDLE      hPrinter = NULL;
    PRINTER_INFO_2* pPrinterData;
    
    // Open a handle to the printer. 
    LPSTR printername = const_cast<char*>(mPrinterName.c_str());
    bStatus           = OpenPrinter(printername, &hPrinter, NULL);

    if (bStatus) {
        // https://learn.microsoft.com/en-us/troubleshoot/windows/win32/modify-printer-settings-documentproperties
        
        //https://stackoverflow.com/questions/62095797/how-to-programmatically-print-without-prompting-for-filename-in-mfc-and-cview-us
        long dwNeeded = DocumentProperties(mAppHandle, hPrinter, printername, NULL, NULL, 0);   
        mDevMode      = (LPDEVMODE)malloc(dwNeeded);
        if (mDevMode == NULL) return;
        auto dwRet    = DocumentProperties(mAppHandle, hPrinter, printername, mDevMode,  NULL, DM_OUT_BUFFER);
        if (dwRet != IDOK) {
            free        (mDevMode);
            ClosePrinter(hPrinter);
            return;
        }

        // merge dev mode if necessary
        dwRet            = DocumentProperties(mAppHandle, hPrinter, printername,
            mDevMode, mDevMode, DM_IN_BUFFER | DM_OUT_BUFFER);

        BYTE*   pdBuffer = new BYTE[16384];
        DWORD   cbBuf    = 16384;
        DWORD   cbNeeded = 0;

        // get the printer port name
        bStatus          = GetPrinter(
            hPrinter, 2, &pdBuffer[0], cbBuf, &cbNeeded);
        pPrinterData     = (PRINTER_INFO_2*)&pdBuffer[0];
        ClosePrinter(hPrinter);

        if (bStatus && pPrinterData != NULL) {
            mDriverName = pPrinterData->pDriverName;
            mPortName   = pPrinterData->pPortName;

            // retrieve supported paper sizes
            int nPapersCount = DeviceCapabilities(printername, mPortName, DC_PAPERS, NULL, NULL);
            if (nPapersCount > 0) {
                WORD*  typeBuf = new WORD[nPapersCount];
                POINT* sizeBuf = new POINT[nPapersCount];
                DeviceCapabilities(printername, mPortName, DC_PAPERS,    (LPTSTR)typeBuf, NULL);
                DeviceCapabilities(printername, mPortName, DC_PAPERSIZE, (LPTSTR)sizeBuf, NULL);
                
                for (int i = 0; i < nPapersCount; i++) {
                    auto& size = sizeBuf[i];
                    mPapers.push_back(PaperSize{ (short)typeBuf[i], rectCompMili(size.x/10.f,size.y/10.f) });
                }

                delete[] sizeBuf;
                delete[] typeBuf;
            }

            mInited     = true; 
            std::cout << "[PrintHelper] context fetched: " << mPrinterName << std::endl;
        }
    
        delete[] pdBuffer;
    }
}

void PrintHelper::printRaw(const std::filesystem::path& filepath, uint8_t* data, const uint32_t& ppi,
                        const bool& hasAlpha, const uint32_t& width, const uint32_t& height) {

    if (!mInited) {
        std::cout << "[PrintHelper] Printer not inited, call initPrinter method first" << std::endl;
        return;
    }

    {
        BITMAPINFOHEADER bmih;
        bmih.biSize          = sizeof(BITMAPINFOHEADER);
        bmih.biWidth         = width;
        bmih.biHeight        = - height;
        bmih.biPlanes        = 1;
        bmih.biBitCount      = hasAlpha ? 32 : 24;
        bmih.biCompression   = BI_RGB;
        bmih.biSizeImage     = 0;
        bmih.biXPelsPerMeter = (LONG)(39.3701f * ppi); //pixels in meters
        bmih.biYPelsPerMeter = (LONG)(39.3701f * ppi);
        bmih.biClrUsed       = 0;
        bmih.biClrImportant  = 0;

        BITMAPINFO dbmi;
        ZeroMemory(&dbmi, sizeof(dbmi));
        dbmi.bmiHeader              = bmih;
        dbmi.bmiColors->rgbBlue     = 0;
        dbmi.bmiColors->rgbGreen    = 0;
        dbmi.bmiColors->rgbRed      = 0;
        dbmi.bmiColors->rgbReserved = 0;
     
        HBITMAP hbmp = CreateDIBitmap(mHDCPrint, &bmih, CBM_INIT, data, &dbmi, DIB_RGB_COLORS);
        
        if (hbmp != NULL) {
            std::cout << "[PrintHelper] Bitmap creation succeed" << std::endl;

            auto drawHdc = CreateCompatibleDC(mHDCPrint);
            SelectObject(drawHdc, hbmp);

            DOCINFO docInfo;
            docInfo.lpszDocName  = (LPTSTR)_T("graphic");
            string path          = filepath.generic_string();
            LPSTR pathname       = const_cast<char*>(path.c_str());
            docInfo.lpszOutput   = filepath.empty() ? NULL : pathname;
            docInfo.lpszDatatype = NULL;
            docInfo.fwType       = 0;
            docInfo.cbSize       = sizeof(DOCINFO);
            
            {
                const std::size_t cxPage = ::GetDeviceCaps(mHDCPrint, HORZRES);
                const std::size_t cyPage = ::GetDeviceCaps(mHDCPrint, VERTRES);

                StartDoc(mHDCPrint, &docInfo);
                StartPage(mHDCPrint);
                auto res = StretchBlt(mHDCPrint, 0, 0, cxPage, cyPage, drawHdc, 0, 0, width, height, SRCCOPY);
                EndPage(mHDCPrint);
                EndDoc(mHDCPrint);
            }

            DeleteDC(drawHdc);
        } else std::cout << "[PrintHelper] Bitmap creation failed" << std::endl;
    }
}

float PrintHelper::rectCompInch(const float& w, const float& h) {
    return w * h * (w > h ? 1.f : -1.f);
}

float PrintHelper::rectCompMili(const float& w, const float& h) {
    return w / 25.4f * (h / 25.4f) * (w > h ? 1.f : -1.f);
}

short PrintHelper::getClosestPaperSize(const float& w, const float& h) {
    float inc  = rectCompInch(w, h);
    float diff = 65535.f;
    /*
    vector<PaperSize> paperSize = {
        // inches
        {DMPAPER_TABLOID,       rectCompInch(11.f,  17.f)},
        {DMPAPER_TABLOID_EXTRA, rectCompInch(11.69f,18.f)},
        {DMPAPER_LETTER,        rectCompInch(8.5f,  11.f)},
        {DMPAPER_LEGAL,         rectCompInch(8.5f,  14.f)},
        {DMPAPER_9X11,          rectCompInch(9.f,   11.f)},
        {DMPAPER_10X11,         rectCompInch(10.f,  11.f)},
        {DMPAPER_10X14,         rectCompInch(10.f,14.f)},
        {DMPAPER_15X11,         rectCompInch(15.f,11.f)},
        {DMPAPER_11X17,         rectCompInch(11.f,17.f)},
        {DMPAPER_12X11,         rectCompInch(12.f,11.f)},

        // millimeters
        {DMPAPER_A2,            rectCompMili(420.f, 594.f)},
        {DMPAPER_A3,            rectCompMili(420.f, 594.f)},
        {DMPAPER_A3_EXTRA,      rectCompMili(322.f, 445.f)},
        {DMPAPER_A3_ROTATED,    rectCompMili(420.f, 297.f)},
        
        {DMPAPER_A4,            rectCompMili(210.f, 297.f)},
        {DMPAPER_A4_EXTRA,      rectCompInch(9.27f, 12.69f)},
        {DMPAPER_A4_PLUS,       rectCompMili(210.f, 330.f)},
        {DMPAPER_A4_ROTATED,    rectCompMili(297.f, 210.f)},
        
        {DMPAPER_A5,            rectCompMili(148.f, 210.f)},
        {DMPAPER_A5_EXTRA,      rectCompMili(174.f, 235.f)},
        {DMPAPER_A5_ROTATED,    rectCompMili(210.f, 148.f)},
        {DMPAPER_A6,            rectCompMili(105.f, 148.f)},
        {DMPAPER_A6_ROTATED,    rectCompMili(148.f, 105.f)}
    };*/

    short type = DMPAPER_USER;
    for (auto& paper : mPapers) {
        float d = inc - paper.area;
        if (d < .0f) d = -d;
        if (d < diff) {
            diff = d;
            type = paper.type;
        }
    }

    return type;
}

void PrintHelper::print(uint8_t* data, const uint32_t& width, const uint32_t& height,
                     const std::filesystem::path& filepath, const bool& resizePaper, const uint32_t& ppi) {
    if (!mInited) {
        std::cout << "[PrintHelper] Printer not inited, call initPrinter method first" << std::endl;
        return;
    }

    if (!createHDC()) {
        std::cout << "[PrintHelper] Printer HDC creation failed" << std::endl;
        return;
    }
    
    if (resizePaper) {
        // devmode params
        // https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-devmodea

        float ww = (float)width / ppi;
        float hh = (float)height / ppi;
        mDevMode->dmPrintQuality = ppi;
        mDevMode->dmFields |= DM_PAPERSIZE | DM_PRINTQUALITY;

        // retrive supported paper sizes
        //https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-devicecapabilitiesa?redirectedfrom=MSDN

        mDevMode->dmPaperSize = getClosestPaperSize(ww, hh);// DMPAPER_LETTER;// DMPAPER_USER;
        auto hdc = ResetDC(mHDCPrint, mDevMode);
        if (hdc == mHDCPrint)  std::cout << "[PrintHelper] Printer setting update success" << std::endl;
    }

    // print on worker thread
    printRaw(filepath, data, ppi, true, width, height);
    release ();
}

std::vector<std::string> PrintHelper::getAllPrinterNames() {
    DWORD dwNeeded = 0, dwReturned = 0;
    PRINTER_INFO_1* pInfo = NULL;
    vector<string> res;

    auto fnReturn = EnumPrinters(
        PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
        NULL, 1L, (LPBYTE)NULL, 0L, &dwNeeded, &dwReturned);

    if (dwNeeded > 0) {
        pInfo = (PRINTER_INFO_1*)HeapAlloc(
            GetProcessHeap(), 0L, (size_t)dwNeeded);
    }

    if (NULL != pInfo) {
        dwReturned = 0;
        fnReturn = EnumPrinters(
            PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
            NULL, 1L, (LPBYTE)pInfo,
            dwNeeded, &dwNeeded, &dwReturned);
    
        if (fnReturn) {
            for (size_t i = 0; i < (size_t)dwReturned; i++) 
                res.push_back(pInfo[i].pName);
        }
    }

    return res;
}
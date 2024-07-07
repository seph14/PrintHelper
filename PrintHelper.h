#pragma once
#include <windows.h>
#include <memory>
#include <vector>
#include <filesystem>

class PrintHelper;
typedef std::shared_ptr<PrintHelper> PrintHelperRef;

//https://learn.microsoft.com/en-us/windows/win32/printdocs/xps-printing
//https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdca
//https://learn.microsoft.com/en-us/windows/win32/printdocs/tailored-app-printing-api

class PrintHelper {
protected:
	struct PaperSize {
		short type;
		float area;
	};
	std::vector<PaperSize> mPapers;

	std::string mPrinterName;
	HWND		mAppHandle;
	HDC			mHDCPrint;
	LPDEVMODE	mDevMode;
	LPSTR		mDriverName, mPortName;
	bool		mInited;

	PrintHelper(HWND handle);

	float   rectCompInch		(const float& w, const float& h);
	float   rectCompMili		(const float& w, const float& h);
	short	getClosestPaperSize	(const float& w, const float& h);
	
	void	printRaw	(const std::filesystem::path& filepath, uint8_t* data, const uint32_t& ppi, 
						 const bool& hasAlpha, const uint32_t& width, const uint32_t& height);
	void	release		();
	bool	createHDC	();

public:
	~PrintHelper();
	static PrintHelperRef create(HWND handle);

	void initPrinter (const std::string& targetPrinterName);

	// data need to be BGRA layout, for somereason without alpha channel GDI api do not generate correct result
	void print		 (uint8_t* data, const uint32_t& width, const uint32_t& height, 
					  const std::filesystem::path& filepath, const bool& resizePaper = true, const uint32_t& ppi = 300);
	
	static std::vector<std::string> getAllPrinterNames();
};
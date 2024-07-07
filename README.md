# PrintHelper
c++17 helper class that print a raw image data directly from code on Windows.

#### no dependency, only requires c++ 17 and windows api
#### the project need to use multi-byte character set instead of unicode, otherwise `LPSTR` need to change accordingly. 

#### sample code
1. create instance
   ```c++
   auto printer = PrinHelper::create((HWND)your_app_window_instance);
   ```
   
2. init printer
   If you need to test, create windows file to PDF converter:
   ```c++
   printer->initPrinter("PDF");
   ```

   Otherwise if you have a HP printer:
   ```c++
   printer->initPrinter("HP");
   ```

   Supplied string will be used to match available printers on Windows and init the cooresponding one.

4. print
   ```c++
   printer->print(rawImgData, imgWidthInPixel, imgHeightInPixel, 
					        outFilepath, resizePaperSize, ppi);
   ```

   where `rawImgData` is a pointer of uint8_t* points to the image data, with BGRA channel order;    
   `imgWidthInPixel` and `imgHeightInPixel` are the pixel size of the image.

   If `outFilepath` is empty, the generated image will be printed directly, other it will be saved to a file. I.e. if you want to test the file, set it to something like `D:test.pdf`.

   If `resizePaperSize` is true, the printer's paper size will be adjusted to the closest one available according to the image size and the ppi supplied.

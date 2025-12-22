/*****************************************************************************
* | File      	:   GUI_BMPfile.h
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*                Used to shield the underlying layers of each master
*                and enhance portability
*----------------
* |	This version:   V2.3
* | Date        :   2022-07-27
* | Info        :   
* -----------------------------------------------------------------------------
* V2.3(2022-07-27):
* 1.Add GUI_ReadBmp_RGB_4Color()
* V2.2(2020-07-08):
* 1.Add GUI_ReadBmp_RGB_7Color()
* V2.1(2019-10-10):
* 1.Add GUI_ReadBmp_4Gray()
* V2.0(2018-11-12):
* 1.Change file name: GUI_BMP.h -> GUI_BMPfile.h
* 2.fix: GUI_ReadBmp()
*   Now Xstart and Xstart can control the position of the picture normally,
*   and support the display of images of any size. If it is larger than
*   the actual display range, it will not be displayed.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/

#include "GUI_BMPfile.h"
#include "GUI_Paint.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>	//exit()
#include <string.h> //memset()
#include <math.h> //memset()
#include <stdio.h>
//#include "test_decoder.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"

static const char *TAG = "GUI_BMPfile";


UBYTE GUI_ReadBmp(const char *path, UWORD Xstart, UWORD Ystart)
{
    FILE *fp;                     //Define a file pointer
    BMPFILEHEADER bmpFileHeader;  //Define a bmp file header structure
    BMPINFOHEADER bmpInfoHeader;  //Define a bmp info header structure


    // Binary file open
    if((fp = fopen(path, "rb")) == NULL) {
        ESP_LOGE(TAG,"Cann't open the file!");
        exit(0);
    }

    // Set the file pointer from the beginning
    fseek(fp, 0, SEEK_SET);
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 14
    fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 50
    printf("pixel = %ld * %ld\r\n", bmpInfoHeader.biWidth, bmpInfoHeader.biHeight);

    UWORD Image_Width_Byte = (bmpInfoHeader.biWidth % 8 == 0)? (bmpInfoHeader.biWidth / 8): (bmpInfoHeader.biWidth / 8 + 1);
    UWORD Bmp_Width_Byte = (Image_Width_Byte % 4 == 0) ? Image_Width_Byte: ((Image_Width_Byte / 4 + 1) * 4);
    UBYTE *Image = (UBYTE *)heap_caps_malloc(Image_Width_Byte * bmpInfoHeader.biHeight * sizeof(UBYTE), MALLOC_CAP_SPIRAM);
    
    memset(Image, 0xFF, Image_Width_Byte * bmpInfoHeader.biHeight);

    // Determine if it is a monochrome bitmap
    int readbyte = bmpInfoHeader.biBitCount;
    if(readbyte != 1) {
        ESP_LOGE(TAG,"the bmp Image is not a monochrome bitmap!");
        exit(0);
    }

    // Determine black and white based on the palette
    UWORD i;
    UWORD Bcolor, Wcolor;
    UWORD bmprgbquadsize = pow(2, bmpInfoHeader.biBitCount);// 2^1 = 2
    BMPRGBQUAD bmprgbquad[bmprgbquadsize];        //palette
    // BMPRGBQUAD bmprgbquad[2];        //palette

    for(i = 0; i < bmprgbquadsize; i++){
    // for(i = 0; i < 2; i++) {
        fread(&bmprgbquad[i], sizeof(BMPRGBQUAD), 1, fp);
    }
    if(bmprgbquad[0].rgbBlue == 0xff && bmprgbquad[0].rgbGreen == 0xff && bmprgbquad[0].rgbRed == 0xff) {
        Bcolor = BLACK;
        Wcolor = WHITE;
    } else {
        Bcolor = WHITE;
        Wcolor = BLACK;
    }

    // Read image data into the cache
    UWORD x, y;
    UBYTE Rdata;
    fseek(fp, bmpFileHeader.bOffset, SEEK_SET);
    for(y = 0; y < bmpInfoHeader.biHeight; y++) {//Total display column
        for(x = 0; x < Bmp_Width_Byte; x++) {//Show a line in the line
            if(fread((char *)&Rdata, 1, readbyte, fp) != readbyte) {
                perror("get bmpdata:\r\n");
                break;
            }
            if(x < Image_Width_Byte) { //bmp
                Image[x + (bmpInfoHeader.biHeight - y - 1) * Image_Width_Byte] =  Rdata;
                // printf("rdata = %d\r\n", Rdata);
            }
        }
    }
    fclose(fp);

    // Refresh the image to the display buffer based on the displayed orientation
    UBYTE color, temp;
    for(y = 0; y < bmpInfoHeader.biHeight; y++) {
        for(x = 0; x < bmpInfoHeader.biWidth; x++) {
            if(x > Paint.Width || y > Paint.Height) {
                break;
            }
            temp = Image[(x / 8) + (y * Image_Width_Byte)];
            color = (((temp << (x%8)) & 0x80) == 0x80) ?Bcolor:Wcolor;
            Paint_SetPixel(Xstart + x, Ystart + y, color);
        }
    }
    heap_caps_free(Image);
    Image = NULL;
    return 0;
}
/*************************************************************************

*************************************************************************/
UBYTE GUI_ReadBmp_4Gray(const char *path, UWORD Xstart, UWORD Ystart)
{
    FILE *fp;                     //Define a file pointer
    BMPFILEHEADER bmpFileHeader;  //Define a bmp file header structure
    BMPINFOHEADER bmpInfoHeader;  //Define a bmp info header structure
    
    // Binary file open
    if((fp = fopen(path, "rb")) == NULL) {
        ESP_LOGE(TAG,"Cann't open the file!");
        exit(0);
    }
 
    // Set the file pointer from the beginning
    fseek(fp, 0, SEEK_SET);
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 14
    fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 50
    printf("pixel = %ld * %ld\r\n", bmpInfoHeader.biWidth, bmpInfoHeader.biHeight);

    UWORD Image_Width_Byte = (bmpInfoHeader.biWidth % 4 == 0)? (bmpInfoHeader.biWidth / 4): (bmpInfoHeader.biWidth / 4 + 1);
    UWORD Bmp_Width_Byte = (bmpInfoHeader.biWidth % 2 == 0)? (bmpInfoHeader.biWidth / 2): (bmpInfoHeader.biWidth / 2 + 1);
    UBYTE Image[Image_Width_Byte * bmpInfoHeader.biHeight * 2];
    memset(Image, 0xFF, Image_Width_Byte * bmpInfoHeader.biHeight * 2);

    // Determine if it is a monochrome bitmap
    int readbyte = bmpInfoHeader.biBitCount;
    printf("biBitCount = %d\r\n",readbyte);
    if(readbyte != 4){
        ESP_LOGE(TAG,"Bmp image is not a 4-color bitmap!");
        exit(0);
    }
    // Read image data into the cache
    UWORD x, y;
    UBYTE Rdata;
    fseek(fp, bmpFileHeader.bOffset, SEEK_SET);
    
    for(y = 0; y < bmpInfoHeader.biHeight; y++) {//Total display column
        for(x = 0; x < Bmp_Width_Byte; x++) {//Show a line in the line
            if(fread((char *)&Rdata, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
            }
            if(x < Image_Width_Byte*2) { //bmp
                Image[x + (bmpInfoHeader.biHeight - y - 1) * Image_Width_Byte*2] =  Rdata;
            }
        }
    }
    fclose(fp);
    
    // Refresh the image to the display buffer based on the displayed orientation
    UBYTE color, temp;
    printf("bmpInfoHeader.biWidth = %ld\r\n",bmpInfoHeader.biWidth);
    printf("bmpInfoHeader.biHeight = %ld\r\n",bmpInfoHeader.biHeight);
    for(y = 0; y < bmpInfoHeader.biHeight; y++) {
        for(x = 0; x < bmpInfoHeader.biWidth; x++) {
            if(x > Paint.Width || y > Paint.Height) {
                break;
            }
            temp = Image[x/2 + y * bmpInfoHeader.biWidth/2] >> ((x%2)? 0:4);//0xf 0x8 0x7 0x0 
            color = temp>>2;                           //11  10  01  00  
            Paint_SetPixel(Xstart + x, Ystart + y, color);
        }
    }
    return 0;
}

UBYTE GUI_ReadBmp_16Gray(const char *path, UWORD Xstart, UWORD Ystart)
{
    FILE *fp;                     //Define a file pointer
    BMPFILEHEADER bmpFileHeader;  //Define a bmp file header structure
    BMPINFOHEADER bmpInfoHeader;  //Define a bmp info header structure

    // Binary file open
    if((fp = fopen(path, "rb")) == NULL) {
        ESP_LOGE(TAG,"Cann't open the file!");
        exit(0);
    }

    // Set the file pointer from the beginning
    fseek(fp, 0, SEEK_SET);
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 14
    fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 50
    printf("pixel = %ld * %ld\r\n", bmpInfoHeader.biWidth, bmpInfoHeader.biHeight);

    // They are both the same width in bytes
    // round up to the next byte
    UWORD Width_Byte = (bmpInfoHeader.biWidth + 1) / 2;
    UBYTE Image[Width_Byte * bmpInfoHeader.biHeight];
    memset(Image, 0xFF, Width_Byte * bmpInfoHeader.biHeight);

    // Determine if it is a monochrome bitmap
    int readbyte = bmpInfoHeader.biBitCount;
    printf("biBitCount = %d\r\n",readbyte);
    if(readbyte != 4) {
        ESP_LOGE(TAG,"Bmp image is not a 4-bit bitmap!");
        exit(0);
    }

    // Determine colors based on the palette

    // A map from palette entry to color
    UBYTE colors[16];
    UBYTE i;
    BMPRGBQUAD rgbData;

    for (i = 0; i < 16; i++){
        fread(&rgbData, sizeof(BMPRGBQUAD), 1, fp);

        // Work out the closest colour
        // 16 colours over 0-255 => 0-8 => 0, 9-25 => 1 (17), 26-42 => 2 (34), etc

        // Base it on red
        colors[i] = (rgbData.rgbRed + 8) / 17;
    }

    // Read image data into the cache
    UWORD x, y;
    UBYTE Rdata;
    fseek(fp, bmpFileHeader.bOffset, SEEK_SET);

    for (y = 0; y < bmpInfoHeader.biHeight; y++) {//Total display column
        for (x = 0; x < Width_Byte; x++) {//Show a line in the line
            if (fread((char *) &Rdata, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
            }
            Image[x + (bmpInfoHeader.biHeight - y - 1) * Width_Byte] = Rdata;
        }
    }
    fclose(fp);

    // Refresh the image to the display buffer based on the displayed orientation
    UBYTE coloridx;
    printf("bmpInfoHeader.biWidth = %ld\r\n", bmpInfoHeader.biWidth);
    printf("bmpInfoHeader.biHeight = %ld\r\n", bmpInfoHeader.biHeight);
    for (y = 0; y < bmpInfoHeader.biHeight; y++) {
        for (x = 0; x < bmpInfoHeader.biWidth; x++) {
            if (Xstart + x > Paint.Width || Ystart + y > Paint.Height)
                break;

            coloridx = (Image[x / 2 + y * Width_Byte] >> ((x % 2) ? 0 : 4)) & 15;
            Paint_SetPixel(Xstart + x, Ystart + y, colors[coloridx]);
        }
    }
    return 0;
}

UBYTE GUI_ReadBmp_RGB_7Color(const char *path, UWORD Xstart, UWORD Ystart)
{
    FILE *fp;                     //Define a file pointer
    BMPFILEHEADER bmpFileHeader;  //Define a bmp file header structure
    BMPINFOHEADER bmpInfoHeader;  //Define a bmp info header structure
    
    // Binary file open
    if((fp = fopen(path, "rb")) == NULL) {
        ESP_LOGE(TAG,"Cann't open the file!");
        exit(0);
    }

    // Set the file pointer from the beginning
    fseek(fp, 0, SEEK_SET);
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 14
    fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 50
    printf("pixel = %ld * %ld\r\n", bmpInfoHeader.biWidth, bmpInfoHeader.biHeight);
	
    UDOUBLE Image_Byte = bmpInfoHeader.biWidth * bmpInfoHeader.biHeight * 3;
    UBYTE Image[Image_Byte];
    memset(Image, 0xFF, Image_Byte);

    // Determine if it is a monochrome bitmap
    int readbyte = bmpInfoHeader.biBitCount;
    if(readbyte != 24){
        ESP_LOGE(TAG,"Bmp image is not 24 bitmap!");
        exit(0);
    }
    // Read image data into the cache
    UWORD x, y;
    UBYTE Rdata[3];
    fseek(fp, bmpFileHeader.bOffset, SEEK_SET);
    
    for(y = 0; y < bmpInfoHeader.biHeight; y++) {//Total display column
        for(x = 0; x < bmpInfoHeader.biWidth ; x++) {//Show a line in the line
            if(fread((char *)Rdata, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
            }
			if(fread((char *)Rdata+1, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
            }
			if(fread((char *)Rdata+2, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
            }

			if(Rdata[0] == 0 && Rdata[1] == 0 && Rdata[2] == 0){
				Image[x+(y* bmpInfoHeader.biWidth )] =  0;//Black
			}else if(Rdata[0] == 255 && Rdata[1] == 255 && Rdata[2] == 255){
				Image[x+(y* bmpInfoHeader.biWidth )] =  1;//White
			}else if(Rdata[0] == 0 && Rdata[1] == 255 && Rdata[2] == 0){
				Image[x+(y* bmpInfoHeader.biWidth )] =  2;//Green
			}else if(Rdata[0] == 255 && Rdata[1] == 0 && Rdata[2] == 0){
				Image[x+(y* bmpInfoHeader.biWidth )] =  3;//Blue
			}else if(Rdata[0] == 0 && Rdata[1] == 0 && Rdata[2] == 255){
				Image[x+(y* bmpInfoHeader.biWidth )] =  4;//Red
			}else if(Rdata[0] == 0 && Rdata[1] == 255 && Rdata[2] == 255){
				Image[x+(y* bmpInfoHeader.biWidth )] =  5;//Yellow
			}else if(Rdata[0] == 0 && Rdata[1] == 128 && Rdata[2] == 255){
				Image[x+(y* bmpInfoHeader.biWidth )] =  6;//Orange
			}
        }
    }
    fclose(fp);
   
    // Refresh the image to the display buffer based on the displayed orientation
    for(y = 0; y < bmpInfoHeader.biHeight; y++) {
        for(x = 0; x < bmpInfoHeader.biWidth; x++) {
            if(x > Paint.Width || y > Paint.Height) {
                break;
            }
            Paint_SetPixel(Xstart + x, Ystart + y, Image[bmpInfoHeader.biHeight *  bmpInfoHeader.biWidth - 1 -(bmpInfoHeader.biWidth-x-1+(y* bmpInfoHeader.biWidth))]);
		}
    }
    return 0;
}

UBYTE GUI_ReadBmp_RGB_4Color(const char *path, UWORD Xstart, UWORD Ystart)
{
    FILE *fp;                     //Define a file pointer
    BMPFILEHEADER bmpFileHeader;  //Define a bmp file header structure
    BMPINFOHEADER bmpInfoHeader;  //Define a bmp info header structure
    
    // Binary file open
    if((fp = fopen(path, "rb")) == NULL) {
        ESP_LOGE(TAG,"Cann't open the file!");
        exit(0);
    }

    // Set the file pointer from the beginning
    fseek(fp, 0, SEEK_SET);
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 14
    fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 50
    printf("pixel = %ld * %ld\r\n", bmpInfoHeader.biWidth, bmpInfoHeader.biHeight);
	
    UDOUBLE Image_Byte = bmpInfoHeader.biWidth * bmpInfoHeader.biHeight * 3;
    UBYTE Image[Image_Byte];
    memset(Image, 0xFF, Image_Byte);

    // Determine if it is a monochrome bitmap
    int readbyte = bmpInfoHeader.biBitCount;
    if(readbyte != 24){
        ESP_LOGE(TAG,"Bmp image is not 24 bitmap!");
        exit(0);
    }
    // Read image data into the cache
    UWORD x, y;
    UBYTE Rdata[3];
    fseek(fp, bmpFileHeader.bOffset, SEEK_SET);
    
    for(y = 0; y < bmpInfoHeader.biHeight; y++) {//Total display column
        for(x = 0; x < bmpInfoHeader.biWidth ; x++) {//Show a line in the line
            if(fread((char *)Rdata, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
            }
			if(fread((char *)Rdata+1, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
            }
			if(fread((char *)Rdata+2, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
            }
			if(Rdata[0] < 128 && Rdata[1] < 128 && Rdata[2] < 128){
				Image[x+(y* bmpInfoHeader.biWidth )] =  0;//Black
			}else if(Rdata[0] > 127 && Rdata[1] > 127 && Rdata[2] > 127){
				Image[x+(y* bmpInfoHeader.biWidth )] =  1;//White
			}else if(Rdata[0] < 128 && Rdata[1] > 127 && Rdata[2] > 127){
				Image[x+(y* bmpInfoHeader.biWidth )] =  2;//Yellow
			}else if(Rdata[0] < 128 && Rdata[1] < 128 && Rdata[2] > 127){
				Image[x+(y* bmpInfoHeader.biWidth )] =  3;//Red
			}
        }
        if(bmpInfoHeader.biWidth % 4 != 0)
        {
            for (UWORD i = 0; i < (bmpInfoHeader.biWidth % 4); i++)
            {
                if(fread((char *)Rdata, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
                }
            }
        }
    }
    fclose(fp);
   
    // Refresh the image to the display buffer based on the displayed orientation
    for(y = 0; y < bmpInfoHeader.biHeight; y++) {
        for(x = 0; x < bmpInfoHeader.biWidth; x++) {
            if(x > Paint.Width || y > Paint.Height) {
                break;
            }
            Paint_SetPixel(Xstart + x, Ystart + y, Image[bmpInfoHeader.biHeight *  bmpInfoHeader.biWidth - 1 -(bmpInfoHeader.biWidth-x-1+(y* bmpInfoHeader.biWidth))]);
		}
    }
    return 0;
}
#if 1
UBYTE GUI_ReadBmp_RGB_6Color(const char *path, UWORD Xstart, UWORD Ystart)
{
#if 0
    FILE *fp;                     //Define a file pointer
    BMPFILEHEADER bmpFileHeader;  //Define a bmp file header structure
    BMPINFOHEADER bmpInfoHeader;  //Define a bmp info header structure
    
    // Binary file open
    if((fp = fopen(path, "rb")) == NULL) {
        ESP_LOGE(TAG,"Cann't open the file!");
        return 0;
    }

    // Set the file pointer from the beginning
    fseek(fp, 0, SEEK_SET);
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 14
    fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 50
    printf("pixel = %ld * %ld\r\n", bmpInfoHeader.biWidth, bmpInfoHeader.biHeight);
	
    UDOUBLE Image_Byte = bmpInfoHeader.biWidth * bmpInfoHeader.biHeight * 3;

    UBYTE *Image = (UBYTE *)heap_caps_malloc(1000 * sizeof(uint8_t), MALLOC_CAP_8BIT);    
    //for(int i = 0; i<Image_Byte; i++)
    memset(Image, 0xFF, Image_Byte);
    return 0;
#else
    FILE *fp;                     //Define a file pointer
    BMPFILEHEADER bmpFileHeader;  //Define a bmp file header structure
    BMPINFOHEADER bmpInfoHeader;  //Define a bmp info header structure
    
    // Binary file open
    if((fp = fopen(path, "rb")) == NULL) {
        ESP_LOGE(TAG,"Cann't open the file!");
        return 0;
    }

    // Set the file pointer from the beginning
    fseek(fp, 0, SEEK_SET);
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 14
    fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp);    //sizeof(BMPFILEHEADER) must be 50
    printf("pixel = %ld * %ld\r\n", bmpInfoHeader.biWidth, bmpInfoHeader.biHeight);
	
    UDOUBLE Image_Byte = bmpInfoHeader.biWidth * bmpInfoHeader.biHeight * 3;

    UBYTE *Image = (UBYTE *)heap_caps_malloc(Image_Byte * sizeof(uint8_t), MALLOC_CAP_SPIRAM);    
    //for(int i = 0; i<Image_Byte; i++)
    //memset(Image, 0xFF, Image_Byte);
    //ESP_LOGE("FILE","%ld",Image_Byte);
    // Determine if it is a monochrome bitmap
    int readbyte = bmpInfoHeader.biBitCount;
    if(readbyte != 24){
        ESP_LOGE(TAG,"Bmp image is not 24 bitmap!");
        exit(0);
    }
    // Read image data into the cache
    UWORD x, y;
    UBYTE Rdata[3];
    fseek(fp, bmpFileHeader.bOffset, SEEK_SET);
    
    for(y = 0; y < bmpInfoHeader.biHeight; y++) {//Total display column
        for(x = 0; x < bmpInfoHeader.biWidth ; x++) {//Show a line in the line
            if(fread((char *)Rdata, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
            }
			if(fread((char *)Rdata+1, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
            }
			if(fread((char *)Rdata+2, 1, 1, fp) != 1) {
                perror("get bmpdata:\r\n");
                break;
            }

			if(Rdata[0] == 0 && Rdata[1] == 0 && Rdata[2] == 0){
				Image[x+(y* bmpInfoHeader.biWidth )] =  0;//Black
			}else if(Rdata[0] == 255 && Rdata[1] == 255 && Rdata[2] == 255){
				Image[x+(y* bmpInfoHeader.biWidth )] =  1;//White
			}else if(Rdata[0] == 0 && Rdata[1] == 255 && Rdata[2] == 255){
				Image[x+(y* bmpInfoHeader.biWidth )] =  2;//Yellow
			}else if(Rdata[0] == 0 && Rdata[1] == 0 && Rdata[2] == 255){
				Image[x+(y* bmpInfoHeader.biWidth )] =  3;//Red
			// }else if(Rdata[0] == 0 && Rdata[1] == 128 && Rdata[2] == 255){
			// 	Image[x+(y* bmpInfoHeader.biWidth )] =  4;//Orange
			}else if(Rdata[0] == 255 && Rdata[1] == 0 && Rdata[2] == 0){
				Image[x+(y* bmpInfoHeader.biWidth )] =  5;//Blue
			}else if(Rdata[0] == 0 && Rdata[1] == 255 && Rdata[2] == 0){
				Image[x+(y* bmpInfoHeader.biWidth )] =  6;//Green
			}
        }
    }
    fclose(fp);
   
    // Refresh the image to the display buffer based on the displayed orientation.
    // Yield periodically to avoid starving lower-priority tasks / triggering the task watchdog.
    for(y = 0; y < bmpInfoHeader.biHeight; y++) {
        if ((y % 16) == 0 && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        for(x = 0; x < bmpInfoHeader.biWidth; x++) {
            if(x > Paint.Width || y > Paint.Height) {
                break;
            }
            Paint_SetPixel(Xstart + x, Ystart + y, Image[bmpInfoHeader.biHeight *  bmpInfoHeader.biWidth - 1 -(bmpInfoHeader.biWidth-x-1+(y* bmpInfoHeader.biWidth))]);

		}
    }
    heap_caps_free(Image);
    Image = NULL;
    return 0;
#endif
}

static UWORD GUI_NormalizeRotate(UWORD Rotate)
{
    if (Rotate == ROTATE_0 || Rotate == ROTATE_90 || Rotate == ROTATE_180 || Rotate == ROTATE_270)
    {
        return Rotate;
    }
    return ROTATE_0;
}

UBYTE GUI_ReadBmp_RGB_6Color_Rotate(const char *path, UWORD Xstart, UWORD Ystart, UWORD srcRotate, UWORD dstRotate)
{
    FILE *fp;
    BMPFILEHEADER bmpFileHeader;
    BMPINFOHEADER bmpInfoHeader;

    fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Can't open file: %s", path);
        return 0;
    }

    if (fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp) != 1 ||
        fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp) != 1) {
        ESP_LOGE(TAG, "Failed to read BMP header");
        fclose(fp);
        return 0;
    }

    if (bmpInfoHeader.biBitCount != 24) {
        ESP_LOGE(TAG, "Bmp image is not 24-bit!");
        fclose(fp);
        return 0;
    }

    const UWORD width = (UWORD)bmpInfoHeader.biWidth;
    const UWORD height = (UWORD)bmpInfoHeader.biHeight;
    const int rowSize = ((width * 3 + 3) & ~3);

    UBYTE *rowBuf = (UBYTE *)heap_caps_malloc(rowSize, MALLOC_CAP_SPIRAM);
    UBYTE *Image = (UBYTE *)heap_caps_malloc((size_t)width * (size_t)height, MALLOC_CAP_SPIRAM);

    if (!rowBuf || !Image) {
        ESP_LOGE(TAG, "Memory allocation failed!");
        if (rowBuf) heap_caps_free(rowBuf);
        if (Image) heap_caps_free(Image);
        fclose(fp);
        return 0;
    }

    if (fseek(fp, bmpFileHeader.bOffset, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "fseek failed");
        heap_caps_free(rowBuf);
        heap_caps_free(Image);
        fclose(fp);
        return 0;
    }

    // Read rows (BMP bottom-up) into a top-down color-index image.
    for (UWORD y = 0; y < height; y++) {
        const size_t got = fread(rowBuf, 1, rowSize, fp);
        if (got != (size_t)rowSize) {
            ESP_LOGE(TAG, "BMP read error at line %u (got %zu, want %u)", (unsigned)y, got, (unsigned)rowSize);
            break;
        }

        const UBYTE *p = rowBuf;
        for (UWORD x = 0; x < width; x++) {
            const UBYTE d0 = *p++; // Blue
            const UBYTE d1 = *p++; // Green
            const UBYTE d2 = *p++; // Red

            UBYTE color;
            if (d0 == 0 && d1 == 0 && d2 == 0) {
                color = 0; // Black
            } else if (d0 == 255 && d1 == 255 && d2 == 255) {
                color = 1; // White
            } else if (d0 == 0 && d1 == 255 && d2 == 255) {
                color = 2; // Yellow
            } else if (d0 == 0 && d1 == 0 && d2 == 255) {
                color = 3; // Red
            } else if (d0 == 255 && d1 == 0 && d2 == 0) {
                color = 5; // Blue
            } else if (d0 == 0 && d1 == 255 && d2 == 0) {
                color = 6; // Green
            } else {
                color = 1; // Default white
            }

            Image[(size_t)(height - 1 - y) * (size_t)width + x] = color;
        }
    }

    fclose(fp);

    const int src = (int)GUI_NormalizeRotate(srcRotate);
    const int dst = (int)GUI_NormalizeRotate(dstRotate);
    int delta = (dst - src) % 360;
    if (delta < 0) delta += 360;

    UWORD outW = width;
    UWORD outH = height;
    if (delta == 90 || delta == 270) {
        outW = height;
        outH = width;
    }

    // Draw (rotating from src -> dst) into the current Paint coordinate system.
    // Yield periodically to avoid starving lower-priority tasks / triggering the task watchdog.
    for (UWORD y = 0; y < outH; y++) {
        if ((y % 16) == 0 && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if ((UWORD)(Ystart + y) >= Paint.Height) break;
        for (UWORD x = 0; x < outW; x++) {
            if ((UWORD)(Xstart + x) >= Paint.Width) break;

            int sx = 0;
            int sy = 0;

            switch (delta) {
            case 0:
                sx = x;
                sy = y;
                break;
            case 90:
                // Rotate clockwise 90: out(x,y) reads from src(y, height-1-x)
                sx = (int)y;
                sy = (int)height - 1 - (int)x;
                break;
            case 180:
                sx = (int)width - 1 - (int)x;
                sy = (int)height - 1 - (int)y;
                break;
            case 270:
                // Rotate clockwise 270 (= counter-clockwise 90): out(x,y) reads from src(width-1-y, x)
                sx = (int)width - 1 - (int)y;
                sy = (int)x;
                break;
            default:
                sx = x;
                sy = y;
                break;
            }

            if (sx < 0 || sy < 0 || sx >= (int)width || sy >= (int)height) {
                continue;
            }

            const UBYTE color = Image[(size_t)sy * (size_t)width + (size_t)sx];
            Paint_SetPixel(Xstart + x, Ystart + y, color);
        }
    }

    heap_caps_free(rowBuf);
    heap_caps_free(Image);
    return 0;
}

bool GUI_Bmp_GetDimensions(const char *path, int *out_width, int *out_height)
{
    if (out_width)
        *out_width = 0;
    if (out_height)
        *out_height = 0;

    if (!path || !out_width || !out_height)
    {
        return false;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        ESP_LOGE(TAG, "Can't open file: %s", path);
        return false;
    }

    BMPFILEHEADER bmpFileHeader;
    BMPINFOHEADER bmpInfoHeader;

    if (fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp) != 1 ||
        fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp) != 1)
    {
        ESP_LOGE(TAG, "Failed to read BMP header");
        fclose(fp);
        return false;
    }

    fclose(fp);

    if (bmpFileHeader.bType != 0x4D42)
    {
        // Not a BMP.
        return false;
    }

    if (bmpInfoHeader.biWidth <= 0 || bmpInfoHeader.biHeight == 0)
    {
        return false;
    }

    // BMP height can be negative (top-down). Treat as absolute size.
    const int w = (int)bmpInfoHeader.biWidth;
    const int h = (bmpInfoHeader.biHeight < 0) ? (int)(-bmpInfoHeader.biHeight) : (int)bmpInfoHeader.biHeight;

    *out_width = w;
    *out_height = h;
    return true;
}

static inline UWORD GUI_MinUword(UWORD a, UWORD b)
{
    return (a < b) ? a : b;
}

static inline float GUI_Clamp255f(float v)
{
    if (v < 0.0f)
        return 0.0f;
    if (v > 255.0f)
        return 255.0f;
    return v;
}

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t paint;
} GUI_PaletteEntry;

static const GUI_PaletteEntry GUI_PALETTE_6[6] = {
    {0, 0, 0, 0},         // black
    {255, 255, 255, 1},   // white
    {255, 255, 0, 2},     // yellow
    {255, 0, 0, 3},       // red
    {0, 0, 255, 5},       // blue (note: paint index 5)
    {0, 255, 0, 6},       // green
};

static inline uint8_t GUI_ClosestPaletteColor6(float r, float g, float b)
{
    int best = 0;
    int bestD = 0x7FFFFFFF;
    for (int i = 0; i < 6; i++)
    {
        const int dr = (int)r - (int)GUI_PALETTE_6[i].r;
        const int dg = (int)g - (int)GUI_PALETTE_6[i].g;
        const int db = (int)b - (int)GUI_PALETTE_6[i].b;
        const int d = dr * dr + dg * dg + db * db;
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return GUI_PALETTE_6[best].paint;
}

UBYTE GUI_DrawBmp_RGB_6Color_Fit(const char *path, UWORD Xstart, UWORD Ystart, UWORD boxW, UWORD boxH, bool allow_upscale)
{
    FILE *fp;
    BMPFILEHEADER bmpFileHeader;
    BMPINFOHEADER bmpInfoHeader;

    fp = fopen(path, "rb");
    if (!fp)
    {
        ESP_LOGE(TAG, "Can't open file: %s", path);
        return 0;
    }

    if (fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp) != 1 ||
        fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp) != 1)
    {
        ESP_LOGE(TAG, "Failed to read BMP header");
        fclose(fp);
        return 0;
    }

    if (bmpInfoHeader.biBitCount != 24)
    {
        ESP_LOGE(TAG, "Bmp image is not 24-bit!");
        fclose(fp);
        return 0;
    }

    const int width = (int)bmpInfoHeader.biWidth;
    const int height_abs = (bmpInfoHeader.biHeight < 0) ? (int)(-bmpInfoHeader.biHeight) : (int)bmpInfoHeader.biHeight;

    if (width <= 0 || height_abs <= 0)
    {
        fclose(fp);
        return 0;
    }

    const UWORD srcW = (UWORD)width;
    const UWORD srcH = (UWORD)height_abs;

    const int rowSize = (((int)srcW * 3 + 3) & ~3);

    UBYTE *rowBuf = (UBYTE *)heap_caps_malloc((size_t)rowSize, MALLOC_CAP_SPIRAM);
    uint8_t *srcRgb = (uint8_t *)heap_caps_malloc((size_t)srcW * (size_t)srcH * 3, MALLOC_CAP_SPIRAM);

    if (!rowBuf || !srcRgb)
    {
        ESP_LOGE(TAG, "Memory allocation failed!");
        if (rowBuf)
            heap_caps_free(rowBuf);
        if (srcRgb)
            heap_caps_free(srcRgb);
        fclose(fp);
        return 0;
    }

    if (fseek(fp, bmpFileHeader.bOffset, SEEK_SET) != 0)
    {
        ESP_LOGE(TAG, "fseek failed");
        heap_caps_free(rowBuf);
        heap_caps_free(srcRgb);
        fclose(fp);
        return 0;
    }

    // Decode BMP into a top-down RGB888 buffer.
    for (UWORD y = 0; y < srcH; y++)
    {
        const size_t got = fread(rowBuf, 1, (size_t)rowSize, fp);
        if (got != (size_t)rowSize)
        {
            ESP_LOGE(TAG, "BMP read error at line %u (got %zu, want %u)", (unsigned)y, got, (unsigned)rowSize);
            break;
        }

        // BMP is bottom-up unless height is negative.
        const UWORD dstY = (bmpInfoHeader.biHeight < 0) ? y : (UWORD)(srcH - 1 - y);
        uint8_t *dst = srcRgb + ((size_t)dstY * (size_t)srcW * 3);

        const UBYTE *p = rowBuf;
        for (UWORD x = 0; x < srcW; x++)
        {
            const uint8_t b = *p++;
            const uint8_t g = *p++;
            const uint8_t r = *p++;
            dst[(size_t)x * 3 + 0] = r;
            dst[(size_t)x * 3 + 1] = g;
            dst[(size_t)x * 3 + 2] = b;
        }
    }

    fclose(fp);

    // Compute fit-scaled output size.
    UWORD outW = boxW;
    UWORD outH = boxH;

    if (!allow_upscale && srcW <= boxW && srcH <= boxH)
    {
        outW = srcW;
        outH = srcH;
    }
    else
    {
        const uint64_t left = (uint64_t)srcW * (uint64_t)boxH;
        const uint64_t right = (uint64_t)srcH * (uint64_t)boxW;

        if (left > right)
        {
            outW = boxW;
            outH = (UWORD)((uint64_t)srcH * (uint64_t)boxW / (uint64_t)srcW);
        }
        else
        {
            outH = boxH;
            outW = (UWORD)((uint64_t)srcW * (uint64_t)boxH / (uint64_t)srcH);
        }

        if (outW == 0)
            outW = 1;
        if (outH == 0)
            outH = 1;

        outW = GUI_MinUword(outW, boxW);
        outH = GUI_MinUword(outH, boxH);
    }

    const int dx0 = (int)Xstart + (int)(boxW - outW) / 2;
    const int dy0 = (int)Ystart + (int)(boxH - outH) / 2;

    // Floyd–Steinberg dithering in destination space.
    float *errR = (float *)malloc(sizeof(float) * ((size_t)outW + 2));
    float *errG = (float *)malloc(sizeof(float) * ((size_t)outW + 2));
    float *errB = (float *)malloc(sizeof(float) * ((size_t)outW + 2));

    float *nextErrR = (float *)malloc(sizeof(float) * ((size_t)outW + 2));
    float *nextErrG = (float *)malloc(sizeof(float) * ((size_t)outW + 2));
    float *nextErrB = (float *)malloc(sizeof(float) * ((size_t)outW + 2));

    if (!errR || !errG || !errB || !nextErrR || !nextErrG || !nextErrB)
    {
        if (errR)
            free(errR);
        if (errG)
            free(errG);
        if (errB)
            free(errB);
        if (nextErrR)
            free(nextErrR);
        if (nextErrG)
            free(nextErrG);
        if (nextErrB)
            free(nextErrB);

        heap_caps_free(rowBuf);
        heap_caps_free(srcRgb);
        return 0;
    }

    memset(errR, 0, sizeof(float) * ((size_t)outW + 2));
    memset(errG, 0, sizeof(float) * ((size_t)outW + 2));
    memset(errB, 0, sizeof(float) * ((size_t)outW + 2));
    memset(nextErrR, 0, sizeof(float) * ((size_t)outW + 2));
    memset(nextErrG, 0, sizeof(float) * ((size_t)outW + 2));
    memset(nextErrB, 0, sizeof(float) * ((size_t)outW + 2));

    for (UWORD y = 0; y < outH; y++)
    {
        if ((y % 16) == 0 && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        const UWORD py = (UWORD)(dy0 + (int)y);
        if (py >= Paint.Height)
            break;

        const UWORD sy = (UWORD)((uint64_t)y * (uint64_t)srcH / (uint64_t)outH);
        const uint8_t *srcRow = srcRgb + ((size_t)sy * (size_t)srcW * 3);

        for (UWORD x = 0; x < outW; x++)
        {
            const UWORD px = (UWORD)(dx0 + (int)x);
            if (px >= Paint.Width)
                break;

            const UWORD sx = (UWORD)((uint64_t)x * (uint64_t)srcW / (uint64_t)outW);
            const uint8_t *sp = srcRow + ((size_t)sx * 3);

            float r = (float)sp[0] + errR[x];
            float g = (float)sp[1] + errG[x];
            float b = (float)sp[2] + errB[x];

            r = GUI_Clamp255f(r);
            g = GUI_Clamp255f(g);
            b = GUI_Clamp255f(b);

            // Quantize
            const uint8_t paint = GUI_ClosestPaletteColor6(r, g, b);
            Paint_SetPixel(px, py, paint);

            // Find the palette RGB we used (for error calc).
            uint8_t pr = 255, pg = 255, pb = 255;
            for (int i = 0; i < 6; i++)
            {
                if (GUI_PALETTE_6[i].paint == paint)
                {
                    pr = GUI_PALETTE_6[i].r;
                    pg = GUI_PALETTE_6[i].g;
                    pb = GUI_PALETTE_6[i].b;
                    break;
                }
            }

            const float er = r - (float)pr;
            const float eg = g - (float)pg;
            const float eb = b - (float)pb;

            // Right pixel (7/16)
            if (x + 1 < outW)
            {
                errR[x + 1] += er * (7.0f / 16.0f);
                errG[x + 1] += eg * (7.0f / 16.0f);
                errB[x + 1] += eb * (7.0f / 16.0f);
            }

            // Next row
            if (y + 1 < outH)
            {
                if (x > 0)
                {
                    nextErrR[x - 1] += er * (3.0f / 16.0f);
                    nextErrG[x - 1] += eg * (3.0f / 16.0f);
                    nextErrB[x - 1] += eb * (3.0f / 16.0f);
                }

                nextErrR[x] += er * (5.0f / 16.0f);
                nextErrG[x] += eg * (5.0f / 16.0f);
                nextErrB[x] += eb * (5.0f / 16.0f);

                if (x + 1 < outW)
                {
                    nextErrR[x + 1] += er * (1.0f / 16.0f);
                    nextErrG[x + 1] += eg * (1.0f / 16.0f);
                    nextErrB[x + 1] += eb * (1.0f / 16.0f);
                }
            }
        }

        // Swap row buffers.
        float *tmp;
        tmp = errR;
        errR = nextErrR;
        nextErrR = tmp;
        memset(nextErrR, 0, sizeof(float) * ((size_t)outW + 2));

        tmp = errG;
        errG = nextErrG;
        nextErrG = tmp;
        memset(nextErrG, 0, sizeof(float) * ((size_t)outW + 2));

        tmp = errB;
        errB = nextErrB;
        nextErrB = tmp;
        memset(nextErrB, 0, sizeof(float) * ((size_t)outW + 2));
    }

    free(errR);
    free(errG);
    free(errB);
    free(nextErrR);
    free(nextErrG);
    free(nextErrB);

    heap_caps_free(rowBuf);
    heap_caps_free(srcRgb);
    return 0;
}
#else

UBYTE GUI_ReadBmp_RGB_6Color(const char *path, UWORD Xstart, UWORD Ystart)
{
    FILE *fp;
    BMPFILEHEADER bmpFileHeader;
    BMPINFOHEADER bmpInfoHeader;

    fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Can't open file: %s", path);
        return 0;
    }

    // 读取头
    if (fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp) != 1 ||
        fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp) != 1) {
        ESP_LOGE(TAG, "Failed to read BMP header");
        fclose(fp);
        return 0;
    }
    printf("pixel = %ld * %ld\r\n", bmpInfoHeader.biWidth, bmpInfoHeader.biHeight);

    if (bmpInfoHeader.biBitCount != 24) {
        ESP_LOGE(TAG, "Bmp image is not 24-bit!");
        fclose(fp);
        return 0;
    }

    UWORD width = (UWORD)bmpInfoHeader.biWidth;
    UWORD height = (UWORD)bmpInfoHeader.biHeight;
    int rowSize = ((width * 3 + 3) & ~3); // 每行4字节对齐

    // 行缓冲与颜色索引缓冲（每像素1字节）
    UBYTE *rowBuf = (UBYTE *)heap_caps_malloc(rowSize, MALLOC_CAP_SPIRAM);
    UBYTE *Image = (UBYTE *)heap_caps_malloc(width * height, MALLOC_CAP_SPIRAM);

    if (!rowBuf || !Image) {
        ESP_LOGE(TAG, "Memory allocation failed!");
        if (rowBuf) heap_caps_free(rowBuf);
        if (Image) heap_caps_free(Image);
        fclose(fp);
        return 0;
    }

    // 移动到像素数据区
    if (fseek(fp, bmpFileHeader.bOffset, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "fseek failed");
        heap_caps_free(rowBuf);
        heap_caps_free(Image);
        fclose(fp);
        return 0;
    }

    // 逐行读取（BMP 底行优先）
    for (UWORD y = 0; y < height; y++) {
        size_t got = fread(rowBuf, 1, rowSize, fp);
        if (got != rowSize) {
            // 如果是最后一行因文件短缺导致读取不足，也可以选择 break 或继续处理已读字节
            ESP_LOGE(TAG, "BMP read error at line %u (got %zu, want %u)", (unsigned)y, got, (unsigned)rowSize);
            break;
        }

        UBYTE *p = rowBuf;
        for (UWORD x = 0; x < width; x++) {
            /* 
             * 重要：保持和你原始代码等效的字节序与比较方式
             * 原代码读三次 fread 到 Rdata[0],Rdata[1],Rdata[2]
             * 这里 p[0] 对应原来的 Rdata[0]，p[1] -> Rdata[1]，p[2] -> Rdata[2]
             */
            UBYTE d0 = *p++; // 原 Rdata[0]
            UBYTE d1 = *p++; // 原 Rdata[1]
            UBYTE d2 = *p++; // 原 Rdata[2]

            UBYTE color;
            if (d0 == 0 && d1 == 0 && d2 == 0) {
                color = 0; // Black
            } else if (d0 == 255 && d1 == 255 && d2 == 255) {
                color = 1; // White
            } else if (d0 == 0 && d1 == 255 && d2 == 255) {
                color = 2; // Yellow  (按你原始判断)
            } else if (d0 == 0 && d1 == 0 && d2 == 255) {
                color = 3; // Red
            // } else if (d0 == 0 && d1 == 128 && d2 == 255) {
            //     color = 4; // Orange (保留注释)
            } else if (d0 == 255 && d1 == 0 && d2 == 0) {
                color = 5; // Blue
            } else if (d0 == 0 && d1 == 255 && d2 == 0) {
                color = 6; // Green
            } else {
                color = 1; // 默认白
            }

            // 存储时保持原来“反转”的索引逻辑：BMP 从底到顶，所以把读取的第 y 行放到 Image 的 (height-1-y)
            Image[(int)(height - 1 - y) * width + x] = color;
        }
    }

    fclose(fp);

    // 刷新显示（保留你原来的绘制边界限制）
    for (UWORD y = 0; y < height; y++) {
        if (Ystart + y >= Paint.Height) break;
        for (UWORD x = 0; x < width; x++) {
            if (Xstart + x >= Paint.Width) break;
            Paint_SetPixel(Xstart + x, Ystart + y, Image[(int)y * width + x]);
        }
    }

    heap_caps_free(rowBuf);
    heap_caps_free(Image);
    return 0;
}


#endif


uint8_t GUI_RGB888_6Color(uint8_t *buffer,int Height,int Width)
{
    return true;
}

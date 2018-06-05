/*
 * fbtestfnt.c
 *
 * http://raspberrycompote.blogspot.com/2014/04/low-level-graphics-on-raspberry-pi-text.html
 *
 * Original work by J-P Rosti (a.k.a -rst- and 'Raspberry Compote')
 *
 * Licensed under the Creative Commons Attribution 3.0 Unported License
 * (http://creativecommons.org/licenses/by/3.0/deed.en_US)
 *
 * Distributed in the hope that this will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <linux/kd.h>
#include <sys/ioctl.h>

#include "fbink.h"

// default framebuffer palette
typedef enum {
    BLACK        =  0, /*   0,   0,   0 */
    BLUE         =  1, /*   0,   0, 172 */
    GREEN        =  2, /*   0, 172,   0 */
    CYAN         =  3, /*   0, 172, 172 */
    RED          =  4, /* 172,   0,   0 */
    PURPLE       =  5, /* 172,   0, 172 */
    ORANGE       =  6, /* 172,  84,   0 */
    LTGREY       =  7, /* 172, 172, 172 */
    GREY         =  8, /*  84,  84,  84 */
    LIGHT_BLUE   =  9, /*  84,  84, 255 */
    LIGHT_GREEN  = 10, /*  84, 255,  84 */
    LIGHT_CYAN   = 11, /*  84, 255, 255 */
    LIGHT_RED    = 12, /* 255,  84,  84 */
    LIGHT_PURPLE = 13, /* 255,  84, 255 */
    YELLOW       = 14, /* 255, 255,  84 */
    WHITE        = 15  /* 255, 255, 255 */
} COLOR_INDEX_T;

static unsigned short def_r[] =
    { 0,   0,   0,   0, 172, 172, 172, 168,
     84,  84,  84,  84, 255, 255, 255, 255};
static unsigned short def_g[] =
    { 0,   0, 168, 168,   0,   0,  84, 168,
     84,  84, 255, 255,  84,  84, 255, 255};
static unsigned short def_b[] =
    { 0, 172,   0, 168,   0, 172,   0, 168,
     84, 255,  84, 255,  84, 255,  84, 255};

// 'global' variables to store screen info
char *fbp = 0;
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;

// helper function to 'plot' a pixel in given color
void put_pixel(int x, int y, int c)
{
    // calculate the pixel's byte offset inside the buffer
    unsigned int pix_offset = x + y * finfo.line_length;

    // now this is about the same as 'fbp[pix_offset] = value'
    *((char*)(fbp + pix_offset)) = c;

}

void put_pixel_RGB24(int x, int y, int r, int g, int b)
{
    // remember to change main(): vinfo.bits_per_pixel = 24;
    // and: screensize = vinfo.xres * vinfo.yres *
    //                   vinfo.bits_per_pixel / 8;

    // calculate the pixel's byte offset inside the buffer
    // note: x * 3 as every pixel is 3 consecutive bytes
    unsigned int pix_offset = x * 3 + y * finfo.line_length;

    // now this is about the same as 'fbp[pix_offset] = value'
    *((char*)(fbp + pix_offset)) = b;
    *((char*)(fbp + pix_offset + 1)) = g;
    *((char*)(fbp + pix_offset + 2)) = r;

}

void put_pixel_RGB565(int x, int y, int r, int g, int b)
{
    // remember to change main(): vinfo.bits_per_pixel = 16;
    // or on RPi just comment out the whole 'Change vinfo'
    // and: screensize = vinfo.xres * vinfo.yres *
    //                   vinfo.bits_per_pixel / 8;

    // calculate the pixel's byte offset inside the buffer
    // note: x * 2 as every pixel is 2 consecutive bytes
    unsigned int pix_offset = x * 2 + y * finfo.line_length;

    // now this is about the same as 'fbp[pix_offset] = value'
    // but a bit more complicated for RGB565
    unsigned short c = ((r / 8) << 11) + ((g / 4) << 5) + (b / 8);
    // or: c = ((r / 8) * 2048) + ((g / 4) * 32) + (b / 8);
    // write 'two bytes at once'
    *((unsigned short*)(fbp + pix_offset)) = c;

}

// helper function to draw a rectangle in given color
void fill_rect(int x, int y, int w, int h, int c) {
    int cx, cy;
    for (cy = 0; cy < h; cy++) {
        for (cx = 0; cx < w; cx++) {
            put_pixel(x + cx, y + cy, c);
        }
    }
}

// helper function to clear the screen - fill whole
// screen with given color
void clear_screen(int c) {
    memset(fbp, c, vinfo.xres * vinfo.yres);
}

// helper function for drawing - no more need to go mess with
// the main function when just want to change what to draw...
void draw(char *arg) {

    fill_rect(0, 0, vinfo.xres, vinfo.yres, 1);

    char *text = (arg != 0) ? arg : "AB\"01\"C'D'E+-=/!?";
    int textX = FONTW;
    int textY = FONTH;
    int textC = 15;

    int i, l, x, y;

    // loop through all characters in the text string
    l = strlen(text);
    for (i = 0; i < l; i++) {
        // get the 'image' index for this character
        int ix = font_index(text[i]);
        // get the font 'image'
        char *img = fontImg[ix];
        // loop through pixel rows
        for (y = 0; y < FONTH; y++) {
            // loop through pixel columns
            for (x = 0; x < FONTW; x++) {
                // get the pixel value
                char b = img[y * FONTW + x];
                if (b > 0) { // plot the pixel
                    put_pixel(textX + i * FONTW + x, textY + y, textC);
                }
                else {
                    // leave empty (or maybe plot 'text backgr color')
                }
            } // end "for x"
        } // end "for y"
    } // end "for i"

    // demo all (printable ASCII) characters
    textY = 3 * FONTH;
    for (i = 32; i <= 126; i++) {
        // get the 'image' index for this character
        int ix = font_index((char)i);
        // get the font 'image'
        char *img = fontImg[ix];

        // loop through pixel rows
        for (y = 0; y < FONTH; y++) {
            // loop through pixel columns
            for (x = 0; x < FONTW; x++) {
                // get the pixel value
                char b = img[y * FONTW + x];
                if (b > 0) { // plot the pixel
                    put_pixel(FONTW + i % 16 * FONTW + x, textY + i / 16 * FONTH + y, textC);
                }
                else {
                    // leave empty
                }
            } // end "for x"
        } // end "for y"
    } // end "for i"

    sleep(5);
}

// application entry point
int main(int argc, char* argv[])
{

    int fbfd = 0;
    struct fb_var_screeninfo orig_vinfo;
    long int screensize = 0;

    // Open the framebuffer file for reading and writing
    fbfd = open("/dev/fb0", O_RDWR);
    if (!fbfd) {
      printf("Error: cannot open framebuffer device.\n");
      return(1);
    }
    printf("The framebuffer device was opened successfully.\n");

    // hide cursor
    char *kbfds = "/dev/tty";
    int kbfd = open(kbfds, O_WRONLY);
    if (kbfd >= 0) {
        ioctl(kbfd, KDSETMODE, KD_GRAPHICS);
    }
    else {
        printf("Could not open %s.\n", kbfds);
    }

    // Get variable screen information
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
      printf("Error reading variable information.\n");
    }
    printf("Original %dx%d, %dbpp\n", vinfo.xres, vinfo.yres,
       vinfo.bits_per_pixel );

    // Store for reset (copy vinfo to vinfo_orig)
    memcpy(&orig_vinfo, &vinfo, sizeof(struct fb_var_screeninfo));

    // Change variable info
    vinfo.bits_per_pixel = 8;
    vinfo.xres = (960 > vinfo.xres) ? vinfo.xres : 960;
    vinfo.yres = (540 > vinfo.yres) ? vinfo.yres : 540;
    if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo)) {
      printf("Error setting variable information.\n");
    }

    // Get fixed screen information
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
      printf("Error reading fixed information.\n");
    }
    //printf("Fixed info: smem_len %d, line_length %d\n", finfo.smem_len, finfo.line_length);

    // map fb to user mem
    screensize = finfo.smem_len;
    fbp = (char*)mmap(0,
              screensize,
              PROT_READ | PROT_WRITE,
              MAP_SHARED,
              fbfd,
              0);

    if ((int)fbp == -1) {
        printf("Failed to mmap.\n");
    }
    else {
        // draw...
        draw(argv[1]);
        //sleep(5);
    }

    // cleanup
    munmap(fbp, screensize);
    if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &orig_vinfo)) {
        printf("Error re-setting variable information.\n");
    }
    close(fbfd);

    // reset cursor
    if (kbfd >= 0) {
        ioctl(kbfd, KDSETMODE, KD_TEXT);
    }

    return 0;

}

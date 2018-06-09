/*
	FBInk: FrameBuffer eInker, a tool to print strings on eInk devices (Kobo/Kindle)
	Copyright (C) 2018 NiLuJe <ninuje@gmail.com>

	Linux framebuffer routines based on: fbtestfnt.c & fbtest6.c, from
	http://raspberrycompote.blogspot.com/2014/04/low-level-graphics-on-raspberry-pi-text.html &
	https://raspberrycompote.blogspot.com/2013/03/low-level-graphics-on-raspberry-pi-part_8.html
	Original works by J-P Rosti (a.k.a -rst- and 'Raspberry Compote'),
	Licensed under the Creative Commons Attribution 3.0 Unported License
	(http://creativecommons.org/licenses/by/3.0/deed.en_US)

	----

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as
	published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "fbink_internal.h"

#include "fbink.h"

// helper function to 'plot' a pixel in given color
static void
    put_pixel_Gray8(unsigned short int x, unsigned short int y, unsigned short int c)
{
	// calculate the pixel's byte offset inside the buffer
	size_t pix_offset = x + y * finfo.line_length;

	// now this is about the same as 'fbp[pix_offset] = value'
	*((char*) (fbp + pix_offset)) = (char) c;
}

static void
    put_pixel_RGB24(unsigned short int x,
		    unsigned short int y,
		    unsigned short int r,
		    unsigned short int g,
		    unsigned short int b)
{
	// calculate the pixel's byte offset inside the buffer
	// note: x * 3 as every pixel is 3 consecutive bytes
	size_t pix_offset = x * 3U + y * finfo.line_length;

	// now this is about the same as 'fbp[pix_offset] = value'
	*((char*) (fbp + pix_offset))     = (char) b;
	*((char*) (fbp + pix_offset + 1)) = (char) g;
	*((char*) (fbp + pix_offset + 2)) = (char) r;
}

static void
    put_pixel_RGB32(unsigned short int x,
		    unsigned short int y,
		    unsigned short int r,
		    unsigned short int g,
		    unsigned short int b)
{
	// calculate the pixel's byte offset inside the buffer
	// note: x * 4 as every pixel is 4 consecutive bytes
	size_t pix_offset = x * 4U + y * finfo.line_length;

	// now this is about the same as 'fbp[pix_offset] = value'
	*((char*) (fbp + pix_offset))     = (char) b;
	*((char*) (fbp + pix_offset + 1)) = (char) g;
	*((char*) (fbp + pix_offset + 2)) = (char) r;
	*((char*) (fbp + pix_offset + 3)) = 0xFF;    // Opaque, always.
}

static void
    put_pixel_RGB565(unsigned short int x,
		     unsigned short int y,
		     unsigned short int r,
		     unsigned short int g,
		     unsigned short int b)
{
	// calculate the pixel's byte offset inside the buffer
	// note: x * 2 as every pixel is 2 consecutive bytes
	size_t pix_offset = x * 2U + y * finfo.line_length;

	// now this is about the same as 'fbp[pix_offset] = value'
	// but a bit more complicated for RGB565
	unsigned int c = ((r / 8U) << 11) + ((g / 4U) << 5) + (b / 8U);
	// or: c = ((r / 8) * 2048) + ((g / 4) * 32) + (b / 8);
	// write 'two bytes at once'
	*((char*) (fbp + pix_offset)) = (char) c;
}

// handle various bpp...
static void
    put_pixel(unsigned short int x, unsigned short int y, unsigned short int c)
{
	if (vinfo.bits_per_pixel == 8) {
		put_pixel_Gray8(x, y, c);
	} else if (vinfo.bits_per_pixel == 16) {
		put_pixel_RGB565(x, y, def_r[c], def_g[c], def_b[c]);
	} else if (vinfo.bits_per_pixel == 24) {
		put_pixel_RGB24(x, y, def_r[c], def_g[c], def_b[c]);
	} else if (vinfo.bits_per_pixel == 32) {
		put_pixel_RGB32(x, y, def_r[c], def_g[c], def_b[c]);
	}
}

// helper function to draw a rectangle in given color
static void
    fill_rect(unsigned short int x,
	      unsigned short int y,
	      unsigned short int w,
	      unsigned short int h,
	      unsigned short int c)
{
	unsigned short int cx;
	unsigned short int cy;
	for (cy = 0U; cy < h; cy++) {
		for (cx = 0U; cx < w; cx++) {
			put_pixel((unsigned short int) (x + cx), (unsigned short int) (y + cy), c);
		}
	}
	printf("filled %hux%hu rectangle @ %hu, %hu\n", w, h, x, y);
}

// helper function to clear the screen - fill whole
// screen with given color
static void
    clear_screen(unsigned short int c)
{
	if (vinfo.bits_per_pixel == 8) {
		memset(fbp, c, finfo.smem_len);
	} else {
		// NOTE: Grayscale palette, we could have used def_r or def_g ;).
		memset(fbp, def_b[c], finfo.smem_len);
	}
}

// Return the font8x8 bitmap for a specifric ascii character
static char*
    font8x8_get_bitmap(int ascii)
{
	// Get the bitmap for that ASCII character
	if (ascii >= 0 && ascii <= 0x7F) {
		return font8x8_basic[ascii];
	} else if (ascii >= 0x80 && ascii <= 0x9F) {
		return font8x8_control[ascii];
	} else if (ascii >= 0xA0 && ascii <= 0xFF) {
		return font8x8_ext_latin[ascii];
	} else if (ascii >= 0x390 && ascii <= 0x3C9) {
		return font8x8_greek[ascii - 0x390];
	} else if (ascii >= 0x2500 && ascii <= 0x257F) {
		return font8x8_box[ascii - 0x2500];
	} else if (ascii >= 0x2580 && ascii <= 0x259F) {
		return font8x8_block[ascii - 0x2580];
	} else {
		printf("ASCII %d is OOR!\n", ascii);
		return font8x8_basic[0];
	}
}

// Render a specific font8x8 glyph into a pixmap
// (base size: 8x8, scaled by a factor of FONTSIZE_MULT, which varies depending on screen resolution)
static void
    font8x8_render(int ascii, char* glyph_pixmap)
{
	char* bitmap = font8x8_get_bitmap(ascii);

	unsigned short int x;
	unsigned short int y;
	unsigned short int i;
	unsigned short int j;
	unsigned short int k;
	bool               set = false;
	for (i = 0U; i < FONTW; i++) {
		// x: input row, i: output row
		x = i / FONTSIZE_MULT;
		for (j = 0U; j < FONTH; j++) {
			// y: input column, j: output column
			y   = j / FONTSIZE_MULT;
			set = bitmap[x] & 1 << y;
			// 'Flatten' our pixmap into a 1D array (0=0,0; 1=0,1; 2=0,2; FONTW=1,0)
			unsigned short int idx = (unsigned short int) (j + (i * FONTW));
			for (k = 0U; k < FONTSIZE_MULT; k++) {
				glyph_pixmap[idx + k] = set ? 1 : 0;
			}
		}
	}
}

// helper function for drawing - no more need to go mess with
// the main function when just want to change what to draw...
static struct mxcfb_rect
    draw(char*              text,
	 unsigned short int row,
	 unsigned short int col,
	 bool               is_inverted,
	 unsigned short int multiline_offset)
{
	printf("Printing '%s' @ line offset %hu\n", text, multiline_offset);
	unsigned short int fgC = is_inverted ? WHITE : BLACK;
	unsigned short int bgC = is_inverted ? BLACK : WHITE;

	unsigned short int i;
	unsigned short int x;
	unsigned short int y;
	// Adjust row in case we're a continuation of a multi-line print...
	row = (unsigned short int) (row + multiline_offset);

	// Compute the length of our actual string
	// NOTE: We already took care in fbink_print() of making sure that the string passed in text
	//       wouldn't exceed the maximum printable length, MAXCOLS - col
	size_t len = strnlen(text, MAXCOLS);
	printf("StrLen: %zu\n", len);

	// Compute the dimension of the screen region we'll paint to (taking multi-line into account)
	struct mxcfb_rect region = {
		.top    = (uint32_t)((row - multiline_offset) * FONTH),
		.left   = (uint32_t)(col * FONTW),
		.width  = multiline_offset > 0U ? (vinfo.xres - (uint32_t)(col * FONTW)) : (uint32_t)(len * FONTW),
		.height = (uint32_t)((multiline_offset + 1U) * FONTH),
	};

	printf("Region: top=%u, left=%u, width=%u, height=%u\n", region.top, region.left, region.width, region.height);

	// NOTE: eInk framebuffers are weird...,
	//       we should be computing the length of a line (MAXCOLS) based on xres_virtual,
	//       not xres (because it's guaranteed to be a multiple of 16).
	//       Unfortunately, that means this last block of the line is partly offscreen.
	//       Also, it CANNOT be part of the region passed to the eInk controller for a screen update...
	//       So, since this this last block is basically unusable because partly unreadable,
	//       and partly unrefreshable, don't count it as "available" (i.e., by including it in MAXCOLS),
	//       since that would happen to also wreak havoc in a number of our heuristics,
	//       just fudge printing a blank square 'til the edge of the screen if we're filling a line *completely*.
	// NOTE: Use len + col if we want to do that everytime we simply *hit* the edge...
	if (len == MAXCOLS) {
		fill_rect((unsigned short int) (region.left + (len * FONTW)),
			  (unsigned short int) (region.top + (unsigned short int) (multiline_offset * FONTH)),
			  (unsigned short int) (vinfo.xres - (len * FONTW)),
			  FONTH,
			  bgC);
	}

	// Fill our bounding box with our background color, so that we'll be visible no matter what's already on screen.
	// NOTE: Unneeded, we already plot the background when handling font glyphs ;).
	//fill_rect(region.left, region.top, region.width, region.height, bgC);

	// Alloc our pixmap on the heap, and re-use it.
	// NOTE: We tried using automatic VLAs, but that... didn't go well.
	//       (as in, subtle (or not so) memory and/or stack corruption).
	char* pixmap;
	pixmap = malloc(sizeof(*pixmap) * (size_t)(FONTW * FONTH));

	// Loop through all characters in the text string
	for (i = 0U; i < len; i++) {
		// get the glyph's pixmap
		font8x8_render(text[i], pixmap);
		// loop through pixel rows
		for (y = 0U; y < FONTH; y++) {
			// loop through pixel columns
			for (x = 0U; x < FONTW; x++) {
				// get the pixel value
				char b = pixmap[(y * FONTW) + x];
				if (b > 0) {
					// plot the pixel (fg, text)
					put_pixel((unsigned short int) ((col * FONTW) + (i * FONTW) + x),
						  (unsigned short int) ((row * FONTH) + y),
						  fgC);
				} else {
					// this is background,
					// fill it so that we'll be visible no matter what was on screen behind us.
					put_pixel((unsigned short int) ((col * FONTW) + (i * FONTW) + x),
						  (unsigned short int) ((row * FONTH) + y),
						  bgC);
				}
			}    // end "for x"
		}            // end "for y"
	}                    // end "for i"

	// Cleanup
	free(pixmap);

	return region;
}

// handle eink updates
static void
    refresh(int fbfd, struct mxcfb_rect region, bool is_flashing)
{
	// NOTE: While we'd be perfect candidates for using A2 waveform mode, it's all kinds of fucked up on Kobos,
	//       and may lead to disappearing text or weird blending depending on the surrounding fb content...
	//       It only shows up properly when FULL, which isn't great...
	//       Also, we need to set the EPDC_FLAG_FORCE_MONOCHROME flag to do it right.
	// NOTE: And while we're on the fun quirks train: FULL never flashes w/ AUTO on (some?) Kobos,
	//       so request GC16 if we want a flash...
	struct mxcfb_update_data update = {
		.temp          = TEMP_USE_AMBIENT,
		.update_marker = (uint32_t) getpid(),
		.update_mode   = is_flashing ? UPDATE_MODE_FULL : UPDATE_MODE_PARTIAL,
		.update_region = region,
		.waveform_mode = is_flashing ? WAVEFORM_MODE_GC16 : WAVEFORM_MODE_AUTO,
		.flags         = 0U,
	};

	// NOTE: Make sure update_marker is valid, an invalid marker *may* hang the kernel instead of failing gracefully,
	//       depending on the device/FW...
	if (update.update_marker == 0U) {
		update.update_marker = (70U + 66U + 73U + 78U + 75U);
	}

	if (ioctl(fbfd, MXCFB_SEND_UPDATE, &update) < 0) {
		perror("MXCFB_SEND_UPDATE");
		// FIXME: Mmmh, maybe don't exit to be nicer when used as a lib?
		exit(EXIT_FAILURE);
	}

	// NOTE: Let's be extremely thorough, and wait for completion on flashing updates ;)
	if (is_flashing) {
		if (ioctl(fbfd, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &update.update_marker) < 0) {
			{
				perror("MXCFB_WAIT_FOR_UPDATE_COMPLETE");
			}
		}
	}
}

// Open the framebuffer file & return the opened fd
int
    fbink_open(void)
{
	int fbfd = -1;

	// Open the framebuffer file for reading and writing
	fbfd = open("/dev/fb0", O_RDWR);
	if (!fbfd) {
		printf("Error: cannot open framebuffer device.\n");
		return EXIT_FAILURE;
	}
	printf("The framebuffer device was opened successfully.\n");

	return fbfd;
}

// Get the various fb info & setup global variables
int
    fbink_init(int fbfd)
{
	// Open the framebuffer if need be...
	bool keep_fd = true;
	if (fbfd == -1) {
		// If we're opening a fd now, don't keep it around.
		keep_fd = false;
		if (-1 == (fbfd = fbink_open())) {
			fprintf(stderr, "Failed to open the framebuffer, aborting . . .\n");
			return EXIT_FAILURE;
		}
	}

	// Get variable screen information
	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
		printf("Error reading variable information.\n");
	}
	printf("Variable info: %dx%d, %dbpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

	// Set font-size based on screen resolution (roughly matches: Pearl, Carta, Carta HD)
	// FIXME: Even on 600x800 screens, 8x8 might be too small...
	// NOTE: Using an odd number as scaling mutliplier works, too.
	if (vinfo.yres <= 800U) {
		FONTSIZE_MULT = 1U;    // 8x8
	} else if (vinfo.yres <= 1024) {
		FONTSIZE_MULT = 2U;    // 16x16
	} else {
		FONTSIZE_MULT = 4U;    // 32x32
	}
	// Go!
	FONTW = (unsigned short int) (FONTW * FONTSIZE_MULT);
	FONTH = (unsigned short int) (FONTH * FONTSIZE_MULT);
	printf("Fontsize set to %dx%d.\n", FONTW, FONTH);

	// Compute MAX* values now that we know the screen & font resolution
	MAXCOLS = (unsigned short int) (vinfo.xres / FONTW);
	MAXROWS = (unsigned short int) (vinfo.yres / FONTH);
	printf("Line length: %hu, Column length: %hu.\n", MAXCOLS, MAXROWS);

	// Get fixed screen information
	if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
		printf("Error reading fixed information.\n");
	}
	printf("Fixed info: smem_len %d, line_length %d\n", finfo.smem_len, finfo.line_length);

	// NOTE: Do we want to keep the fb0 fd open and pass it to our caller, or simply close it for now?
	//       Useful because we probably want to close it to keep open fds to a minimum when used as a library,
	//       while wanting to avoid a useless open/close/open/close cycle when used as a standalone tool.
	if (keep_fd) {
		return fbfd;
	} else {
		close(fbfd);
		return EXIT_SUCCESS;
	}
}

// Magic happens here!
void
    fbink_print(int fbfd, char* string, FBInkConfig* fbink_config)
{
	// Open the framebuffer if need be...
	bool keep_fd = true;
	if (fbfd == -1) {
		// If we open a fd now, we'll only keep it open for this single print call!
		// NOTE: We *expect* to be initialized at this point, though, but that's on the caller's hands!
		keep_fd = false;
		if (-1 == (fbfd = fbink_open())) {
			fprintf(stderr, "Failed to open the framebuffer, aborting . . .\n");
			return;
		}
	}

	// map fb to user mem
	// NOTE: Beware of smem_len on Kobos?
	//       c.f., https://github.com/koreader/koreader-base/blob/master/ffi/framebuffer_linux.lua#L36
	// NOTE: If we're keeping the fb's fd open, keep this mmap around, too.
	if (!fb_is_mapped) {
		screensize = finfo.smem_len;
		fbp        = (char*) mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
		if (fbp == MAP_FAILED) {
			perror("mmap");
		} else {
			fb_is_mapped = true;
		}
	}

	// NOTE: Make copies of these so we don't wreck our original struct, since we passed it by reference,
	//       and we *will* heavily mangle these two...
	short int col = fbink_config->col;
	short int row = fbink_config->row;

	struct mxcfb_rect region;

	if (fb_is_mapped) {
		// Clear screen?
		if (fbink_config->is_cleared) {
			clear_screen(fbink_config->is_inverted ? BLACK : WHITE);
		}

		// See if want to position our text relative to the edge of the screen, and not the beginning
		if (col < 0) {
			col = (short int) MAX(MAXCOLS + col, 0);
		}
		if (row < 0) {
			row = (short int) MAX(MAXROWS + row, 0);
		}
		printf("Adjusted position: column %hu, row %hu\n", col, row);

		// Clamp coordinates to the screen, to avoid blowing up ;).
		if (col >= MAXCOLS) {
			col = (short int) (MAXCOLS - 1);
			printf("Clamped column to %hu\n", col);
		}
		if (row >= MAXROWS) {
			row = (short int) (MAXROWS - 1);
			printf("Clamped row to %hu\n", row);
		}

		// See if we need to break our string down into multiple lines...
		size_t len = strlen(string);

		// Compute the amount of characters we can actually print on *one* line given the column we start on...
		// NOTE: When centered & padded, col will effectively be zero, but we still reserve one slot to the left,
		//       so fudge it here...
		// NOTE: In the same vein, when only centered, col will fluctuate,
		//       so we'll often end-up with a smaller amount of lines than originally calculated.
		//       Doing the computation with the initial col value ensures we'll have MORE lines than necessary,
		//       though, which is mostly harmless, since we'll skip trailing blank lines in this case :).
		unsigned short int available_cols;
		if (fbink_config->is_centered && fbink_config->is_padded) {
			available_cols = (unsigned short int) (MAXCOLS - 1U);
		} else {
			available_cols = (unsigned short int) (MAXCOLS - col);
		}
		// Given that, compute how many lines it'll take to print all that in these constraints...
		unsigned short int lines            = 1U;
		unsigned short int multiline_offset = 0U;
		if (len > available_cols) {
			lines = (unsigned short int) (len / available_cols);
			// If there's a remainder, we'll need an extra line ;).
			if (len % available_cols) {
				lines++;
			}
		}

		// Truncate to a single screen...
		if (lines > MAXROWS) {
			printf("Can only print %hu out of %hu lines, truncating!\n", MAXROWS, lines);
			lines = MAXROWS;
		}

		// Move our initial row up if we add so much line that some of it goes off-screen...
		if (row + lines > MAXROWS) {
			row = (short int) MIN(row - ((row + lines) - MAXROWS), MAXROWS);
		}
		printf("Final position: column %hu, row %hu\n", col, row);

		// We'll copy our text in chunks of formatted line...
		// NOTE: Store that on the heap, we've had some wacky adventures with automatic VLAs...
		char* line;
		line = malloc(sizeof(*line) * (MAXCOLS + 1U));

		printf(
		    "Need %hu lines to print %zu characters over %hu available columns\n", lines, len, available_cols);

		// Do the initial computation outside the loop,
		// so we'll be able to re-use line_len to accurately compute left when looping.
		size_t left     = len - (size_t)((multiline_offset) * (MAXCOLS - col));
		size_t line_len = 0U;
		// If we have multiple lines to print, draw 'em line per line
		for (multiline_offset = 0U; multiline_offset < lines; multiline_offset++) {
			// Compute the amount of characters left to print...
			left -= line_len;
			// And use it to compute the amount of characters to print on *this* line
			line_len = MIN(left, (size_t)(MAXCOLS - col));
			printf("Size to print: %zu out of %zu (left: %zu)\n",
			       line_len,
			       (size_t)(MAXCOLS - col) * sizeof(char),
			       left);

			// Just fudge the column for centering...
			if (fbink_config->is_centered) {
				// Don't fudge if also padded, we'll need the original value for heuristics,
				// but we still enforce column 0 later, as we always want full padding.
				col = fbink_config->is_padded ? col : (short int) ((MAXCOLS / 2U) - (line_len / 2U));
				if (!fbink_config->is_padded) {
					// Much like when both centering & padding, ensure we never write in column 0
					if (col == 0) {
						col = 1;
					}
					printf("Adjusted column to %hu for centering\n", col);
					// Recompute line_len since col has been updated.
					line_len = MIN(left, (size_t)(MAXCOLS - col));
					printf("Adjusted line_len to %zu for centering\n", line_len);
					// Don't print trailing blank lines...
					if (multiline_offset > 0 && line_len == 0) {
						printf("Skipping trailing blank line @ offset %hu\n", multiline_offset);
						continue;
					}
				}
			}
			// Just fudge the (formatted) line length for free padding :).
			if (fbink_config->is_padded) {
				// Don't fudge if also centered, we'll need the original value to split padding in two.
				line_len = fbink_config->is_centered ? line_len : (size_t)(MAXCOLS - col);
				if (!fbink_config->is_centered) {
					printf("Adjusted line_len to %zu for padding\n", line_len);
				}
			}

			// When centered & padded, we need to split the padding in two, left & right.
			if (fbink_config->is_centered && fbink_config->is_padded) {
				// NOTE: As we enforce a single padding space on the left,
				// to match the nearly full block that we fudge on the right in draw())
				// We crop 1 slot off MAXCOLS when doing these calculations,
				// but only when we'd be printing a full line,
				// to avoid shifting the centering to the left in other cases...
				short unsigned int available_maxcols = MAXCOLS;
				if (line_len + (short unsigned int) col == MAXCOLS) {
					available_maxcols = (short unsigned int) (MAXCOLS - 1U);
					printf("Setting available_maxcols to %hu\n", available_maxcols);
				}
				// We always want full padding
				col = 0;
				// We need to recompute left, because col is now 0.
				left = len - (size_t)((multiline_offset) * (MAXCOLS) - (multiline_offset));
				printf("Adjusted left to %zu for padding & centering\n", left);
				// We need to recompute line_len, because col is now 0.
				line_len = MIN(left, (size_t) available_maxcols);
				if (available_maxcols != MAXCOLS) {
					printf("Adjusted line_len to %zu for padding & centering\n", line_len);
				}
				size_t pad_len = (MAXCOLS - line_len) / 2U;
				// If we're not at the edge of the screen because of rounding errors,
				// add extra padding on the right.
				// It'll get cropped out by snprintf if it turns out to be extraneous.
				size_t extra_pad = MAXCOLS - line_len - (pad_len * 2U);
				printf("Total size: %zu + %zu + %zu + %zu = %zu\n",
				       1U + pad_len,
				       line_len,
				       pad_len,
				       extra_pad,
				       1U + (pad_len * 2U) + line_len + extra_pad);
				snprintf(line,
					 MAXCOLS + 1U,
					 "%*s%*s%*s",
					 (int) pad_len + 1,
					 "",
					 (int) line_len,
					 string + (len - left),
					 (int) (pad_len + extra_pad),
					 "");
			} else {
				snprintf(line, line_len + 1U, "%*s", (int) line_len, string + (len - left));
			}

			region = draw(line,
				      (unsigned short int) row,
				      (unsigned short int) col,
				      fbink_config->is_inverted,
				      multiline_offset);
		}

		// Cleanup
		free(line);
	}

	// Fudge the region if we asked for a screen clear, so that we actually refresh the full screen...
	if (fbink_config->is_cleared) {
		region.top    = 0U;
		region.left   = 0U;
		region.width  = vinfo.xres;
		region.height = vinfo.yres;
	}

	// Refresh screen
	refresh(fbfd, region, fbink_config->is_flashing);

	// cleanup
	if (fb_is_mapped && !keep_fd) {
		munmap(fbp, screensize);
	}
	if (!keep_fd) {
		close(fbfd);
	}
}

// application entry point
int
    main(int argc, char* argv[])
{
	int                        opt;
	int                        opt_index;
	static const struct option opts[] = {
		{ "row", required_argument, NULL, 'y' }, { "col", required_argument, NULL, 'x' },
		{ "invert", no_argument, NULL, 'h' },    { "flash", no_argument, NULL, 'f' },
		{ "clear", no_argument, NULL, 'c' },     { "centered", no_argument, NULL, 'm' },
		{ "padded", no_argument, NULL, 'p' },    { NULL, 0, NULL, 0 }
	};

	FBInkConfig fbink_config = { 0 };

	while ((opt = getopt_long(argc, argv, "y:x:hfcmp", opts, &opt_index)) != -1) {
		switch (opt) {
			case 'y':
				fbink_config.row = (short int) atoi(optarg);
				break;
			case 'x':
				fbink_config.col = (short int) atoi(optarg);
				break;
			case 'h':
				fbink_config.is_inverted = true;
				break;
			case 'f':
				fbink_config.is_flashing = true;
				break;
			case 'c':
				fbink_config.is_cleared = true;
				break;
			case 'm':
				fbink_config.is_centered = true;
				break;
			case 'p':
				fbink_config.is_padded = true;
				break;
			default:
				fprintf(stderr, "?? Unknown option code 0%o ??\n", opt);
				return EXIT_FAILURE;
				break;
		}
	}

	// Open framebuffer and keep it around, then setup globals.
	int fbfd = -1;
	if (-1 == (fbfd = fbink_open())) {
		fprintf(stderr, "Failed to open the framebuffer, aborting . . .\n");
		return EXIT_FAILURE;
	}
	if (EXIT_FAILURE == (fbfd = fbink_init(fbfd))) {
		fprintf(stderr, "Failed to initialize FBInk, aborting . . .\n");
		return EXIT_FAILURE;
	}

	char* string;
	if (optind < argc) {
		while (optind < argc) {
			string = argv[optind++];
			printf(
			    "Printing string '%s' @ column %hu, row %hu (inverted: %s, flashing: %s, centered: %s, left padded: %s, clear screen: %s)\n",
			    string,
			    fbink_config.col,
			    fbink_config.row,
			    fbink_config.is_inverted ? "true" : "false",
			    fbink_config.is_flashing ? "true" : "false",
			    fbink_config.is_centered ? "true" : "false",
			    fbink_config.is_padded ? "true" : "false",
			    fbink_config.is_cleared ? "true" : "false");
			fbink_print(fbfd, string, &fbink_config);
			// NOTE: Don't clobber previous entries if multiple strings were passed...
			fbink_config.row++;
			// NOTE: By design, if you ask for a clear screen, only the final print will stay on screen ;).
		}
	} else {
		printf("Usage!\n");
	}

	// Cleanup
	if (fb_is_mapped) {
		munmap(fbp, screensize);
	}
	close(fbfd);

	return EXIT_SUCCESS;
}

/*
 * TODO: DOC
 * TODO: Library (thread safety?)
 * TODO: waveform mode user-selection? -w
 * TODO: ioctl only (i.e., refresh current fb data, don't paint)
 *       -s w=758,h=1024 -f
 *       NOTE: Don't bother w/ getsubopt() and always make it full-screen?
 * TODO: Kindle ifdeffery and testing.
 */

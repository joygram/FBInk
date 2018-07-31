/*
	FBInk: FrameBuffer eInker, a tool to print text & images on eInk devices (Kobo/Kindle)
	Copyright (C) 2018 NiLuJe <ninuje@gmail.com>

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

// Build w/ ${CROSS_TC}-gcc -O3 -ffast-math -ftree-vectorize -funroll-loops -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=hard -mthumb -D_GLIBCXX_USE_CXX11_ABI=0 -pipe -fomit-frame-pointer -frename-registers -fweb -flto=9 -fuse-linker-plugin -Wall -Wextra -s tools/button_scan.c -o button_scan

// NOTE: Don't do this at home. This is a quick and rough POC to have some fun w/
//       https://www.mobileread.com/forums/showpost.php?p=3731967&postcount=12
//       No-one should ever, ever, ever include internal headers/code, I'm just re-using bits of private API to isolate this POC.
#include "../fbink.c"
#include "../fbink_device_id.c"

// FBInk always returns negative values on failure
#define ERRCODE(e) (-(e))

#include <linux/input.h>

// c.f., https://github.com/koreader/koreader-base/pull/468/files
#define SEND_INPUT_EVENT(t, c, v)                                                                                        \
	({                                                                                                               \
		gettimeofday(&ev.time, NULL);                                                                            \
		ev.type  = (t);                                                                                          \
		ev.code  = (c);                                                                                          \
		ev.value = (v);                                                                                          \
		write(ifd, &ev, sizeof(ev));                                                                             \
	})

// Application entry point
int
    main(int argc __attribute__((unused)), char* argv[] __attribute__((unused)))
{
	FBInkConfig fbink_config = { 0 };

	// Open framebuffer and keep it around, then setup globals.
	int fbfd = -1;
	if (ERRCODE(EXIT_FAILURE) == (fbfd = fbink_open())) {
		fprintf(stderr, "Failed to open the framebuffer, aborting . . .\n");
		return ERRCODE(EXIT_FAILURE);
	}
	if (fbink_init(fbfd, &fbink_config) == ERRCODE(EXIT_FAILURE)) {
		fprintf(stderr, "Failed to initialize FBInk, aborting . . .\n");
		return ERRCODE(EXIT_FAILURE);
	}

	// mmap the fb if need be...
	if (!g_fbink_isFbMapped) {
		if (memmap_fb(fbfd) != EXIT_SUCCESS) {
			return ERRCODE(EXIT_FAILURE);
		}
	}

	// Wheee! (Default to the proper value on 32bpp FW)
	FBInkColor button_color = { 0xD9, 0xD9, 0xD9 };

	// And handle yet another bit of 16bpp weirdness...
	// NOTE: There *may* be a rounding/conversion error somewhere...
	//       I can vouch for get_pixel_RGB565's accuracy,
	//       and set_pixel_RGB565 looks straightforward enough, so, err, I blame Kobo? :D.
	if (fbink_is_fb_quirky()) {
		button_color.r = 0xDE;
		button_color.g = 0xDB;
		button_color.b = 0xDE;
	}

	FBInkColor         color = { 0U };
	unsigned short int x;
	unsigned short int y;
	FBInkCoordinates   coords              = { 0U };
	unsigned short int consecutive_matches = 0U;
	unsigned short int match_count         = 0U;
	unsigned short int matched_lines       = 0U;
	bool               gotcha              = false;
	FBInkCoordinates   match_coords        = { 0U };

	// DEBUG: Fake a Glo ;).
	/*
	viewWidth  = 758U;
	viewHeight = 1024U;
	*/

	// Centralize the various thresholds we use...
	// NOTE: A button roughly takes 5% of the screen's height.
	//       We roughly assume we can split a button's height in three vertical zones: top padding, text, bottom padding.
	//       We want to match on half of the top padding, and move our cursor roughly to the center (of the text & button).
	//       So, split its height in 6, match for one sixth, and move the cursor for two sixth after that to get to the middle.
	//       We go with half of the padding because this isn't an exact science,
	//       and because Large Print mode makes the "text" zone slightly larger without changing the height of the button,
	//       which stretches a bit the assumption of the three zones of roughly equal heights ;).
	//       Fun fact: Large Print mode *can* make the button wider, on the other hand.
	unsigned short int target_lines         = ((0.05 * viewHeight) / 6U);
	unsigned short int button_height_offset = target_lines * 2U;
	// NOTE: Depending on the device's DPI & resolution, a button takes between 17% and 20% of the screen's width.
	//       Possibly less on larger resolutions, and more on smaller resolutions, so try to handle everyone in one fell swoop.
	unsigned short int min_target_pixels = (0.125 * viewWidth);
	unsigned short int max_target_pixels = (0.25 * viewWidth);

	// Recap the various settings as computed for this screen...
	fprintf(stderr, "Button color is expected to be #%hhx%hhx%hhx\n", button_color.r, button_color.g, button_color.b);
	fprintf(stderr,
		"We need to match two buttons each between %hu and %hu pixels wide over %hu lines!\n",
		min_target_pixels,
		max_target_pixels,
		target_lines);
	fprintf(stderr, "Correcting button coordinates by +%hupx vertically!\n", button_height_offset);

	// Start looping from the bottom half of the screen, to save some time, buttons are always going to be below that...
	for (y = (viewHeight / 2U); y < viewHeight; y++) {
		if (match_count == 2) {
			// It looks like we found the buttons on the previous line, keep looking...
			matched_lines++;
			fprintf(stderr, "Now at %hu consecutive lines matched\n", matched_lines);
			if (matched_lines >= target_lines) {
				gotcha = true;
				fprintf(stderr, "Gotcha! (After %hu consecutive lines matched)\n", matched_lines);
				break;
			}
		} else {
			// No match on the previous line, break if we were attempting to track a pair of buttons
			if (matched_lines > 0) {
				fprintf(stderr, "Booh :(. Failed to match after %hu consecutive lines\n", matched_lines);
				break;
			} else {
				// Reset the counter otherwise.
				matched_lines = 0U;
			}
		}
		// New line, reset counters
		consecutive_matches = 0U;
		match_count         = 0U;

		for (x = 0U; x < viewWidth; x++) {
			coords.x = x;
			coords.y = y;

			// Handle 16bpp rota (hopefully applies in Nickel, too ;D)
			(*fxpRotateCoords)(&coords);
			(*fxpGetPixel)(&coords, &color);

			if (color.r == button_color.r && color.g == button_color.g && color.b == button_color.b) {
				// Found a pixel of the right color for a button...
				consecutive_matches++;
			} else {
				// Pixel is no longer part of a button...
				if (consecutive_matches >= min_target_pixels &&
				    consecutive_matches <= max_target_pixels) {
					// But we've just finished matching enough pixels in a row to assume we found a button!
					match_count++;
					fprintf(stderr,
						"End of match %hu after %hu consecutive matche @ (%hu, %hu)\n",
						match_count,
						consecutive_matches,
						x,
						y);
					// We only care about the second button, Connect :).
					if (match_count == 2) {
						// Try to hit roughly the middle of the button
						match_coords.y = y + button_height_offset;
						// Backtrack to the middle of the button
						match_coords.x = x - (consecutive_matches / 2U);
					}
				} else {
					if (consecutive_matches > 0U) {
						// And we only matched a few stray pixels of the right color before, not a button.
						fprintf(
						    stderr,
						    "Failed end of match after %hu consecutive matches @ (%hu, %hu)\n",
						    consecutive_matches,
						    x,
						    y);
					}
				}
				// In any case, wrong color, reset the counter.
				consecutive_matches = 0U;
			}
		}
	}

	if (gotcha) {
		fprintf(stderr, "Match! :)\n");

		// The touch panel has a fixed origin that differs from the framebuffer's... >_<".
		rotate_coordinates(&match_coords);
		fprintf(stdout, "x=%hu, y=%hu\n", match_coords.x, match_coords.y);

		// NOTE: The H2O²r1 is a special snowflake, input is rotated 90° in the *other* direction
		//       (i.e., origin at the bottom-left instead of top-right).
		//       Hopefully that doesn't apply to the fb itself, too...
		fprintf(stdout,
			"H2O²r1: x=%hu, y=%hu\n",
			(unsigned short int) (viewHeight - match_coords.x - 1),
			(unsigned short int) (viewWidth - match_coords.y - 1));

		// Press it if TOUCH_ME is in the env...
		if (getenv("TOUCH_ME") != NULL) {
			fprintf(stderr, "Pressing the button . . .\n");
			struct input_event ev;
			int                ifd = -1;
			ifd                    = open("/dev/input/event1", O_WRONLY | O_NONBLOCK);
			if (ifd == -1) {
				fprintf(stderr, "Failed to open input device! \n");
				return ERRCODE(EXIT_FAILURE);
			}

			// NOTE: May not be completely right for every model... (OK on H2O)
			//       Double-check on your device w/ hexdump -x /dev/input/event1 (or -d if you prefer decimal).
			SEND_INPUT_EVENT(EV_ABS, ABS_MT_TRACKING_ID, 1);
			SEND_INPUT_EVENT(EV_ABS, ABS_MT_TOUCH_MAJOR, 1);
			SEND_INPUT_EVENT(EV_ABS, ABS_MT_WIDTH_MAJOR, 1);
			SEND_INPUT_EVENT(EV_ABS, ABS_MT_POSITION_X, match_coords.x);
			SEND_INPUT_EVENT(EV_ABS, ABS_MT_POSITION_Y, match_coords.y);
			SEND_INPUT_EVENT(EV_SYN, SYN_MT_REPORT, 0);
			SEND_INPUT_EVENT(EV_SYN, SYN_REPORT, 0);

			SEND_INPUT_EVENT(EV_ABS, ABS_MT_TRACKING_ID, 1);
			SEND_INPUT_EVENT(EV_ABS, ABS_MT_TOUCH_MAJOR, 0);
			SEND_INPUT_EVENT(EV_ABS, ABS_MT_WIDTH_MAJOR, 0);
			SEND_INPUT_EVENT(EV_ABS, ABS_MT_POSITION_X, match_coords.x);
			SEND_INPUT_EVENT(EV_ABS, ABS_MT_POSITION_Y, match_coords.y);
			SEND_INPUT_EVENT(EV_SYN, SYN_MT_REPORT, 0);
			SEND_INPUT_EVENT(EV_SYN, SYN_REPORT, 0);

			ioctl(ifd, EVIOCGRAB, 0);
			close(ifd);
		}
	} else {
		fprintf(stderr, "No match :(\n");
	}

	// Cleanup
	if (g_fbink_isFbMapped) {
		munmap(g_fbink_fbp, g_fbink_screensize);
	}
	close(fbfd);

	return EXIT_SUCCESS;
}
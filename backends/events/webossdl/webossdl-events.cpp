/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef WEBOS

// Allow use of stuff in <time.h>
#define FORBIDDEN_SYMBOL_EXCEPTION_time_h

#include "common/scummsys.h"
#include "common/system.h"
#include "sys/time.h"
#include "time.h"

#include "backends/events/webossdl/webossdl-events.h"
#include "gui/message.h"
#include "engines/engine.h"
#include "PDL.h"

// Inidicates if gesture area is pressed down or not.
static bool gestureDown = false;

// The timestamp when screen was pressed down.
static int screenDownTime = 0;

// The index of the motion pointer.
static int motionPtrIndex = -1;

// The maximum horizontal motion during dragging (For tap recognition).
static int dragDiffX = 0;

// The maximum vertical motion during dragging (For tap recognition).
static int dragDiffY = 0;

// Indicates if we are in drag mode.
static bool dragging = false;

// The current mouse position on the screen.
static int curX = 0, curY = 0;

// The time (seconds after 1/1/1970) when program started.
static time_t programStartTime = time(0);

// Time in millis to wait before loading a queued event
static const int queuedInputEventDelay = 250;

// Time to execute queued event
static long queuedEventTime = 0;

// An event to be processed after the next poll tick
static Common::Event queuedInputEvent;

// To prevent left clicking after right or middle click
static bool blockLClick = false;

// To prevent right clicking after middle click
static bool blockRClick = false;

// To prevent clicking when we want a special action
static bool specialAction = false;

/**
 * Initialize a new WebOSSdlEventSource.
 */
WebOSSdlEventSource::WebOSSdlEventSource() {
	queuedInputEvent.type = (Common::EventType)0;
}

/**
 * Returns the number of passed milliseconds since program start.
 *
 * @return The number of passed milliseconds.
 */
static time_t getMillis()
{
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return (time(0) - programStartTime) * 1000 + tv.tv_usec / 1000;
}

/**
 * Before calling the original SDL implementation, this method loads in
 * queued events.
 *
 * @param event The ScummVM event
 */
bool WebOSSdlEventSource::pollEvent(Common::Event &event) {
	long curTime = getMillis();

	// Move the queued event into the event if it's time
	if (queuedInputEvent.type != (Common::EventType)0 && curTime >= queuedEventTime) {
		event = queuedInputEvent;
		if (event.type == Common::EVENT_LBUTTONDOWN)
            processMouseEvent(event, curX, curY);
		queuedInputEvent.type = (Common::EventType)0;
		return true;
	}

	return SdlEventSource::pollEvent(event);
}

/**
 * WebOS devices only have a Shift key and a CTRL key. There is also an Alt
 * key (the orange key) but this is already processed by WebOS to change the
 * mode of the keys so ScummVM must not use this key as a modifier. Instead
 * pressing down the gesture area is used as Alt key.
 *
 * @param mod   The pressed key modifier as detected by SDL.
 * @param event The ScummVM event to setup.
 */
void WebOSSdlEventSource::SDLModToOSystemKeyFlags(SDLMod mod,
		Common::Event &event) {
	event.kbd.flags = 0;

	if (mod & KMOD_SHIFT)
		event.kbd.flags |= Common::KBD_SHIFT;
	if (mod & KMOD_CTRL)
		event.kbd.flags |= Common::KBD_CTRL;

	// Holding down the gesture area emulates the ALT key
	if (gestureDown)
		event.kbd.flags |= Common::KBD_ALT;
}

/**
 * Before calling the original SDL implementation this method checks if the
 * gesture area is pressed down.
 *
 * @param ev    The SDL event
 * @param event The ScummVM event.
 * @return True if event was processed, false if not.
 */
bool WebOSSdlEventSource::handleKeyDown(SDL_Event &ev, Common::Event &event) {
	// Handle gesture area tap.
	if (ev.key.keysym.sym == SDLK_WORLD_71) {
		gestureDown = true;
		return true;
	}

	// Ensure that ALT key (Gesture down) is ignored when back or forward
	// gesture is detected. This is needed for WebOS 1 which releases the
	// gesture tap AFTER the backward gesture event and not BEFORE (Like
	// WebOS 2).
	if (ev.key.keysym.sym == 27 || ev.key.keysym.sym == 229) {
	    gestureDown = false;
	}

        // handle virtual keyboard dismiss key
        if (ev.key.keysym.sym == 24) {
                int gblPDKVersion = PDL_GetPDKVersion();
                // check for correct PDK Version
                if (gblPDKVersion >= 300) {
                        PDL_SetKeyboardState(PDL_FALSE);
                        return true;
                }
        }

	// Call original SDL key handler.
	return SdlEventSource::handleKeyDown(ev, event);
}

/**
 * Before calling the original SDL implementation this method checks if the
 * gesture area has been released.
 *
 * @param ev    The SDL event
 * @param event The ScummVM event.
 * @return True if event was processed, false if not.
 */
bool WebOSSdlEventSource::handleKeyUp(SDL_Event &ev, Common::Event &event) {
	// Handle gesture area tap.
	if (ev.key.keysym.sym == SDLK_WORLD_71) {
		gestureDown = false;
		return true;
	}

	// handle virtual keyboard dismiss key
	if (ev.key.keysym.sym == 24) {
		int gblPDKVersion = PDL_GetPDKVersion();
		// check for correct PDK Version
		if (gblPDKVersion >= 300) {
			PDL_SetKeyboardState(PDL_FALSE);
			return true;
		}
	}

	// Call original SDL key handler.
	return SdlEventSource::handleKeyUp(ev, event);
}

/**
 * Handles mouse button press.
 *
 * @param ev    The SDL event
 * @param event The ScummVM event.
 * @return True if event was processed, false if not.
 */
bool WebOSSdlEventSource::handleMouseButtonDown(SDL_Event &ev, Common::Event &event) {
    // If no button was pressed
	if (motionPtrIndex == -1) {

	    // We calculate the position of the first touch
	    // to put our cursor on it
	    int screenX = g_system->getWidth();
        int screenY = g_system->getHeight();
        curX = MIN(screenX, MAX(0, static_cast<int>(ev.motion.x)));
        curY = MIN(screenY, MAX(0, static_cast<int>(ev.motion.y)));

		dragDiffX = 0;
		dragDiffY = 0;
		screenDownTime = getMillis();
		blockLClick = false;
		blockRClick = false;
		specialAction = false;

		long curTime = getMillis();

        // Queued event, to hold left click if we don't move
        queuedInputEvent.type = Common::EVENT_LBUTTONDOWN;
        queuedEventTime = curTime + 500;
	}
	// If we push another button while the first one is pressed,
	// we stop the queued event if it didn't trigger yet
	else {
	    if (queuedInputEvent.type == Common::EVENT_LBUTTONDOWN) {
            queuedInputEvent.type = (Common::EventType)0;
        }
	}

	// We store the index of the pressed button (for multi-touch)
    motionPtrIndex = ev.button.which;

	return true;
}

/**
 * Handles mouse button release.
 *
 * @param ev    The SDL event
 * @param event The ScummVM event.
 * @return True if event was processed, false if not.
 */
bool WebOSSdlEventSource::handleMouseButtonUp(SDL_Event &ev, Common::Event &event) {
    // We stop the queued event that was supposed to left click if it hasn't happen yet
    if (queuedInputEvent.type == Common::EVENT_LBUTTONDOWN) {
        queuedInputEvent.type = (Common::EventType)0;
    }

    // To handle the first button pressed
    if (ev.button.which == 0) {
        // No more button pressed
        motionPtrIndex = -1;

		// When drag mode was active then simply send a mouse up event
		// only if we don't display the menu, or it could click on it
		if (dragging && !specialAction) {
			event.type = Common::EVENT_LBUTTONUP;
			processMouseEvent(event, curX, curY);
			dragging = false;
			return true;
		}

		// When mouse was moved 5 pixels or less then emulate
		// a mouse button click
		if (ABS(dragDiffX) < 6 && ABS(dragDiffY) < 6 && !blockLClick) {
            event.type = Common::EVENT_LBUTTONUP;
            processMouseEvent(event, curX, curY);
            g_system->getEventManager()->pushEvent(event);
            event.type = Common::EVENT_LBUTTONDOWN;
        }
	}

	// If the second button is released
	if (ev.button.which == 1) {
	    int screenX = g_system->getWidth();
        int screenY = g_system->getHeight();

	    // 60% of the screen height for menu dialog/keyboard
        if (ABS(dragDiffY) >= ABS(screenY*0.6)) {
            specialAction = true;
            if (dragDiffY <= 0) {
                int gblPDKVersion = PDL_GetPDKVersion();
                // check for correct PDK Version
                if (gblPDKVersion >= 300) {
                    PDL_SetKeyboardState(PDL_TRUE);
                    return true;
                }
            } else {
                if (g_engine && !g_engine->isPaused()) {
                    g_engine->openMainMenuDialog();
                    return true;
                }
            }
        }

        // 60% of the screen width for escape key (left or right)
		if (ABS(dragDiffX) >= ABS(screenX*0.6)) {
		    specialAction = true;
		    long curTime = getMillis();

			event.type = Common::EVENT_KEYDOWN;
			queuedInputEvent.type = Common::EVENT_KEYUP;
			event.kbd.flags = queuedInputEvent.kbd.flags = 0;
			event.kbd.keycode = queuedInputEvent.kbd.keycode = Common::KEYCODE_ESCAPE;
			event.kbd.ascii = queuedInputEvent.kbd.ascii = Common::ASCII_ESCAPE;
			queuedEventTime = curTime + queuedInputEventDelay;
			return true;
		}

		// When we tap with the second finger (without moving the first finger
        // more than 6 pixels), we emulate a right click
		if (ABS(dragDiffX) < 6 && ABS(dragDiffY) < 6 && !blockRClick) {
            event.type = Common::EVENT_RBUTTONUP;
            processMouseEvent(event, curX, curY);
            g_system->getEventManager()->pushEvent(event);
            event.type = Common::EVENT_RBUTTONDOWN;
            blockLClick = true;
        }
	}

	// If the third button is released
	if (ev.button.which == 2) {
        // When we tap with the third finger (without moving the first finger
        // more than 6 pixels), we emulate a middle click
		if (ABS(dragDiffX) < 6 && ABS(dragDiffY) < 6) {
            event.type = Common::EVENT_MBUTTONUP;
            processMouseEvent(event, curX, curY);
            g_system->getEventManager()->pushEvent(event);
            event.type = Common::EVENT_MBUTTONDOWN;
            blockLClick = blockRClick = true;
        }
	}

	return true;
}

/**
 * Handles mouse motion.
 *
 * @param ev    The SDL event
 * @param event The ScummVM event.
 * @return True if event was processed, false if not.
 */
bool WebOSSdlEventSource::handleMouseMotion(SDL_Event &ev, Common::Event &event) {
	if (ev.motion.which == 0) {
        int screenX = g_system->getWidth();
        int screenY = g_system->getHeight();
		curX = MIN(screenX, MAX(0, static_cast<int>(ev.motion.x)));
		curY = MIN(screenY, MAX(0, static_cast<int>(ev.motion.y)));
        dragDiffX += ev.motion.xrel;
        dragDiffY += ev.motion.yrel;
		event.type = Common::EVENT_MOUSEMOVE;
		processMouseEvent(event, curX, curY);
	}
	// If we move more than 5 pixels, we're dragging
    // so, we stop the queued event that was going to
    // left click
    if (motionPtrIndex != -1 && (ABS(dragDiffX) > 5 || ABS(dragDiffY) > 5) && !dragging) {
        if (queuedInputEvent.type == Common::EVENT_LBUTTONDOWN) {
            queuedInputEvent.type = (Common::EventType)0;
        }
        dragging = true;
    }
	return true;
}

#endif

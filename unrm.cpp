/*
 *	FATX filesystem support
 *
 *  Copyright (C) 2012, 2013, 2014 Christophe Duverger
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

extern "C" {
	#include <hal/input.h>
	#include <hal/xbox.h>
	//#include <hal/fileio.h>
	#include <openxdk/debug.h>
}
#include "fatx.hpp"

enum {
	autom,
	test,
	manual
} conf = test;
bool verb = false;

#ifndef XBOX_DRIVE
	#define XBOX_DRIVE	"\\\\.\\PhysicalDrive0"
#endif

const char* const			drive = XBOX_DRIVE;

void 						boost::			throw_exception(std::exception const& e) {
	(void) e;
}
void						console::		write(const string s, bool err) {
	(void) err;
	debugPrint((char*)&s[0]);
}
pair<bool, bool>			console::		read() {
	XInput_GetEvents();
	pair<bool, bool> res(
		((g_Pads[0].PressedButtons.ucAnalogButtons[XPAD_A] != 0) || (g_Pads[0].PressedButtons.ucAnalogButtons[XPAD_B] != 0)),
		g_Pads[0].PressedButtons.ucAnalogButtons[XPAD_A] != 0
	);
	return res;
}
bool						frontend::		getanswer(bool def) {
	bool res = false;
	console::write(def ? " [Y(A,X)/n(B)] :" : " [y(A)/N(B,X)] :");
	if(force_n) {
		console::write("n\n");
		res = false;
	}
	else if(force_y) {
		console::write("y\n");
		res = true;
	}
	else if(force_a) {
		console::write(def ? "y\n" : "n\n");
		res = def;
	}
	else {
		pair<bool, bool> r = console::read();
		res = r.first ? r.second : def;
	}
	allyes = allyes && res;
	return res;
}
							frontend::		frontend(int ac, const char* const * const av) :
	readonly(conf == test),	prog(
	#ifdef XBOX_FSCK
		fsck
	#endif
	#ifdef XBOX_UNRM
		unrm
	#endif
	),
	force_y(false),
	force_n(false),
	force_a(conf != manual),
	verbose(verb),
	recover(
	#ifdef XBOX_UNRM
		true
	#else
		false
	#endif
	),
	local(false),
	deldate(true),
	dellost(true),
	fuse_debug(false),
	fuse_foregrd(false),
	fuse_singlethr(false),
	nofat(false),
	argc(ac),
	argv(av),
	progname(
	#ifdef XBOX_FSCK
		"xboxfsck"
	#endif
	#ifdef XBOX_UNRM
		"xboxunrm"
	#endif
	),
	dialog(true),
	lostfound(def_landf),
	foundfile(def_fpre),
	filecount(0),
	mount(),
	volname(def_label),
	fuse_option(),
	unkopt(),
	partition("x2"),
	clus_size(0),
	uid(0),
	gid(0),
	mask(0),
	allyes(true),
	offset(0),
	size(0),
	input(drive) {
}

extern "C" {
	void									_pei386_runtime_relocator () {
	}
	void									XBoxStartup() {
		console::write(
			#ifdef XBOX_FSCK
				"XBoxFSCK\n"
			#endif
			#ifdef XBOX_UNRM
				"XBoxUNRM\n"
			#endif
		);
		XInput_Init();
		console::write(
			"Press X to run automagically, in test mode (no modification)\n"
			"Press Y to run automagically\n"
			"Press A to run interactively\n"
			"Press B to cancel\n"
			"Press Start to toggle verbose mode\n"
		);
		while(true) {
			console::write(string() + "Verbose mode is " + (verb ? "on" : "off") + "\n");
			XInput_GetEvents();
			if(g_Pads[0].PressedButtons.ucAnalogButtons[XPAD_X] != 0) {
				conf = test;
				break;
			}
			if(g_Pads[0].PressedButtons.ucAnalogButtons[XPAD_Y] != 0) {
				conf = autom;
				break;
			}
			if(g_Pads[0].PressedButtons.ucAnalogButtons[XPAD_A] != 0) {
				conf = manual;
				break;
			}
			if(g_Pads[0].PressedButtons.ucAnalogButtons[XPAD_B] != 0)
				return;
			if(g_Pads[0].PressedButtons.usDigitalButtons & XPAD_START)
				verb = (!verb);
		}
		console::write(string() +
			#ifdef XBOX_FSCK
				"fsck "
			#endif
			#ifdef XBOX_UNRM
				"unrm "
			#endif
			+ ((conf == autom) ? "-a " : (conf == test) ? "-t " : "")
			+ (verb ? "-v " : "")
			+ drive
			+ "\n"
		);
		bool status = fatx();
		console::write(string() + "Press X to exit.\n");
		while(true) {
			XInput_GetEvents();
			if(g_Pads[0].PressedButtons.ucAnalogButtons[XPAD_X] != 0)
				break;
		}
		XInput_Quit();
		if(status != code_noerr) {
			console::write("Modifications done, rebooting in 5s...\n");
			XSleep(5000);
			XReboot();
		}
	}
}

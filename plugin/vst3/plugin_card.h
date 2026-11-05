/*
 * plugin_card.h
 *
 * Each plugin instance's folder (plan E9.2 Task 8): BASE/<16 hex digits>/ holds card/ (the
 * engine's card) and lock, which the instance holds with flock() for its life. BASE is
 * $LICHOMAT_PLUGIN_HOME, or ~/Library/Application Support/LICHOMAT Plugin. A folder whose lock
 * nobody holds and that is older than a minute is left from a host that died: the next instance
 * removes it.
 *
 * The instance's log, BASE/logs/<16 hex digits>.log (the id of its first folder), holds the
 * plugin's lines and the engine's output, and outlives the instance: the owner reads it after
 * the set is closed. BASE/logs keeps the newest kLogsKept (32) logs; each new instance removes
 * the older ones.
 *
 * A new instance's card is a copy, in the card's order and with /TMP, of BASE/Card, the
 * plugin's own start card, if there is one; else of LICHOMAT Emu's card ($LICHOMAT_EMU_HOME/Card,
 * or ~/Library/Application Support/LICHOMAT Emu/Card, app_card.c:55-77), so that it starts where
 * the app left off; else it stays empty and the engine seeds the init card. A restored state
 * replaces it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include <string>

namespace lichomat {

class CardFolder
{
public:
	~CardFolder () { release (); }

	/** BASE, made if missing; empty on an error */
	static std::string base ();
	/** remove the folders of BASE that no instance holds and that are older than 60 s, and the
	 * logs of BASE/logs but the newest kLogsKept */
	static void cleanStale ();

	static constexpr unsigned kLogsKept = 32;

	/** a new folder with an empty card/, its lock held; false with the problem in err */
	bool create (std::string& err);
	/** the card of a new instance: BASE/Card or LICHOMAT Emu's, copied into card/; true if copied */
	bool copyStartCard ();
	/** delete the folder and let go of the lock */
	void remove ();
	/** let go of the lock and keep the folder (it is the next instance's to clean) */
	void release ();

	bool valid () const { return lockFd >= 0; }
	const std::string& dir () const { return dirPath; }
	std::string card () const { return dirPath + "/card"; }
	/** BASE/logs/<this folder's id>.log, BASE/logs made if missing; empty on an error */
	std::string log () const;

	/** move other's folder and lock into this one, which lets go of its own first */
	void take (CardFolder& other);

private:
	std::string dirPath;
	int lockFd = -1;
};

} // namespace lichomat

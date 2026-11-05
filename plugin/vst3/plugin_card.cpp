/*
 * plugin_card.cpp
 *
 * Each plugin instance's folder (plan E9.2 Task 8); see plugin_card.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plugin_card.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

extern "C" {
#include "emu_hostfs.h"	/* a C header without its own extern "C" */
}

namespace lichomat {

static int mkdirs (const std::string& path)
{
	std::string p;
	size_t at = 0;
	while (at != std::string::npos)
	{
		at = path.find ('/', at + 1);
		p = path.substr (0, at);
		if (mkdir (p.c_str (), 0755) != 0 && errno != EEXIST)
			return -1;
	}
	return 0;
}

static int removeEntry (const char* path, const struct stat*, int, struct FTW*)
{
	return ::remove (path) == 0 ? 0 : -1;
}

static void removeTree (const std::string& dir)
{
	if (!dir.empty ())
		nftw (dir.c_str (), removeEntry, 16, FTW_DEPTH | FTW_PHYS);
}

/* everything below the folder but its own lock file */
static int removeEntryButLock (const char* path, const struct stat*, int, struct FTW* w)
{
	if (w->level == 0 || (w->level == 1 && strcmp (path + w->base, "lock") == 0))
		return 0;
	return ::remove (path) == 0 ? 0 : -1;
}

/* An instance folder, its lock file last: a removal that stops partway (an error, a crash) leaves
 * the lock, so the folder is still seen as an instance's and cleaned later; the next cleaner takes
 * a folder without a lock file for nobody's (cleanStale()) */
static void removeFolder (const std::string& dir)
{
	if (dir.empty ())
		return;
	if (nftw (dir.c_str (), removeEntryButLock, 16, FTW_DEPTH | FTW_PHYS) != 0)
		return;	/* the lock stays: the next instance tries again */
	unlink ((dir + "/lock").c_str ());
	rmdir (dir.c_str ());
}

std::string CardFolder::base ()
{
	std::string b;
	const char* env = getenv ("LICHOMAT_PLUGIN_HOME");
	if (env && *env)
		b = env;
	else if (const char* home = getenv ("HOME"))
		b = std::string (home) + "/Library/Application Support/LICHOMAT Plugin";
	else
		return {};
	return mkdirs (b) == 0 ? b : std::string ();
}

static bool instanceName (const char* name)
{
	if (strlen (name) != 16)
		return false;
	for (const char* p = name; *p; p++)
		if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
			return false;
	return true;
}

/* the instance logs of BASE/logs but the newest keep */
static void pruneLogs (const std::string& b, unsigned keep)
{
	const std::string dir = b + "/logs";
	DIR* d = opendir (dir.c_str ());
	if (!d)
		return;
	std::vector<std::pair<struct timespec, std::string>> logs;
	for (const struct dirent* e; (e = readdir (d)) != nullptr;)
	{
		const std::string n = e->d_name;
		struct stat st;
		if (n.size () == 20 && n.compare (16, 4, ".log") == 0 && instanceName (n.substr (0, 16).c_str ())
		    && stat ((dir + "/" + n).c_str (), &st) == 0 && S_ISREG (st.st_mode))
			logs.push_back ({st.st_mtimespec, n});
	}
	closedir (d);
	if (logs.size () <= keep)
		return;
	std::sort (logs.begin (), logs.end (), [] (const auto& x, const auto& y) {
		return x.first.tv_sec != y.first.tv_sec ? x.first.tv_sec > y.first.tv_sec : x.first.tv_nsec > y.first.tv_nsec;
	});
	for (size_t i = keep; i < logs.size (); i++)
		unlink ((dir + "/" + logs[i].second).c_str ());
}

void CardFolder::cleanStale ()
{
	const std::string b = base ();
	if (!b.empty ())
		pruneLogs (b, kLogsKept);
	DIR* d = b.empty () ? nullptr : opendir (b.c_str ());
	if (!d)
		return;
	const time_t now = time (nullptr);
	for (const struct dirent* e; (e = readdir (d)) != nullptr;)
	{
		if (!instanceName (e->d_name))
			continue;
		const std::string dir = b + "/" + e->d_name;
		struct stat st;
		if (stat (dir.c_str (), &st) != 0 || !S_ISDIR (st.st_mode) || now - st.st_mtime < 60)
			continue;
		/* nobody holds it: its lock is free, or it has none (a removal that stopped partway) */
		const int fd = open ((dir + "/lock").c_str (), O_RDWR | O_CLOEXEC);
		if ((fd >= 0 && flock (fd, LOCK_EX | LOCK_NB) == 0) || (fd < 0 && errno == ENOENT))
			removeFolder (dir);
		if (fd >= 0)
			close (fd);
	}
	closedir (d);
}

bool CardFolder::create (std::string& err)
{
	release ();
	const std::string b = base ();
	if (b.empty ())
	{
		err = "no folder for the plugin's cards (HOME is not set, or it cannot be made)";
		return false;
	}
	for (int attempt = 0; attempt < 8; attempt++)
	{
		uint8_t id[8];
		arc4random_buf (id, sizeof id);
		char name[17];
		for (int i = 0; i < 8; i++)
			snprintf (name + 2 * i, 3, "%02x", id[i]);
		const std::string dir = b + "/" + name;
		if (mkdir (dir.c_str (), 0755) != 0)
			continue;	/* taken: another id */
		const int fd = open ((dir + "/lock").c_str (), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
		if (fd < 0 || flock (fd, LOCK_EX | LOCK_NB) != 0 || mkdir ((dir + "/card").c_str (), 0755) != 0)
		{
			err = std::string ("cannot make ") + dir + ": " + strerror (errno);
			if (fd >= 0)
				close (fd);
			removeTree (dir);
			return false;
		}
		dirPath = dir;
		lockFd = fd;
		return true;
	}
	err = "cannot make a folder in " + b;
	return false;
}

bool CardFolder::copyStartCard ()
{
	std::string app;
	const char* env = getenv ("LICHOMAT_EMU_HOME");
	if (env && *env)
		app = std::string (env) + "/Card";
	else if (const char* home = getenv ("HOME"))
		app = std::string (home) + "/Library/Application Support/LICHOMAT Emu/Card";
	const std::string own = base () + "/Card";
	const std::string& from = emu_hostKind (own.c_str ()) == 1 ? own : app;
	if (from.empty () || emu_hostKind (from.c_str ()) != 1 || dirPath.empty ())
		return false;
	if (emu_hostCopyCard (from.c_str (), card ().c_str (), 0) == 0)
		return true;
	removeTree (card ());	/* a part of it: the engine seeds the init card instead */
	mkdir (card ().c_str (), 0755);
	return false;
}

std::string CardFolder::log () const
{
	const size_t slash = dirPath.rfind ('/');
	if (slash == std::string::npos)
		return {};
	const std::string dir = dirPath.substr (0, slash) + "/logs";
	return mkdirs (dir) == 0 ? dir + "/" + dirPath.substr (slash + 1) + ".log" : std::string ();
}

void CardFolder::remove ()
{
	removeFolder (dirPath);	/* the lock still held: no cleaner takes it meanwhile */
	release ();
}

void CardFolder::release ()
{
	if (lockFd >= 0)
		close (lockFd);
	lockFd = -1;
	dirPath.clear ();
}

void CardFolder::take (CardFolder& other)
{
	release ();
	dirPath = other.dirPath;
	lockFd = other.lockFd;
	other.dirPath.clear ();
	other.lockFd = -1;
}

} // namespace lichomat

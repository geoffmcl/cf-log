// daemon.cxx
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, U$
//
// Copyright (C) 2006  Oliver Schroeder
// 2012/11/06 14:29:30 - Add double fork(), and close 0, 1, and 2 file descriptor - geoff
//
//////////////////////////////////////////////////////////////////////
//
// implement the class "cDaemon", which does everything necessary
// to become a daemon
//
//////////////////////////////////////////////////////////////////////
#ifndef _MSC_VER
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h> // printf()
#include <iostream>
#include <cstdlib>
#include "logstream.hxx"
#include "daemon.hxx"
#include "cf_misc.hxx"

using namespace std;

pid_t cDaemon::PidOfDaemon; // remember who we are
list <pid_t> cDaemon::Children; // keep track of our children

#ifndef _NFILES
#define _NFILES 3
#endif

//////////////////////////////////////////////////////////////////////
// SigHandler ()
//////////////////////////////////////////////////////////////////////
void cDaemon::SigHandler ( int SigType )
{
	if (SigType == SIGCHLD)
	{
		int stat;
		pid_t childpid;
		while ((childpid=waitpid (-1, &stat, WNOHANG)) > 0)
			SG_LOG2 (SG_SYSTEMS, SG_ALERT, "Child stopped: " << childpid );
		return;
	}
	if (SigType == SIGPIPE)
	{
		pid_t childpid;
		childpid = getpid();
		SG_LOG2 (SG_SYSTEMS, SG_ALERT, "SIGPIPE received. Connection error " << childpid);
		return;
	}
	switch (SigType)
	{
		case  1: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGHUP! " << "Hangup (POSIX)");
			break;
		case  2: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGINT! " << "Interrupt (ANSI)");
			break;
		case  3: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGQUIT! " << "Quit (POSIX)");
			break;
		case  4: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGILL! " << "Illegal instruction (ANSI)");
			break;
		case  5: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGTRAP! " << "Trace trap (POSIX)");
			break;
		case  6: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGABRT! " << "IOT trap (4.2 BSD)");
			break;
		case  7: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGBUS! " << "BUS error (4.2 BSD)");
			break;
		case  8: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGFPE! " << "Floating-point exception (ANSI)");
			break;
		case  9: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGKILL! " << "Kill, unblockable (POSIX)");
			break;
		case 10: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGUSR1! " << "User-defined signal 1 (POSIX)");
			break;
		case 11: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGSEGV! " << "Segmentation violation (ANSI)");
			break;
		case 12: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGUSR2! " << "User-defined signal 2 (POSIX)");
			break;
		case 13: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGPIPE! " << "Broken pipe (POSIX)");
			break;
		case 14: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGALRM! " << "Alarm clock (POSIX)");
			break;
		case 15: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGTERM! " << "Termination (ANSI)");
			break;
		case 16: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGSTKFLT! " << "Stack fault");
			break;
		case 17: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGCHLD! " << "Child status has changed (POSIX)");
			break;
		case 18: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGCONT! " << "Continue (POSIX)");
			break;
		case 19: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGSTOP! " << "Stop, unblockable (POSIX)");
			break;
		case 20: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGTSTP! " << "Keyboard stop (POSIX)");
			break;
		case 21: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGTTIN! " << "Background read from tty (POSIX)");
			break;
		case 22: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGTTOU! " << "Background write to tty (POSIX)");
			break;
		case 23: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGURG! " << "Urgent condition on socket (4.2 BSD)");
			break;
		case 24: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGXCPU! " << "CPU limit exceeded (4.2 BSD)");
			break;
		case 25: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGXFSZ! " << "File size limit exceeded (4.2 BSD)");
			break;
		case 26: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGVTALRM! " << "Virtual alarm clock (4.2 BSD)");
			break;
		case 27: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGPROF! " << "Profiling alarm clock (4.2 BSD)");
			break;
		case 28: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGWINCH! " << "Window size change (4.3 BSD, Sun)");
			break;
		case 29: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGIO! " << "I/O now possible (4.2 BSD)");
			break;
		case 30: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by SIGPWR! " << "Power failure restart (System V)");
			break;
		default: SG_LOG2 (SG_SYSTEMS, SG_ALERT, "killed by signal " << SigType << "!");
	}
#ifdef _CF_MISC_HXX_
    SG_LOG2 (SG_SYSTEMS, SG_ALERT, "App exit at " << Get_Current_UTC_Time_Stg() << " UTC");
#endif
	exit (0);
}

//////////////////////////////////////////////////////////////////////
// Daemonize ()
// installs the signal-handler and makes ourself a daemon
//////////////////////////////////////////////////////////////////////
int cDaemon::Daemonize () // make us a daemon
{
	pid_t pid;
	int fd; /* file descriptor */

	//
	// fork to get rid of our parent
	//
	if ( (pid = fork ()) < 0)
		return (-1);	// something went wrong!
	else if ( pid > 0 )	// parent-process
	{
		PidOfDaemon = 0;
		exit (0);	// good-bye dad!
	}
	//
	// well, my child, do well!
	//
	setsid ();	// become a session leader

	if ( (pid = fork ()) < 0)
		return (-1);	// something went wrong!
	else if ( pid > 0 )	// parent-process
	{
		exit (0);	// good-bye dad!
	}

	//
	// well, my child's child, do well!
	//
	PidOfDaemon = getpid();
	SG_LOG2 (SG_SYSTEMS, SG_ALERT, "# My PID is " << PidOfDaemon);

	chdir ("/");	// make sure, we're not on a mounted fs
	umask (0);	// clear the file creation mode

	for ( fd = 0; fd < _NFILES; fd++ ) {
	    close( fd ); /* close 'std' file handles */
	}

	return (0);	// ok, that's all volks!
}

//////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////
void cDaemon::KillAllChildren ()  // kill our children and ourself
{
	list <pid_t>::iterator aChild;

	aChild = Children.begin ();
	while ( aChild != Children.end () )
	{
		SG_LOG (SG_SYSTEMS, SG_ALERT, "cDaemon: killing child " << (*aChild));
		if ( kill ((*aChild), SIGTERM))
			kill ((*aChild), SIGKILL);
		aChild++;
	}
	Children.clear ();
	// exit (0);
}

//////////////////////////////////////////////////////////////////////
// AddChild ()
// inserts the ChildsPid in the list of our Children.
// So we can keep track of them and kill them if necessary,
// e.g. the daemon dies.
//////////////////////////////////////////////////////////////////////
void cDaemon::AddChild ( pid_t ChildsPid )
{
	Children.push_back (ChildsPid);
}

int cDaemon::NumChildren ()
{
	return (Children.size ());
}

cDaemon::cDaemon()
{
	//
	// catch some signals
	//
	signal (SIGINT,SigHandler);
	signal (SIGHUP,SigHandler);
	signal (SIGTERM,SigHandler);
	signal (SIGCHLD,SigHandler);
	signal (SIGPIPE,SigHandler);
	PidOfDaemon = getpid(); 
}

cDaemon::~cDaemon ()
{
	// KillAllChildren ();
}

#endif // !_MSC_VER

// vim: ts=2:sw=2:sts=0

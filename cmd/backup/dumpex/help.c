/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ident	"@(#)help.c 1.6 93/10/15"

#include "structs.h"
#include <locale.h>
#include <curses.h>
#include <string.h>

#define	MAXHELP		11

static WINDOW	*helpwin;
static int	help_lines;

static char *helptext[HELP_MAX][2][MAXHELP]; /* help text, initialized below */
static char **hsection;		/* pointer to current help section */
static char *hp;		/* pointer to current help text */
static int y;			/* current screen line */
static int nextbreak;		/* "y" of next prompt break */
static int lastscreen;		/* on last screen-full */

#ifdef __STDC__
static void helpmsg(char *);
static void helplines(void);
#else
static void helpmsg();
static void helplines();
#endif

void
helpinit(rows, columns)
	int	rows;
	int	columns;
{
	help_lines = rows;
	helpwin = newwin(rows, columns, 0, 0);
	if (helpwin == (WINDOW *)0) {
		(void) fprintf(stderr, gettext(
			"%s: cannot initialize help window"), progname);
		exit(1);
	}
	scrollok(helpwin, TRUE);
#ifdef USG
	idlok(helpwin, TRUE);		/* for smooth scrolling */
#endif

	/*
	 * XGETTEXT:  Because the target string of a "msgstr"
	 * directive must be less then 2048 characters in length,
	 * the help text is divided into sections.  Each help
	 * section consists of one or more lines of text that
	 * should be formatted the way you wish them to appear
	 * on the screen.  The default/native text is formatted
	 * for a screen size of 80 [or more] columns.
	 */
	helptext[HELP_MAIN][0][0] = gettext(
"Main Menu Help Screen (Normal Mode):\n");

	helptext[HELP_MAIN][1][0] = gettext(
"Main Menu Help Screen (Expert Mode):\n");

	helptext[HELP_MAIN][0][1] = helptext[HELP_MAIN][1][1] = gettext(
"\n\
The dumped program edits a configuration file created\n\
by dumpconfig.  From this Main Menu you can:\n\
\n\
Choose option:               To:\n\
\n\
      a                      Add to, delete from, and edit the list\n\
                             of local and remote filesystems that\n\
                             this configuration dumps.\n\
\n\
      b                      Add to, delete from, and edit the list of\n\
                             tape devices this configuration\n\
                             uses.  Change the order in which\n\
                             the tape devices are used.\n\
\n\
      c                      Add to, delete from, or edit the list of\n\
                             users to receive e-mail\n\
                             notification when dumpex finishes.\n\
\n\
      d                      Schedule automatic execution of\n\
                             dumps by day and time.\n\
\n\
      e                      Specify how long to reserve a tape\n\
                             before reusing it.\n\
\n\
      f                      Specify a login, other than root, to\n\
                             run remote dumps and access remote\n\
                             tapes.  See \"Remote User Help\" below for\n\
                             more information.\n");

	helptext[HELP_MAIN][1][2] = gettext(
"\n\
      g                      Change the tape library name for\n\
                             this configuration (not necessary\n\
                             in normal operation).  See \"Tape library\n\
                             name Help\" below for more information.\n\
\n\
      h                      Change the host location of the\n\
                             dump database daemon.  See \"Dump library\n\
                             machine name Help\" below for more information.\n\
\n\
      i                      Toggle long-play mode (to fill each\n\
                             tape to capacity before switching\n\
                             to another tape).  See \"Long-play mode Help\"\n\
                             below for more information.\n\
\n\
      j                      Change the number of tapes to reserve at\n\
                             each invocation of dumpex.  See \"Tapes up Help\"\n\
                             below for more information.\n\
\n\
      k                      Change the size of blocks written\n\
                             to the tape device.  See \"Blocking Factor Help\"\n\
                             below for more information.\n\
\n\
      l                      Change the number of dump sets that\n\
                             this configuration uses.  See \"Dumpsets Help\"\n\
                             below for more information.\n");

	helptext[HELP_MAIN][0][2] = helptext[HELP_MAIN][1][3] = gettext(
"\n\
      m                      Change the temp directory used by dumpex.\n\
                             This is helpful if the default temp directory,\n\
                             /var/tmp, is an NFS mounted file system.  The\n\
                             temp directory must be a locally mounted ufs\n\
                             file system.\n\
\n\
      q                      Quit dumped and save or discard\n\
                             any changes you have made.  See \"Quit Help\"\n\
                             below for more information.\n\
\n\
      x                      Toggle expert mode on or off.  See \"Expert\n\
                             Mode Toggle Help\" below for more information.\n\
\n\
      ?                      Get online help.\n");

	helptext[HELP_MAIN][0][3] = helptext[HELP_MAIN][1][4] = gettext(
"\n\
---------------------------------------------------------------------------\n\
\n\
          Remote User Help (option 'f')\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Remote dump user option on the Main Menu allows you to\n\
          change the name of the remote user.  The specified user name\n\
          must be a valid login name and have a home directory which has\n\
          been mounted from all remote hosts listed in this backup\n\
          configuration.  The user must be in group \"sys\" for backing up\n\
          remote Solaris 2.x hosts running Online: Backup, and in group\n\
          \"operator\" for backing up remote SunOS 4.1.x hosts running Backup\n\
          Copilot.  Further, the specified user must have a \".rhosts\"\n\
          file within its home directory that contains the host names of\n\
          all the machines that are backed up by this configuration.\n\
\n\
          ========  Steps  ========\n\
\n\
          To change the name, type in a new name and press Return.\n\
\n\
          Pressing Return without typing in a new name\n\
          discards any changes (provided you have\n\
          not quit and saved the configuration file).\n");

	helptext[HELP_MAIN][1][5] = gettext(
"\n\
---------------------------------------------------------------------------\n\
\n\
          Tape library name Help (option 'g')\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Tape library name option on the Main Menu allows you to\n\
          change the name of the tape library.\n\
\n\
          ========  Steps  ========\n\
\n\
          To change the name, type in a new name and press\n\
          Return.\n\
\n\
          Pressing Return without typing in a new name\n\
          keeps any changes you have made.\n\
\n\
---------------------------------------------------------------------------\n\
\n\
          Dump library machine name Help (option 'h')\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Dump library machine name option on the Main Menu\n\
          allows you to change the name of the dump library machine.\n\
\n\
          Caution: Changing the database host without first\n\
                   moving current database information to the\n\
                   new host may cause loss of database information.\n\
\n\
          ========  Steps  ========\n\
\n\
          To change the name, type in a new name and press\n\
          Return.\n\
\n\
          Pressing Return without typing in a new name\n\
          retains any changes you have made.\n\
\n\
---------------------------------------------------------------------------\n\
\n\
          Long-play mode Help (option 'i')\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Long-play mode option on the Main Menu allows you to\n\
          toggle off and on the long-play mode feature.\n\
\n\
          ========  Steps  ========\n\
\n\
          Press 'i' to toggle the long-play setting from on to off, or\n\
          from off to on.\n\
\n\
---------------------------------------------------------------------------\n\
\n\
          Tapes up Help (option 'j')\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Tapes up option on the Main Menu allows you to\n\
          change the number of tapes that dumpex reserves when invoked.\n\
          Most sites will not need to change this value from the default\n\
          of 0.\n\
\n\
          ========  Steps  ========\n\
\n\
          To change the number, type in a new number and press\n\
          Return.\n\
\n\
          Pressing Return without typing in a new name\n\
          retains any changes you have made.\n\
\n\
          Typing in a non-numerical value sets the\n\
          Tapes up value to 0.\n\
\n\
---------------------------------------------------------------------------\n\
\n\
          Blocking Factor Help (option 'k')\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Blocking Factor option on the Main Menu allows you to\n\
          specify the blocking factor you want to use when writing to\n\
          the tape device.\n\
\n\
          The blocking factor is described in 512-byte units.\n\
          An entry, therefore, of 112 is 56 Kbytes record size.\n\
\n\
          ========  Steps  ========\n\
\n\
          To change the number, type in a new number and press\n\
          Return.\n\
\n\
          Pressing Return without typing in a new name\n\
          retains any changes you have made.\n\
\n\
          Typing in a non-numerical value sets the\n\
          Blocking Factor value to 0, which is not a valid entry.\n\
\n\
---------------------------------------------------------------------------\n\
\n\
          Dumpsets Help (option 'l')\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Dumpsets option on the Main Menu allows you to\n\
          change the number of parallel dumpsets to run.\n\
\n\
          This option permits you to run parallel sets of\n\
          dumps with similar sequences of dump levels.  Each\n\
          sequence maintains its own dumpdates file.\n\
\n\
          ========  Steps  ========\n\
\n\
          To change the number, type in the number of dumpsets\n\
          to run in parallel and press Return.\n\
\n\
          Pressing Return without typing in a new name\n\
          retains any changes you have made.\n");

	helptext[HELP_MAIN][0][4] = helptext[HELP_MAIN][1][6] = gettext(
"\n\
---------------------------------------------------------------------------\n\
\n\
          Quit Help (option 'q')\n\
\n\
\n\
          ========  Quit  ========\n\
\n\
          The Quit option on the Main Menu quits the\n\
          dumped program.\n\
\n\
          If you have made any changes to the configuration,\n\
          dumped will ask you if you want to save the changes.\n\
\n\
          To accept the default (y for Yes), press Return.\n\
\n\
          To discard any changes you have made, type n for No.\n\
\n\
---------------------------------------------------------------------------\n\
\n\
          Expert Mode Toggle Help (option 'x')\n\
\n\
\n\
          ========  Expert Mode Toggle  ========\n\
\n\
          The Expert Mode Toggle option (x) on the Main Menu permits\n\
          toggling between off and on for expert mode.\n\
\n\
          Toggling expert mode on produces a list of more\n\
          options (g-l).  Toggling expert mode off\n\
          shortens the list of options to a-f.\n\
\n\
          Both expert mode and regular mode have the\n\
          q (Quit), x (Expert Mode Toggle), and ? (Help) options.\n");

	helptext[HELP_FS][0][0] = helptext[HELP_FS][1][0] = gettext(
"\n\
\n\
          Dump File System Editor Menu Help\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Dump File System Editor menu allows you to\n\
          add, delete, and edit file system information for the\n\
          current configuration file.\n\
\n\
          ========  Scrolling ========\n\
\n\
          To scroll through the list of file systems, press\n\
          the + (plus) key to scroll forward, and the - (minus)\n\
          key to scroll backward.\n\
\n\
\n\
          ========  Option a  ========\n\
\n\
        >  Purpose:   Adds a file system or machine to the current\n\
                      configuration file.\n\
\n\
        >  Menu Steps:\n\
\n\
            1) Enter the ID number of the file system or machine\n\
               *after* which you want to add the new file system\n\
               or machine.  0 begins the list.  Pressing Return to accept\n\
               the default (the end of the list) is usually appropriate\n\
\n\
            2) Type the name of the file system or machine.  To add\n\
               a single file system, just enter its name here.  To add\n\
               all file systems from a particular host, include a\n\
               + (plus sign) with no space before the machine\n\
               name (for example, +mymachine).\n\
\n\
            3) Choose a dump sequence template, specify your own sequence,\n\
               or accept the default sequence.  Pressing Return here is\n\
               usually the correct response, as it will copy the dump\n\
               sequence template from the first file system in the list.\n\
\n\
               The periods after the templates indicate that\n\
               the sequences repeat.\n\
\n\
             a. Templates:\n\
\n\
                 -Template 1 will dump one level-0, followed\n\
                  by four level 5 dumps (and continuously repeat).\n\
\n\
                 -Template 2 will dump one level-0, followed by four\n\
                  level 9, then a level 5, followed by four level 9 dumps.\n\
\n\
                 -Template 3 will dump one level-0, followed by four\n\
                  true incremental dumps.\n\
\n\
             b. Other: Customize a dump sequence by\n\
                       typing in your own sequence.\n\
\n\
             c. Return:  Accept the default sequence, which will look\n\
                         like sequence number 1.\n\
\n\
\n\
          ========  Option d  ========\n\
\n\
        >  Purpose:   Deletes a file system or machine in the current\n\
                      configuration file.\n\
\n\
        >  Menu Steps:\n\
\n\
               Enter the ID number of the file system or machine\n\
               that you want to delete and press Return.\n\
\n\
\n\
         ========  Option e  ========\n\
\n\
        > Purpose: Edits single file system information.\n\
\n\
        > Menu Steps:\n\
\n\
               1) Enter the ID number of the file system you want\n\
                  to edit.\n\
\n\
               2) The next screen shows the name of the file system\n\
                  (FS), the dump level sequence for that file system,\n\
                  and four menu choices (f, l, <,>, and q).  See the\n\
                  online help within that screen for more information.\n\
\n\
\n\
       ========  Option q  ========\n\
\n\
       > Purpose: Quits this screen and returns you to the Main Menu.\n\
\n\
\n\
       ========  Option s, > (align), and u   ========\n\
\n\
       > Purpose: Staggers dump level sequences for all file systems.\n\
                  The pointers (>) point to the next level dump\n\
                  to run.\n\
\n\
                  Note: If you stagger the dumps, the pointers don't\n\
                  change position (they merely rotate along with\n\
                  the sequence).  To make the pointers point at the\n\
                  first dump in the sequence, press the > (align)\n\
                  key.\n\
\n\
      > Menu Steps:\n\
\n\
             1) Press s to stagger the dump sequences for all\n\
                fle systems.\n\
\n\
             2) Press the > (align) character to force the pointers\n\
                to point to the first dump in the sequences.\n\
\n\
             3) Press u to unstagger the dump sequences.\n");

	helptext[HELP_FS_EDIT][0][0] = helptext[HELP_FS_EDIT][1][0] = gettext(
"\n\
\n\
          Dump Single File System Editor Menu Help\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Dump Single File System Editor menu allows you to\n\
          change the file system name, assign new dump levels to the\n\
          file system, or change the location of the '>' pointer to\n\
          force the next dump to be done at the specified dump level.\n\
\n\
\n\
         ========  Option f  ========\n\
\n\
        > Purpose: Edits the name of the file system.\n\
\n\
        > Menu Steps:\n\
\n\
               To change the file system name, type in a new name and\n\
               press Return.\n\
\n\
               Pressing Return without typing in a new name\n\
               retains any changes that you have made.\n\
\n\
\n\
         ========  Option l  ========\n\
\n\
        > Purpose: Edits the dump level sequence of the file system.\n\
\n\
        > Menu Steps:\n\
\n\
                 Choose a dump level sequence template (menu items 1-3),\n\
                 or specify your own sequence (menu item 4).\n\
\n\
                     -If you choose a template (1-3), a prompt\n\
                      asks you to choose the length and, if necessary,\n\
                      sublength of the sequence.\n\
\n\
                     -If you create your own seqence, use digits and x\n\
                      to specify the sequence (digits for dumps from\n\
                      levels 0-9, and x for true incremental dumps).\n\
\n\
\n\
         ========  Option <  ========\n\
\n\
        > Purpose: Rotates dump levels to the left.\n\
\n\
\n\
         ========  Option >  ========\n\
\n\
        > Purpose: Advances the '>' pointer to the right.  This is a\n\
                   convenient way to specify the dump level that will\n\
                   be performed for this file system at the next\n\
                   invocation.\n\
\n\
\n\
        ========  Option q  ========\n\
\n\
       > Purpose: Quits this screen and returns you to the\n\
                  Dump File System Editor Menu.\n");

	helptext[HELP_DEVS][0][0] = helptext[HELP_DEVS][1][0] = gettext(
"\n\
\n\
          Dump Device Editor Menu Help\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Dump Device Editor menu allows you to\n\
          add, delete, and edit tape device information\n\
          for the current configuration file.\n\
\n\
          ========  Scrolling ========\n\
\n\
          To scroll through the list of tape devices, press\n\
          the + (plus) key to scroll forward and the - (minus)\n\
          key to scroll backward.\n\
\n\
          Note: You must press Return after most prompts.\n\
\n\
\n\
          ========  Option a  ========\n\
\n\
        >  Purpose:   Adds a tape device to the current\n\
                      configuration file.\n\
\n\
        >  Menu Steps:\n\
\n\
            1) Enter the ID number of the tape device *after*\n\
               which you want to add the new device and press Return.\n\
               0 begins the list.\n\
\n\
            2) Type the name of the tape device and press Return.\n\
\n\
\n\
          ========  Option d  ========\n\
\n\
        >  Purpose:   Deletes a tape device in the current\n\
                      configuration file.\n\
\n\
        >  Menu Steps:\n\
\n\
               Enter the ID number of the file system or machine\n\
               that you want to delete and press Return.\n\
\n\
\n\
         ========  Option e  ========\n\
\n\
        > Purpose: Edits tape device information.\n\
\n\
        > Menu Steps:\n\
\n\
               1) Enter the ID number of the tape device you want\n\
                  to edit.\n\
\n\
               2) Type the name of the device you want to replace\n\
                  at that ID number position and press Return.\n\
                  (Example: /dev/rmt/0bn)\n\
\n\
\n\
       ========  Option q  ========\n\
\n\
       > Purpose: Quits this screen and returns you to the Main Menu.\n");

	helptext[HELP_MAIL][0][0] = helptext[HELP_MAIL][1][0] = gettext(
"\n\
\n\
          Dump Mail Recipient Menu Help\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Dump Mail Recipient Editor menu allows you to\n\
          add, delete, and edit information about e-mail recipients\n\
          for the current configuration file.  The e-mail addresses\n\
          you specify here will receive automatic notification\n\
          about successful or unsuccessful completion of dumps,\n\
          including error messages (if any are generated).\n\
\n\
\n\
          ========  Scrolling ========\n\
\n\
          To scroll through the list of mail recipients, press\n\
          the + (plus) key to scroll forward and the - (minus)\n\
          key to scroll backward.\n\
\n\
          Note: You must press Return after most prompts.\n\
\n\
\n\
          ========  Option a  ========\n\
\n\
        >  Purpose:   Adds a mail recipient to the current\n\
                      configuration file.\n\
\n\
        >  Menu Steps:\n\
\n\
            1) Enter the ID number of the mail recipient *after*\n\
               which you want to add the new mail recipient.\n\
               Then press Return.  0 begins the list.\n\
\n\
            2) Type the name of the mail recipient and press Return.\n\
\n\
\n\
          ========  Option d  ========\n\
\n\
        >  Purpose:   Deletes a mail recipient in the current\n\
                      configuration file.\n\
\n\
        >  Menu Steps:\n\
\n\
               Enter the ID number of the mail recipient\n\
               that you want to delete and press Return.\n\
\n\
\n\
         ========  Option e  ========\n\
\n\
        > Purpose: Edits mail recipient information.\n\
\n\
        > Menu Steps:\n\
\n\
               1) Enter the ID number of the mail recipient you want\n\
                  to edit and press Return.\n\
\n\
               2) Type the name of the mail recipient you want to replace\n\
                  at that ID number position and press Return.\n\
\n\
\n\
       ========  Option q  ========\n\
\n\
       > Purpose: Quits this screen and returns you to the Main Menu.\n");

	helptext[HELP_SCHED][0][0] = helptext[HELP_SCHED][1][0] = gettext(
"\n\
\n\
          Dump Scheduling Editor Menu Help\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          The Dump Scheduling Editor menu allows you to\n\
          schedule automatic dump execution for the current\n\
          configuration file.\n\
\n\
\n\
          ========  Option a  ========\n\
\n\
        >  Purpose:   Toggles automatic dumps on or off.\n\
                      (Default is Yes).  If this option is set to\n\
                      No, then the remaining settings are effectively\n\
                      ignored.\n\
\n\
\n\
          ========  Option d  ========\n\
\n\
        >  Purpose:   Sets the time for dumps to run.\n\
\n\
        >  Menu Steps:\n\
\n\
                  Type a 4-digit (24-hour) time to start dumps,\n\
                  then press Return. (Example: 0100, for 1:00 a.m.)\n\
\n\
                  If you want to accept the default (the current setting),\n\
                  simply press Return.\n\
\n\
\n\
         ========  Option t  ========\n\
\n\
        > Purpose: Sets a time to send e-mail reminder to mount\n\
                   tapes.\n\
\n\
        > Menu Steps:\n\
\n\
               1) Type a 4-digit (24-hour) time to set automatic\n\
                  e-mail notification of tape mount requests.\n\
                  Then press Return.\n\
\n\
                  If you want to accept the default (the current setting),\n\
                  simply press Return.\n\
\n\
\n\
       ========  Option e  ========\n\
\n\
       > Purpose:   Allows editing of dump schedule information for\n\
                    each day of the week.  You can enable dumpex execution\n\
                    on any particular day of the week, and specify\n\
                    whether a new tape should be used on that day.\n\
\n\
        > Menu Steps:\n\
\n\
               1) Enter the ID number of the day for which you want\n\
                  to change scheduling information.\n\
\n\
               2) Type n to disable dumps for this day.  Type y to\n\
                  enable dumps for this day.  Press Return to leave\n\
                  the setting unchanged.\n\
\n\
               3) If you enabled dumps for a particular day,\n\
                  you must specify whether you want to switch to a\n\
                  new tape when a dump runs on that day.  Press Return\n\
                  to leave this setting unchanged.\n\
\n\
\n\
       ========  Option q  ========\n\
\n\
       > Purpose: Quits this screen and returns you to the Main Menu.\n\
\n\
              NOTE: Make sure you have toggled Automatic Dump Execution\n\
                    to Yes if you want your dumps to run automatically.\n");

	helptext[HELP_KEEP][0][0] = gettext(
"\n\
\n\
          Setting Tape Expiration Data Help Screen\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          This screen allows you to set tape expiration information\n\
          for the current configuration file.  You can add, delete,\n\
          and edit expiration settings from this screen.\n\
\n\
\n\
          ========  Default Settings  ========\n\
\n\
          The default settings to expire tapes are:\n\
\n\
          For every level-0 dump, expire the tape after 60 days.\n\
          For every level-5 dump, expire the tape after 30 days.\n\
          For every level-x dump, expire the tape after 14 days.\n\
\n\
          SunSoft recommends that you not change any of the default\n\
          settings in this screen unless you have read the\n\
          documentation thoroughly and know what you are doing.\n\
\n\
\n\
          ========  Expert Mode Help  ========\n\
\n\
          For expert-mode help, press q to quit to the Main Menu,\n\
          toggle Expert Mode on, and press e to return to the\n\
          tape expiration settings screen.  Then press ? (question mark).\n\
\n\
          To return to the Main Menu, press q for quit.\n");

	helptext[HELP_KEEP][1][0] = gettext(
"\n\
\n\
          Setting Tape Expiration Data Help Screen\n\
                     Expert Mode\n\
\n\
\n\
          ========  Purpose  ========\n\
\n\
          This screen allows you to set tape expiration information\n\
          for the current configuration file.  You can add, delete,\n\
          and edit expiration settings from this screen.\n\
\n\
\n\
          ========  Default Settings  ========\n\
\n\
          The default settings to expire tapes are:\n\
\n\
          For every level-0 dump, expire the tape after 60 days.\n\
          For every level-5 dump, expire the tape after 30 days.\n\
          For every level-x dump, expire the tape after 14 days.\n\
\n\
          SunSoft recommends that you not change any of the default\n\
          settings in this screen unless you have read the\n\
          documentation thoroughly and know what you are doing.\n\
\n\
          To return to the Main Menu, press q for quit.\n\
\n\
          For general information on this screen, see the\n\
          General Information section, following the options\n\
          help text.\n\
\n\
\n\
          =======  Option a  ========\n\
\n\
        >  Purpose:   To add a dump level to the \"Level\" field.\n\
\n\
        >  Menu Steps:\n\
\n\
              1)  Type a dump-level number (0-9 or x\n\
                  for a true incremental dump) and press Return.\n\
\n\
                  If you want to accept the default (level-0),\n\
                  press Return.\n\
\n\
             2)   Type in a multiple number and press Return.\n\
                  This number specifies how many times a dump\n\
                  will run before triggering the expiration\n\
                  criteria in the \"Days\" and \"Onshelf\" fields.\n\
\n\
             3)   Type in the number of days before tapes expire\n\
                  and press Return.\n\
\n\
                  If you want to accept the default (60),\n\
                  press Return.\n\
\n\
             4)   Type a number for the Onshelf field and press Return.\n\
                  (For more information on the\"Onshelf\" field, see the\n\
                  General Information section below.)\n\
\n\
                  If you want to accept the default (0),\n\
                  press Return.\n\
\n\
\n\
          ========  Option d  ========\n\
\n\
        >  Purpose:   Sets the time for dumps to run.\n\
\n\
        >  Menu Steps:\n\
\n\
                Type the ID number of the tape expiration setting\n\
                you want to delete.  Return to delete the setting.\n\
\n\
\n\
         ========  Option e  ========\n\
\n\
        > Purpose:  Allows editing of tape expiration settings.\n\
\n\
        > Menu Steps:\n\
\n\
               1) Type the ID number of the tape expiration setting\n\
                  you want to edit and press Return.\n\
\n\
               2) Type a new dump level (0-9 or x for a true\n\
                  incremental dump) and press Return.\n\
\n\
               3) Type a new multiple number and press Return.\n\
                  This number specifies how many times a\n\
                  dump will run before triggering the expiration\n\
                  criteria in the \"Days\" and \"Onshelf\" fields.\n\
\n\
               4) Type in the number of days before which tapes\n\
                  cannot expire.\n\
\n\
               5) Type a number for the Onshelf field. (For more\n\
                  information on the \"Onshelf\" field, see the\n\
                  General Information section below.)\n\
\n\
                  If you want to accept the default (3),\n\
                  press Return.\n\
\n\
\n\
       ========  Option q  ========\n\
\n\
       > Purpose: Quits this screen and returns you to the Main Menu.\n\
\n\
\n\
       ========  General Information  ========\n\
\n\
\n\
      --> Tip: Most users will find the Days settings adequate\n\
               (and more intuitive) for setting tape expirations.\n\
               If you choose to use Onshelf settings, be aware of\n\
               the complex calculations that may comprise your\n\
               tape expiration settings.\n\
\n\
\n\
          Dump levels range from level-0 to level-9, as well as level-x,\n\
          or \"true incremental,\" dumps.\n\
\n\
          The dump execution program, dumpex, chooses a tape expiration\n\
          setting based on the dump level and its multiple.\n\
\n\
          The Multiple setting specifies how many times you want\n\
          to repeat a level before applying the Days and Onshelf\n\
          expiration settings.  When dumpex, the program that runs\n\
          your dumps, finishes dumping each file system, it uses\n\
          the Level and Multiple settings as \"gating\" criteria for\n\
          setting the Days and Onshelf values.\n\
\n\
\n\
      --> Tip: Specify Multiple settings other than 1 only for\n\
               full (level-0) dumps.\n\
\n\
\n\
          The Onshelf setting means that, given the following:\n\
\n\
                y = dump level\n\
                n = value of Multiple field\n\
                x = value of Onshelf field\n\
\n\
          for every nth dump of level y, tapes will not expire until\n\
          at least x many more dumps at level y have been completed.\n\
\n\
\n\
      --> Tip: Think of \"multiple of 1\" as equal to \"every.\"\n\
\n\
\n\
          Examples:\n\
\n\
          Id  Level    Multiple   Days   Onshelf\n\
\n\
          1    0         1         30      0\n\
          2    0         3         60      0\n\
          3    0         4         90      2\n\
\n\
\n\
          In the first line, a full dump (level-0) will not\n\
          expire until 30 days have passed.\n\
\n\
          In the second line, a full dump will run 3 times\n\
          before the \"Days\" setting can apply to the tapes.\n\
\n\
          In the third line, a full dump will run 4 times before\n\
          the combination of the \"Days\" and \"Onshelf\" settings\n\
          determine the expiration settings.  The Onshelf value\n\
          of 2 ensures that at least 2 more level-0 dumps accrue\n\
          before the tape is returned to the tape pool for reuse.\n\
\n\
          The following table shows how tapes are expired under\n\
          the expiration scheme in the examples above.\n\
\n\
\n\
                      Tape 1    Tape 2    Tape 3    Tape 4\n\
\n\
           Level:       0         0          0         0\n\
\n\
           Keep for:    30        30         60        90*\n\
\n\
\n\
                        *(and until 2 more level-0's have been done)\n\
\n\
\n\
          Tape 1 runs a full dump.  Tape expiration settings are set\n\
          to 30 days (because the multiple of 1 criterion always\n\
          applies).\n\
\n\
          Tape 2 runs a full dump.  Tape expiration settings remain\n\
          at 30 days (because, again, the multiple of 1 applies).\n\
\n\
          Tape 3 runs a full dump, but the tape expiration settings\n\
          change to 60 days (because the multiple of 3 now applies).\n\
\n\
          Tape 4 runs a full dump.  The tape expiration settings are\n\
          now determined by both the \"Days\" and \"Onshelf\" values.\n\
          The tapes will not expire until both conditions are true:\n\
          90 days have passed and at least 2 more level-0 dumps\n\
          have been done.\n");
}

static void
helpmsg(prompt)
	char	*prompt;
{
	wmove(helpwin, help_lines-1, 0);
	wclrtoeol(helpwin);
	wstandout(helpwin);
	waddstr(helpwin, prompt);
	wstandend(helpwin);
/*	dorefresh(helpwin, TRUE); */
	wrefresh(helpwin);
}

static void
#ifdef __STDC__
helplines(void)
#else
helplines()
#endif
{
	char	*nl;

	while (hp && *hp && y != nextbreak) {
		wmove(helpwin, y++, 0);
		nl = strchr(hp, '\n');
		if (nl != (char *)0) {
			waddnstr(helpwin, hp, (nl + 1) - hp);
			hp = nl + 1;
		} else {
			waddstr(helpwin, hp);
			hp = (char *)0;
		}
		wclrtoeol(helpwin);
		/*
		 * get next help section if done
		 * with this one
		 */
		if ((hp == (char *)0 || *hp == '\0') && *hsection != (char *)0)
			hp = *++hsection;
	}
}

void
#ifdef __STDC__
scr_help(int category, int expert)
#else
scr_help(category, expert)
	int category;
	int expert;
#endif
{
	char *quit = gettext("quit");
	WINDOW *savescr;
	int key;

	wclear(helpwin);
	nextbreak = help_lines-1;
	lastscreen = 0;
	y = 0;

	/*
	 * Find out if localized help text is available.
	 * Note that the text "help section" appears in
	 * msgid strings in the catalog and thus should
	 * not be wrapped by gettext().
	 */
	hsection = helptext[category][expert ? 1 : 0];
	hp = *hsection;

	for (;;) {
		helplines();			/* display some help text */

		if (hp == (char *)0 || *hp == '\0') {
			helpmsg(gettext("Press any key to return to dumped "));
			lastscreen = 1;
			savescr = stdscr;
			stdscr = helpwin;
			(void) zgetch();
			stdscr = savescr;
			break;
		} else {
			helpmsg(gettext(
		"'Return' next line, 'Space Bar' next page, 'q' exit help "));
			savescr = stdscr;
			stdscr = helpwin;
			key = zgetch();
			stdscr = savescr;
			if (key == '\n')	/* next line */
				nextbreak++;
			else if (key == ' ')	/* next screen */
				nextbreak += (help_lines-1);
			else if (key == quit[0]) /* quit */
				break;
		}
		(void) tcflush(fileno(stdin), 0);
		wmove(helpwin, help_lines-1, 0);
		wclrtoeol(helpwin);
	}
	/*
	 * This clear() is needed because some remnants of the help screen
	 * were messing up the subsequent menu display on shelltools and
	 * cmdtools. xterms did not have this problem, so it is not obvious
	 * where the bug is located (in dumped or shelltool).
	 */
	clear();
}

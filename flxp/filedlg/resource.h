// Resource ids of rc/filedlg.rc. The file dialog itself keeps comdlg32's
// control ids (dlgs.h: stc1..4, cmb1/2/13, edt1, lst1, ctl1, chx1, pshHelp).
#pragma once

#define IDD_FILEDLG         1552    // NEWFILEOPENV2ORD, XP's Explorer-style template with the places bar
#define IDD_FD_MESSAGE      200
#define IDD_FILEDLG_LH      1553    // the Longhorn look: the bottom controls (the rest is created and laid out in code)
#define IDC_FD_HIDE         2010    // Hide Explorer / Browse Folders (Save)

#define IDC_FD_MSG_ICON     2001
#define IDC_FD_MSG_TEXT     2002

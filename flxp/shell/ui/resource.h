/* The Properties sheet's resource ids (flxp/rc/shell.rc; shell::properties, properties.cpp) */
#pragma once
#define IDD_GENERAL       100
#define IDD_METADATA      101
#define IDD_NEWATTR       102

/* General */
#define IDC_GICON         110
#define IDC_NAME          111
#define IDC_TYPEL         112
#define IDC_TYPE          113
#define IDC_OPENSL        114
#define IDC_OPENS         115
#define IDC_OPENSICON     199   // made at run time (properties.cpp show_opens_with): the program's icon
#define IDC_LOCATION      116
#define IDC_GSIZE         117
#define IDC_CONTAINSL     118
#define IDC_CONTAINS      119
#define IDC_MODIFIEDL     120
#define IDC_MODIFIED      121
#define IDC_ACCESSEDL     122
#define IDC_ACCESSED      123
#define IDC_CHANGEDL      124
#define IDC_CHANGED       125
#define IDC_PERMSL        126
#define IDC_PERMS         127
#define IDC_EXTRAL        128
#define IDC_EXTRA         129
#define IDC_SEP3          130
#define IDC_SEP4          131
#define IDC_CHANGE        132

/* Metadata: the simple view's fields; the swatches are IDC_SWATCH0 + i (i < 11: None, then shell::details::palette()) */
#define IDC_DESCL         140
#define IDC_DESC          141
#define IDC_TAGSL         142
#define IDC_TAGEDIT       143
#define IDC_TAGADD        144
#define IDC_TAGLIST       145
#define IDC_TAGREMOVE     146
#define IDC_COLORL        147
#define IDC_COMMENTL      148
#define IDC_COMMENT       149
#define IDC_RUNL          150
#define IDC_RUN           151
#define IDC_RUNBROWSE     152
#define IDC_RUNHINT       153
#define IDC_SWATCH0       180
#define IDC_ADVANCED      161
#define IDC_LIST          162
#define IDC_NEW           163
#define IDC_REMOVE        164
#define IDC_SIMPLE        165

/* New Attribute */
#define IDC_ATTRNAME      170
#define IDC_ATTRVALUE     171

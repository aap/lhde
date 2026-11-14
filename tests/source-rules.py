"""The sources' [files] rules (share/sources/*.ini, tools/lhdeimport.py match_rules) over the member names of a medium, no
Microsoft file read: the XP source's font rule takes Tahoma and the others from AMD64/ and XP's East Asian faces of font
linking (share/fonts/fontlink.ini: MS UI Gothic, Gulim, SimSun, PMingLiU) from AMD64/LANG/, all into fonts/ (an expanded
setup file lands there under its real name), and not the LANG directory's other fonts.
Usage: python3 -I tests/source-rules.py"""
import os
import sys

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(TOP, 'tools'))
import lhdeimport  # noqa: E402

fails = 0


def check(what, ok):
    global fails
    print('%s %s' % ('ok  ' if ok else 'FAIL', what))
    if not ok:
        fails += 1


lhdeimport.log = lambda *a, **k: None
xp = lhdeimport.Source(os.path.join(TOP, 'share', 'sources', 'xp.ini'))
members = ['AMD64/%s.TT_' % n for n in ('MARLETT', 'MICROSS', 'TAHOMA', 'TAHOMABD', 'TREBUC', 'TREBUCBD', 'TREBUCBI', 'TREBUCIT',
                                       'ARIAL', 'LUCON')] + \
          ['AMD64/LANG/%s.TT_' % n for n in ('MSGOTHIC', 'GULIM', 'SIMSUN', 'MINGLIU', 'BATANG', 'MSMINCHO', 'SIMHEI')]
fonts = {m for dest, m, ddir in lhdeimport.match_rules(xp, members) if ddir == 'fonts/'}
check('xp: the eight Western faces into fonts/', all('AMD64/%s.TT_' % n in fonts for n in
                                                     ('MARLETT', 'MICROSS', 'TAHOMA', 'TAHOMABD', 'TREBUC', 'TREBUCBD', 'TREBUCBI', 'TREBUCIT')))
check('xp: msgothic, gulim, simsun, mingliu from LANG/ into fonts/',
      all('AMD64/LANG/%s.TT_' % n in fonts for n in ('MSGOTHIC', 'GULIM', 'SIMSUN', 'MINGLIU')))
check('xp: not Arial, Lucida Console, Batang, MS Mincho, SimHei', not fonts & {'AMD64/ARIAL.TT_', 'AMD64/LUCON.TT_',
      'AMD64/LANG/BATANG.TT_', 'AMD64/LANG/MSMINCHO.TT_', 'AMD64/LANG/SIMHEI.TT_'})
check('xp: twelve font files', len(fonts) == 12)

# the faces fontlink.ini links Tahoma to are the families of those four files (msgothic.ttc holds MS UI Gothic, mingliu.ttc PMingLiU)
links = {}
section = False
for line in open(os.path.join(TOP, 'share', 'fonts', 'fontlink.ini')):
    line = line.strip()
    if not line or line[0] in ';#':
        continue
    if line.startswith('['):
        section = line.lower() == '[systemlink]'
        continue
    if section and '=' in line:
        k, v = line.split('=', 1)
        links[k.strip().lower()] = [x.strip() for x in v.split(',') if x.strip()]
check('fontlink.ini: Tahoma = MS UI Gothic, Gulim, SimSun, PMingLiU', links.get('tahoma') == ['MS UI Gothic', 'Gulim', 'SimSun', 'PMingLiU'])
check('fontlink.ini: Microsoft Sans Serif as Tahoma', links.get('microsoft sans serif') == links.get('tahoma'))

print('%d failure(s)' % fails)
sys.exit(1 if fails else 0)

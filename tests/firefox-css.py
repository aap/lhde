#!/usr/bin/env python3
"""tests/firefox-css.py CSS...: Firefox's own CSS parser over lhde-firefox's userChrome.css files (tests/check-free.sh).

One headless Firefox with a scratch profile in a temporary directory, driven over Marionette (its remote protocol, no
geckodriver): in the browser window's chrome context each file is registered as a user sheet, as Firefox loads
userChrome.css, and unregistered again; every message of the CSS parser about that file (Services.console, category
"CSS Parser") is a failure: a declaration Firefox drops, a selector it rejects. Prints one line per problem, exits 1 when
there is any, 77 when there is no firefox (skipped). Firefox and all its helper processes are ended at the end (its own
process group)."""
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

PREFS = '''user_pref("marionette.port", %d);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.aboutwelcome.enabled", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("browser.startup.page", 0);
user_pref("app.update.enabled", false);
user_pref("layout.css.report_errors", true);
'''

SCRIPT = '''
const [texts] = arguments;
// Firefox reports CSS errors only for a document whose docshell asks for them (the developer tools set this flag); each
// text is parsed as a <style> of the browser window, in chrome, and its parser messages are taken before the next
return (async () => {
window.docShell.cssErrorReportingEnabled = true;
const out = [];
const seen = new Set(Services.console.getMessageArray() || []);
for (const text of texts) {
  const st = document.createElementNS("http://www.w3.org/1999/xhtml", "style");
  st.textContent = text;
  document.documentElement.appendChild(st);
  void st.sheet.cssRules.length;
  await new Promise(r => setTimeout(r, 50));
  const errs = [];
  for (const m of Services.console.getMessageArray() || []) {
    if (seen.has(m)) continue;
    seen.add(m);
    if (m instanceof Ci.nsIScriptError && m.category == "CSS Parser")
      errs.push(m.lineNumber + ": " + m.errorMessage + "  " + (m.sourceLine || "").trim());
  }
  st.remove();
  out.push(errs);
}
return out;
})();
'''


class Marionette:
    def __init__(self, port, deadline):
        while True:
            try:
                self.s = socket.create_connection(('127.0.0.1', port), timeout=5)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.3)
        self.s.settimeout(60)
        self.buf = b''
        self.id = 0
        self.read()                                     # the greeting

    def read(self):
        while b':' not in self.buf:
            self.buf += self.recv()
        n, rest = self.buf.split(b':', 1)
        n = int(n)
        while len(rest) < n:
            rest += self.recv()
        self.buf = rest[n:]
        return json.loads(rest[:n].decode())

    def recv(self):
        d = self.s.recv(65536)
        if not d:
            raise OSError('marionette closed the connection')
        return d

    def call(self, name, params):
        self.id += 1
        msg = json.dumps([0, self.id, name, params]).encode()
        self.s.sendall(b'%d:%s' % (len(msg), msg))
        while True:
            r = self.read()
            if r[0] == 1 and r[1] == self.id:
                if r[2]:
                    raise RuntimeError('%s: %s' % (name, r[2]))
                return r[3]


def expand(text):
    """the text with every var(--lhde-NAME) replaced by the value its :root block gives: a declaration that holds a var()
    is only checked when it is used, so the values are parsed in place as well"""
    vals = dict(re.findall(r'^\s*--(lhde-[\w-]+):\s*(.*?);\s*$', text, re.M))
    for _ in range(3):
        text = re.sub(r'var\(--(lhde-[\w-]+)\)', lambda m: vals.get(m.group(1), m.group(0)), text)
    return text


def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


def main(argv):
    files = [os.path.abspath(f) for f in argv[1:]]
    ff = os.environ.get('FIREFOX') or shutil.which('firefox')
    if not ff:
        print('skip  no firefox')
        return 77
    tmp = tempfile.mkdtemp(prefix='lhde-firefox-css-')
    port = free_port()
    prof = os.path.join(tmp, 'profile')
    os.makedirs(prof)
    with open(os.path.join(prof, 'user.js'), 'w') as f:
        f.write(PREFS % port)
    env = dict(os.environ, MOZ_HEADLESS='1', HOME=tmp, XDG_CACHE_HOME=os.path.join(tmp, 'cache'),
               XDG_CONFIG_HOME=os.path.join(tmp, 'config'), MOZ_CRASHREPORTER_DISABLE='1',
               MOZ_REMOTE_ALLOW_SYSTEM_ACCESS='1')            # Marionette's chrome context
    env.pop('DISPLAY', None)
    env.pop('WAYLAND_DISPLAY', None)
    log = open(os.path.join(tmp, 'firefox.log'), 'w')
    p = subprocess.Popen([ff, '--headless', '--marionette', '--no-remote', '--profile', prof, 'about:blank'],
                         env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
    bad = 0
    try:
        m = Marionette(port, time.time() + 60)
        m.call('WebDriver:NewSession', {'capabilities': {}})
        m.call('Marionette:SetContext', {'value': 'chrome'})
        texts = []
        for f in files:
            with open(f) as fh:
                texts += [fh.read()]
            texts.append(expand(texts[-1]))
        res = m.call('WebDriver:ExecuteScript', {'script': SCRIPT, 'args': [texts]})
        res = res.get('value', res) if isinstance(res, dict) else res
        res = res or []
        for i, f in enumerate(files):
            for k, how in ((2 * i, ''), (2 * i + 1, ' (with the --lhde-* values put in)')):
                for line in (res[k] if k < len(res) else []):
                    print('%s:%s%s' % (f, line, how))
                    bad += 1
        try:
            m.call('Marionette:Quit', {'flags': ['eForceQuit']})
        except (OSError, RuntimeError, ValueError):
            pass
    except (OSError, RuntimeError, ValueError) as e:
        print('FAIL  firefox-css: %s (log: %s)' % (e, os.path.join(tmp, 'firefox.log')))
        bad = -1
    finally:
        try:
            p.wait(10)
        except subprocess.TimeoutExpired:
            pass
        try:
            os.killpg(p.pid, signal.SIGKILL)        # the content and helper processes as well
        except OSError:
            pass
        p.wait()
        log.close()
        if bad >= 0:
            shutil.rmtree(tmp, ignore_errors=True)
    if bad:
        return 1
    print('ok    Firefox %s parses the %d files without a CSS error' % (
        (subprocess.run([ff, '--version'], stdout=subprocess.PIPE, universal_newlines=True).stdout.split() or ['?'])[-1],
        len(files)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

"""Serve a staged site (default public_html/, else build/site) under a sub-path, /armdos/, the way it is
often hosted, so the tests also check that every URL is relative. Used by the web tests;
also runs on its own:

    python3 web/tests/serve.py [SITE_DIR] [--port 8000] [--prefix /armdos/]
"""
import http.server, os, sys, threading

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
# the site ./build.sh stages (public_html/), else what web/tools/build-site.mjs writes by default
SITE = os.path.join(ROOT, 'public_html') if os.path.isfile(os.path.join(ROOT, 'public_html', 'index.html')) else os.path.join(ROOT, 'build', 'site')
PREFIX = '/armdos/'

class Handler(http.server.SimpleHTTPRequestHandler):
    site = SITE
    prefix = PREFIX
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, '.webmanifest': 'application/manifest+json', '.js': 'text/javascript'}
    def log_message(self, *a): pass
    def translate_path(self, path):
        p = path.split('?', 1)[0].split('#', 1)[0]
        if not p.startswith(self.prefix):
            return os.path.join(self.site, '__nothing__')
        parts = [s for s in p[len(self.prefix):].split('/') if s not in ('', '.', '..')]
        return os.path.join(self.site, *parts) + ('/' if p.endswith('/') else '')
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

class Server(http.server.ThreadingHTTPServer):
    # the page opens many requests at once (the streamed hard disk's chunks, the BBS disk):
    # the default listen backlog of 5 lets some be refused ("NetworkError" in Firefox)
    request_queue_size = 128
    daemon_threads = True

def start(site=None, port=0, prefix=PREFIX):
    h = type('H', (Handler,), {'site': site or SITE, 'prefix': prefix})
    srv = Server(('127.0.0.1', port), h)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, f'http://127.0.0.1:{srv.server_address[1]}{prefix}'

if __name__ == '__main__':
    a = sys.argv[1:]
    port = int(a[a.index('--port') + 1]) if '--port' in a else 8000
    prefix = a[a.index('--prefix') + 1] if '--prefix' in a else PREFIX
    rest = [x for i, x in enumerate(a) if not x.startswith('--') and (i == 0 or a[i - 1] not in ('--port', '--prefix'))]
    site = os.path.abspath(rest[0]) if rest else SITE
    srv, url = start(site, port, prefix)
    print(f'serving {site} at {url} (Ctrl+C stops)', flush=True)
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass

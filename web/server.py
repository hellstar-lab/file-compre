#!/usr/bin/env python3
import os, sys, shutil, subprocess, time
from http.server import SimpleHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse
import cgi

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
WEB_DIR = os.path.abspath(os.path.dirname(__file__))
UPLOAD_DIR = os.path.join(WEB_DIR, 'uploads')
OUTPUT_DIR = os.path.join(WEB_DIR, 'output')

def ensure_dirs():
    os.makedirs(UPLOAD_DIR, exist_ok=True)
    os.makedirs(OUTPUT_DIR, exist_ok=True)

def detect_cli():
    # Prefer native universal CLI built under build/cli
    candidates = [
        os.path.join(ROOT, 'build', 'cli', 'universal'),
        # macOS app bundle: if running inside afc_gui.app, use bundled neighbor (unlikely here, but safe)
        os.path.join(ROOT, 'build', 'src', 'gui', 'afc_gui.app', 'Contents', 'MacOS', 'universal'),
        # Fallback to legacy names if present
        os.path.join(ROOT, 'build', 'cli', 'compress_file'),
        os.path.join(ROOT, 'build', 'cli', 'decompress_file'),
        # Windows-style exe built locally
        os.path.join(ROOT, 'bin', 'universal_comp.exe'),
        os.path.join(ROOT, 'bin', 'universal_decompressor.exe'),
    ]
    for c in candidates:
        if os.path.exists(c) and os.access(c, os.X_OK):
            return c
    return None

CLI = detect_cli()
if not CLI:
    print('ERROR: CLI not found. Build it under build/cli/universal.', file=sys.stderr)

class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == '/':
            self.path = '/web/index.html'
            return SimpleHTTPRequestHandler.do_GET(self)
        if parsed.path.startswith('/static/'):
            self.path = '/web/' + parsed.path[len('/static/'):]
            return SimpleHTTPRequestHandler.do_GET(self)
        return SimpleHTTPRequestHandler.do_GET(self)

    def _fail(self, code, msg):
        self.send_response(code)
        self.send_header('Content-Type', 'text/plain; charset=utf-8')
        self.end_headers()
        self.wfile.write(msg.encode('utf-8'))

    def _handle_upload(self):
        ctype, pdict = cgi.parse_header(self.headers.get('Content-Type'))
        if ctype != 'multipart/form-data':
            return None, 'Invalid content type'
        pdict['boundary'] = pdict['boundary'].encode('utf-8')
        pdict['CONTENT-LENGTH'] = int(self.headers.get('Content-Length', 0))
        fs = cgi.FieldStorage(fp=self.rfile, headers=self.headers, environ={'REQUEST_METHOD':'POST', 'CONTENT_TYPE': self.headers['Content-Type']})
        if 'file' not in fs:
            return None, 'Missing file field'
        fitem = fs['file']
        if not fitem.filename:
            return None, 'Empty filename'
        base = os.path.basename(fitem.filename)
        safe = base.replace('..','').replace('/','_')
        path = os.path.join(UPLOAD_DIR, safe)
        with open(path, 'wb') as f:
            shutil.copyfileobj(fitem.file, f)
        return path, None

    def _run_cli(self, mode, in_path):
        if not CLI:
            return None, 'CLI not found on server'
        # Some builds require .comp to reside under OUTPUT_DIR for safety; ensure that.
        if mode == 'decompress':
            base = os.path.basename(in_path)
            safe = base.replace('..','').replace('/','_')
            if not in_path.startswith(OUTPUT_DIR):
                target = os.path.join(OUTPUT_DIR, safe)
                shutil.copy2(in_path, target)
                in_path = target
        args = [CLI, '-c' if mode=='compress' else '-d', in_path, '-o', OUTPUT_DIR]
        p = subprocess.run(args, capture_output=True, text=True)
        if p.returncode != 0:
            return None, p.stderr.strip() or 'Compression failed'
        # Try to parse Output: line from stdout
        out_path = None
        for line in (p.stdout or '').splitlines():
            if line.startswith('Output:'):
                out_path = line[len('Output:'):].strip()
                break
        if not out_path:
            # Fallback guess
            base = os.path.basename(in_path)
            if mode=='compress':
                out_path = os.path.join(OUTPUT_DIR, base + '.comp')
            else:
                # strip .comp if present
                if base.endswith('.comp'):
                    out_path = os.path.join(OUTPUT_DIR, base[:-5])
        if not out_path or not os.path.exists(out_path):
            # Last resort: pick newest file from OUTPUT_DIR
            candidates = [os.path.join(OUTPUT_DIR, f) for f in os.listdir(OUTPUT_DIR)]
            candidates = [p for p in candidates if os.path.isfile(p)]
            if candidates:
                out_path = max(candidates, key=lambda p: os.path.getmtime(p))
        return out_path, None

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path not in ('/compress','/decompress'):
            return self._fail(404, 'Not found')
        ensure_dirs()
        in_path, err = self._handle_upload()
        if err:
            return self._fail(400, err)
        out_path, run_err = self._run_cli('compress' if parsed.path=='/compress' else 'decompress', in_path)
        if run_err:
            return self._fail(500, run_err)

        # Stream file back with a reasonable filename header
        filename = os.path.basename(out_path)
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Disposition', f'attachment; filename="{filename}"')
        self.send_header('x-output-filename', filename)
        self.end_headers()
        with open(out_path, 'rb') as f:
            shutil.copyfileobj(f, self.wfile)


def main():
    ensure_dirs()
    port = int(os.environ.get('PORT', '8000'))
    host = os.environ.get('HOST', '0.0.0.0')
    server = HTTPServer((host, port), Handler)
    print(f"Serving web UI at http://{host}:{port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == '__main__':
    main()
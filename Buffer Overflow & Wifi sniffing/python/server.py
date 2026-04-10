from http.server import BaseHTTPRequestHandler, HTTPServer

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers['Content-Length'])
        data = self.rfile.read(length)

        print("\n=== RECEIVED ===")
        print(data.decode())

        response = b"OK"

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()

        self.wfile.write(response)

    def log_message(self, format, *args):
        return  # silence default logging

HTTPServer(("0.0.0.0", 8000), Handler).serve_forever()
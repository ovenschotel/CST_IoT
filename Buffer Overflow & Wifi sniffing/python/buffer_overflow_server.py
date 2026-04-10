from http.server import BaseHTTPRequestHandler, HTTPServer

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        # prompt user in terminal
        user_input = input("\nEnter payload: ")

        payload = user_input.encode()

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()

        self.wfile.write(payload)

    def log_message(self, format, *args):
        return

HTTPServer(("0.0.0.0", 8000), Handler).serve_forever()
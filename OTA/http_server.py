import http.server
import socketserver

PORT = 8070

Handler = http.server.SimpleHTTPRequestHandler

with socketserver.TCPServer(("0.0.0.0", PORT), Handler) as httpd:
    print(f"Serving INSECURE HTTP on port {PORT}...")
    httpd.serve_forever()

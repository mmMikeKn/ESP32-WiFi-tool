#!/usr/bin/env python
# -*- coding: utf-8 -*-
from http.server import BaseHTTPRequestHandler, HTTPServer
import os
import shutil
from time import sleep

class TestHandler(BaseHTTPRequestHandler):
    def do_POST(s):
        print('POST request,\nPath: ' + s.path + '\nHeaders:\n' + str(s.headers) + '\n')
        length = int(s.headers['Content-Length'])
        try:
            rq = s.rfile.read(length)
            print('POST request :' + str(rq))
            s.send_response(200)
            s.end_headers()
            fname = s.path[1:]
            with open(fname, 'wb') as f:
                f.write(rq)

        except Exception as e:
            print("POST error:"+e)
        return

    def do_GET(s):
        print('GET request,\nPath: ' + s.path + '\nHeaders:\n' + str(s.headers) + '\n')
        if s.path == '/' or s.path == '/index.html':
            fname = 'index.html'
            with open(fname, 'rb') as f:
                s.send_response(200)
                s.send_header('Content-type', 'text/html;charset=utf-8')
                s.send_header('Content-Length', str(os.fstat(f.fileno()).st_size))
                s.end_headers()
                shutil.copyfileobj(f, s.wfile)
        elif s.path.endswith(".jsn"):
            fname = s.path[1:]
            with open(fname, 'rb') as f:
                s.send_response(200)
                s.send_header('Content-type', 'application/json;charset=utf-8')
                s.send_header('Content-Length', str(os.fstat(f.fileno()).st_size))
                s.end_headers()
                shutil.copyfileobj(f, s.wfile)
        else:
            fname = s.path[1:]
            with open(fname, 'rb') as f:
                s.send_response(200)
                s.send_header('Content-type', 'application/octet-stream')
                s.send_header('Content-Disposition', 'attachment; filename="{}"'.format(os.path.basename(fname)))
                s.send_header('Content-Length', str(os.fstat(f.fileno()).st_size))
                s.end_headers()
                while True:
                    chunk = f.read(2048)
                    if chunk:
                        s.wfile.write(chunk)
                        sleep(0.1)
                    else:
                        break
                # shutil.copyfileobj(f, s.wfile)


httpd = HTTPServer(('0.0.0.0', 8080), TestHandler)
try:
    print("------- server started ----------")
    httpd.serve_forever()
except KeyboardInterrupt:
    print("------- server stoped ----------")
    httpd.socket.close()

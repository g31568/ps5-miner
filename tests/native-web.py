#!/usr/bin/env python3
"""Exercise the production nonblocking server over real host sockets."""
from pathlib import Path
import json,socket,subprocess,tempfile,time,urllib.parse,shlex
root=Path(__file__).resolve().parents[1]
source=r'''
#include "native-web.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
// A harmless callback isolates HTTP framing, concurrency and origin enforcement.
native::WebReply handle(const std::string &,const std::string &,const std::string &body){return {200,body.empty()?"{}":body};}
int main(){native::WebServer web;if(!web.start(handle))return 1;puts(web.url().c_str());fflush(stdout);for(;;){auto now=std::chrono::steady_clock::now().time_since_epoch();web.poll(std::chrono::duration_cast<std::chrono::microseconds>(now).count());std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
'''
with tempfile.TemporaryDirectory() as work:
    work=Path(work);(work/'server.cpp').write_text(source)
    uv_flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','libuv'],text=True))
    subprocess.run(['clang++','-std=c++17','-I'+str(root/'homebrew'),str(work/'server.cpp'),str(root/'homebrew/native-web.cpp'),'-o',str(work/'server'),*uv_flags],check=True)
    process=subprocess.Popen([str(work/'server')],stdout=subprocess.PIPE,text=True)
    try:
        url=urllib.parse.urlsplit(process.stdout.readline().strip());endpoint=(url.hostname,url.port);host=url.netloc
        def exchange(data,split=None):
            with socket.create_connection(endpoint,3) as client:
                client.settimeout(3)
                if split:
                    client.sendall(data[:split]);time.sleep(.02);client.sendall(data[split:])
                else:client.sendall(data)
                result=b''
                while part:=client.recv(65536):result+=part
            header,body=result.split(b'\r\n\r\n',1)
            assert int(next(line.split(b':',1)[1] for line in header.split(b'\r\n') if line.lower().startswith(b'content-length:')))==len(body)
            return int(header.split()[1]),body
        def request(extra='',body=b'',method='GET'):
            return f'{method} /config HTTP/1.1\r\nHost: {host}\r\n{extra}Content-Length: {len(body)}\r\n\r\n'.encode()+body
        assert exchange(request())[0]==200
        body=json.dumps({'user':'test','padding':'a'*9000}).encode()
        raw=request('X-XMRig-Control: 1\r\nContent-Type: application/json\r\n',body,'POST')
        assert exchange(raw,split=5000)==(200,body)
        assert exchange(request(body=b'{}',method='POST'))[0]==403
        assert exchange(request('Origin: http://attacker.invalid\r\n'))[0]==403
        assert exchange(request().replace(host.encode(),b'attacker.invalid'))[0]==403
        assert exchange(request('Content-Length: 0\r\n'))[0]==400
        assert exchange(request('Transfer-Encoding: chunked\r\n'))[0]==400
        assert exchange(request().replace(b'Content-Length: 0',b'Content-Length: 16384'))[0]==413
        assert exchange(request('X-Long: '+'a'*4100+'\r\n'))[0]==413
        assert exchange(request()+b'extra')[0]==400
        # A stalled client must not block an independent status request.
        with socket.create_connection(endpoint,3) as stalled:
            stalled.sendall(b'GET / HTTP/1.1\r\n')
            assert exchange(request())[0]==200
        print('Native HTTP framing and origin tests PASS')
    finally:
        process.terminate();process.wait(timeout=5)

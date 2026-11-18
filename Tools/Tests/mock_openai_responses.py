"""One-request loopback contract test. Never forwards requests or logs credentials."""
import argparse
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--endpoint-file', required=True)
parser.add_argument('--report-file', required=True)
args = parser.parse_args()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *unused):
        pass

    def do_POST(self):
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length < 65536:
                raise ValueError('invalid size')
            body = json.loads(self.rfile.read(length).decode('utf-8'))
            context = json.loads(body['input'])
            valid = (self.path == '/v1/responses' and self.headers.get('Authorization') == 'Bearer unit-test-key'
                     and self.headers.get('Content-Type', '').startswith('application/json')
                     and body['model'] == 'MiniMax-M3.1-Flash' and body['store'] is False
                     and body['reasoning']['effort'] == 'low' and 'text' not in body
                     and '冰拿铁' in context['authored_line'])
            if not valid:
                raise ValueError('contract mismatch')
            payload = {'status': 'completed', 'output': [
                {'type': 'reasoning', 'content': [{'type': 'reasoning_text', 'text': 'ignored by game'}]},
                {'type': 'message', 'role': 'assistant', 'content': [
                {'type': 'output_text', 'text': json.dumps({'line': context['authored_line']}, ensure_ascii=False)}]}]}
            data = json.dumps(payload, ensure_ascii=False).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            Path(args.report_file).write_text(json.dumps({'valid_contract': True, 'utf8_round_trip': True,
                                                        'real_api_called': False}), encoding='utf-8')
        except (KeyError, ValueError, TypeError):
            self.send_error(422, 'invalid test request')


server = HTTPServer(('127.0.0.1', 0), Handler)
server.timeout = 75
Path(args.endpoint_file).write_text(f'http://127.0.0.1:{server.server_port}/v1/responses', encoding='utf-8')
server.handle_request()
server.server_close()

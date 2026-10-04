"""Runs real CRUD against localhost; use a development Neon branch."""
import json
import uuid
import urllib.request
import urllib.error

BASE = 'http://localhost:8080'

def request(method, route, body=None, expected=200):
    payload = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(BASE + route, data=payload, method=method,
                                 headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req, timeout=30) as response:
            status, data = response.status, response.read()
    except urllib.error.HTTPError as response:
        status, data = response.code, response.read()
    assert status == expected, (method, route, status, data)
    return json.loads(data) if data else None

request('GET', '/ready')
spec = request('GET', '/openapi.json')
assert spec['openapi'] == '3.0.3'
token = uuid.uuid4().hex
for table, body in [('users', {'name': 'Smoke test user', 'email': token + '@example.com', 'phone': None}),
                    ('products', {'name': 'Smoke test product', 'description': None, 'price': '12.34', 'stock': 2})]:
    base = '/api/v1/' + table
    record = request('POST', base, body, 201)
    route = base + '/' + record['id']
    try:
        assert request('GET', route)['name'] == body['name']
        assert isinstance(request('GET', base + '?limit=2&offset=0'), list)
        if table == 'users': request('POST', base, body, 409)
        else: request('POST', base, {**body, 'price': '-1.00'}, 400)
        body['name'] = 'Updated smoke test'
        assert request('PUT', route, body)['name'] == body['name']
        assert request('GET', route)['name'] == body['name']
    finally:
        request('DELETE', route, expected=204)
    request('GET', route, expected=404)
request('POST', '/api/v1/users', {}, 400)
request('GET', '/api/v1/products?limit=101', expected=400)
request('GET', '/api/v1/users/abc', expected=400)
print('PASS: database readiness, OpenAPI, users/products CRUD, persistence, and error responses')

const fs = require('fs');
const http = require('http');
const path = require('path');
const { execFile, spawn } = require('child_process');

const HOST = '127.0.0.1';
const PORT = 8042;
const WEB_DIRECTORY = __dirname;
const PROJECT_DIRECTORY = path.resolve(WEB_DIRECTORY, '..');
const ENGINE_PATH = path.join(PROJECT_DIRECTORY, 'build', 'theta.exe');
const CONFIG_PATH = path.join(PROJECT_DIRECTORY, 'config', 'config.conf');
const MAX_REQUEST_SIZE = 32_768;

const CONTENT_TYPES = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.md': 'text/plain; charset=utf-8',
  '.svg': 'image/svg+xml'
};

function sendJson(response, status, value) {
  const body = JSON.stringify(value);

  response.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body)
  });
  response.end(body);
}

function configuredMaxDepth() {
  let content;
  let match;
  let depth;

  try {
    content = fs.readFileSync(CONFIG_PATH, 'utf8');
  } catch (error) {
    return 6;
  }

  match = content.match(/^\s*max_depth\s*=\s*(\d+)\s*$/m);
  depth = match ? Number(match[1]) : 6;

  return Number.isInteger(depth) && depth >= 1 && depth <= 128
    ? depth
    : 6;
}

function readRequestBody(request, callback) {
  let body = '';

  request.setEncoding('utf8');
  request.on('data', chunk => {
    body += chunk;

    if (Buffer.byteLength(body) > MAX_REQUEST_SIZE) {
      request.destroy();
    }
  });
  request.on('end', () => callback(body));
}

function handleEngineRequest(request, response, command) {
  readRequestBody(request, body => {
    let fen;
    let arguments;
    let timeout = 5000;

    try {
      const value = JSON.parse(body);
      fen = value.fen;

      if (command === 'search') {
        if (!Number.isInteger(value.depth) || value.depth < 1 ||
            value.depth > configuredMaxDepth()) {
          sendJson(response, 400, { error: 'Invalid depth' });
          return;
        }

        if (!Number.isInteger(value.timeMs) ||
            value.timeMs < 0 || value.timeMs > 60000) {
          sendJson(response, 400, { error: 'Invalid time limit' });
          return;
        }

        arguments = [command, String(value.depth), String(value.timeMs), fen];
        timeout = value.timeMs + 1000;
      } else {
        arguments = [command, fen];
      }
    } catch (error) {
      sendJson(response, 400, { error: 'Invalid request' });
      return;
    }

    if (typeof fen !== 'string' || fen.length > 256) {
      sendJson(response, 400, { error: 'Invalid FEN' });
      return;
    }

    if (!fs.existsSync(ENGINE_PATH)) {
      sendJson(response, 503, { error: 'Engine is not built' });
      return;
    }

    execFile(
      ENGINE_PATH,
      arguments,
      {
        cwd: PROJECT_DIRECTORY,
        timeout,
        maxBuffer: 1024 * 1024,
        windowsHide: true
      },
      (error, stdout, stderr) => {
        if (error) {
          sendJson(response, 400, {
            error: stderr.trim() || 'Engine failed'
          });
          return;
        }

        try {
          sendJson(response, 200, JSON.parse(stdout));
        } catch (parseError) {
          sendJson(response, 500, { error: 'Invalid engine response' });
        }
      }
    );
  });
}

function parseUciInfo(line) {
  const match = line.match(/^info depth (\d+) score cp (-?\d+) pv(?: (.*))?$/);

  if (!match) {
    return null;
  }

  return {
    depth: Number(match[1]),
    evaluation: Number(match[2]),
    pv: match[3] ? match[3].trim().split(/\s+/) : []
  };
}

function handleSearchStream(request, response) {
  readRequestBody(request, body => {
    let value;
    let engine;
    let output = '';
    let errorOutput = '';
    let timeout;
    let closed = false;

    try {
      value = JSON.parse(body);
    } catch (error) {
      sendJson(response, 400, { error: 'Invalid request' });
      return;
    }

    if (typeof value.fen !== 'string' ||
        value.fen.length > 256 ||
        /[\r\n]/.test(value.fen) ||
        !Number.isInteger(value.depth) ||
        value.depth < 1 || value.depth > configuredMaxDepth() ||
        !Number.isInteger(value.timeMs) ||
        value.timeMs < 0 || value.timeMs > 60000) {
      sendJson(response, 400, { error: 'Invalid search request' });
      return;
    }

    if (!fs.existsSync(ENGINE_PATH)) {
      sendJson(response, 503, { error: 'Engine is not built' });
      return;
    }

    response.writeHead(200, {
      'Content-Type': 'application/x-ndjson; charset=utf-8',
      'Cache-Control': 'no-cache',
      'Connection': 'keep-alive'
    });

    engine = spawn(ENGINE_PATH, [], {
      cwd: PROJECT_DIRECTORY,
      windowsHide: true
    });

    engine.stdout.setEncoding('utf8');
    engine.stderr.setEncoding('utf8');

    engine.stdout.on('data', chunk => {
      output += chunk;

      for (;;) {
        const lineEnd = output.indexOf('\n');
        let line;
        let info;

        if (lineEnd === -1) {
          break;
        }

        line = output.slice(0, lineEnd).trim();
        output = output.slice(lineEnd + 1);
        info = parseUciInfo(line);

        if (info && !closed) {
          response.write(`${JSON.stringify(info)}\n`);
        }
      }
    });

    engine.stderr.on('data', chunk => {
      errorOutput += chunk;
    });

    engine.on('error', error => {
      if (!closed) {
        response.write(`${JSON.stringify({ error: error.message })}\n`);
      }
    });

    engine.on('close', code => {
      clearTimeout(timeout);

      if (!closed && code !== 0 && errorOutput.trim()) {
        response.write(`${JSON.stringify({ error: errorOutput.trim() })}\n`);
      }

      if (!closed) {
        response.end();
      }
    });

    response.on('close', () => {
      closed = true;
      engine.kill();
    });

    timeout = setTimeout(() => engine.kill(), value.timeMs + 1000);
    engine.stdin.end([
      'uci',
      'isready',
      `position fen ${value.fen}`,
      `go depth ${value.depth} movetime ${value.timeMs}`,
      'quit',
      ''
    ].join('\n'));
  });
}

function handleStaticRequest(request, response) {
  const requestPath = request.url === '/'
    ? '/index.html'
    : decodeURIComponent(request.url.split('?')[0]);
  const filePath = path.resolve(WEB_DIRECTORY, `.${requestPath}`);

  if (!filePath.startsWith(`${WEB_DIRECTORY}${path.sep}`)) {
    response.writeHead(403);
    response.end();
    return;
  }

  fs.readFile(filePath, (error, content) => {
    if (error) {
      response.writeHead(error.code === 'ENOENT' ? 404 : 500);
      response.end();
      return;
    }

    const contentType = CONTENT_TYPES[path.extname(filePath)] ||
      'application/octet-stream';

    response.writeHead(200, { 'Content-Type': contentType });
    response.end(content);
  });
}

const server = http.createServer((request, response) => {
  if (request.method === 'GET' && request.url === '/api/config') {
    sendJson(response, 200, { maxDepth: configuredMaxDepth() });
    return;
  }

  if (request.method === 'POST' && request.url === '/api/moves') {
    handleEngineRequest(request, response, 'moves');
    return;
  }

  if (request.method === 'POST' && request.url === '/api/search') {
    handleEngineRequest(request, response, 'search');
    return;
  }

  if (request.method === 'POST' && request.url === '/api/search-stream') {
    handleSearchStream(request, response);
    return;
  }

  if (request.method === 'GET') {
    handleStaticRequest(request, response);
    return;
  }

  response.writeHead(405);
  response.end();
});

server.listen(PORT, HOST, () => {
  console.log(`Theta: http://${HOST}:${PORT}`);
});

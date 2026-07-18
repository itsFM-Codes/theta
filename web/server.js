const fs = require('fs');
const http = require('http');
const path = require('path');
const { execFile, spawn } = require('child_process');

const HOST = '127.0.0.1';
const DEFAULT_PORT = 8042;
const requestedPort = Number(process.env.THETA_WEB_PORT);
const PORT = Number.isInteger(requestedPort) &&
  requestedPort > 0 && requestedPort <= 65535
  ? requestedPort
  : DEFAULT_PORT;
const WEB_DIRECTORY = __dirname;
const PROJECT_DIRECTORY = path.resolve(WEB_DIRECTORY, '..');
const ENGINE_PATH = path.join(PROJECT_DIRECTORY, 'build', 'theta.exe');
const CONFIG_PATH = path.join(PROJECT_DIRECTORY, 'config', 'config.conf');
const MAX_REQUEST_SIZE = 32_768;
const MATE_DISPLAY_SCORE = 100000;

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
  const tokens = line.trim().split(/\s+/);
  const info = {
    depth: null,
    selectiveDepth: null,
    evaluation: null,
    scoreType: null,
    mate: null,
    nodes: null,
    nps: null,
    timeMs: null,
    hashfull: null,
    pv: []
  };
  let index;

  if (tokens[0] !== 'info') {
    return null;
  }

  for (index = 1; index < tokens.length; ++index) {
    const token = tokens[index];

    if (token === 'depth' && index + 1 < tokens.length) {
      info.depth = Number(tokens[++index]);
    } else if (token === 'seldepth' && index + 1 < tokens.length) {
      info.selectiveDepth = Number(tokens[++index]);
    } else if (token === 'score' && index + 2 < tokens.length) {
      const scoreType = tokens[++index];
      const scoreValue = Number(tokens[++index]);

      if (scoreType === 'cp') {
        info.scoreType = 'cp';
        info.evaluation = scoreValue;
      } else if (scoreType === 'mate') {
        info.scoreType = 'mate';
        info.mate = scoreValue;
        info.evaluation = scoreValue >= 0
          ? MATE_DISPLAY_SCORE
          : -MATE_DISPLAY_SCORE;
      }
    } else if (token === 'nodes' && index + 1 < tokens.length) {
      info.nodes = Number(tokens[++index]);
    } else if (token === 'nps' && index + 1 < tokens.length) {
      info.nps = Number(tokens[++index]);
    } else if (token === 'time' && index + 1 < tokens.length) {
      info.timeMs = Number(tokens[++index]);
    } else if (token === 'hashfull' && index + 1 < tokens.length) {
      info.hashfull = Number(tokens[++index]);
    } else if (token === 'pv') {
      info.pv = tokens.slice(index + 1);
      break;
    }
  }

  if (!Number.isFinite(info.depth) ||
      !Number.isFinite(info.evaluation) ||
      info.scoreType === null) {
    return null;
  }

  return info;
}

function parseUciBestMove(line) {
  const tokens = line.trim().split(/\s+/);

  if (tokens[0] === 'bestmove' &&
      tokens[1] &&
      /^[a-h][1-8][a-h][1-8][qrbn]?$/.test(tokens[1])) {
    return tokens[1];
  }

  return null;
}

function handleSearchStream(request, response) {
  readRequestBody(request, body => {
    let value;
    let engine;
    let output = '';
    let errorOutput = '';
    let timeout;
    let closed = false;
    let goCommand;

    try {
      value = JSON.parse(body);
    } catch (error) {
      sendJson(response, 400, { error: 'Invalid request' });
      return;
    }

    if (typeof value.fen !== 'string' ||
        value.fen.length > 256 ||
        /[\r\n]/.test(value.fen) ||
        (value.infinite !== true &&
          (!Number.isInteger(value.depth) ||
          value.depth < 1 || value.depth > configuredMaxDepth())) ||
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

    function writeEngineLine(line) {
      const info = parseUciInfo(line);
      const bestMove = parseUciBestMove(line);

      if (info && !closed) {
        response.write(`${JSON.stringify(info)}\n`);
      } else if (bestMove && !closed) {
        response.write(`${JSON.stringify({ bestMove })}\n`);
      }
    }

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

        if (lineEnd === -1) {
          break;
        }

        line = output.slice(0, lineEnd).trim();
        output = output.slice(lineEnd + 1);
        writeEngineLine(line);
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
      if (timeout) {
        clearTimeout(timeout);
      }

      if (output.trim()) {
        writeEngineLine(output.trim());
      }

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

    if (value.infinite === true) {
      goCommand = 'go infinite';
    } else {
      goCommand = `go depth ${value.depth} movetime ${value.timeMs}`;
      timeout = setTimeout(() => engine.kill(), value.timeMs + 1000);
    }

    engine.stdin.end([
      'uci',
      'isready',
      `position fen ${value.fen}`,
      goCommand,
      value.infinite === true ? '' : 'quit',
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

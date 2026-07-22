const FILES = 'abcdefgh';
const STARTING_POSITION = [
  'rnbqkbnr',
  'pppppppp',
  '........',
  '........',
  '........',
  '........',
  'PPPPPPPP',
  'RNBQKBNR'
];

const MOVE_FLAG_CAPTURE = 1 << 0;
const MOVE_FLAG_DOUBLE_PAWN = 1 << 1;
const MOVE_FLAG_EN_PASSANT = 1 << 2;
const MOVE_FLAG_CASTLE_KINGSIDE = 1 << 3;
const MOVE_FLAG_CASTLE_QUEENSIDE = 1 << 4;
const MOVE_FLAG_PROMOTION = 1 << 5;
let searchDepth = 8;
let searchTimeMs = 1000;
let searchInfinite = true;

const boardElement = document.querySelector('#board');
const linesElement = document.querySelector('#lines');
const modeButtons = document.querySelectorAll('.mode');
const evaluationElement = document.querySelector('.evaluation');
const sidePanelElement = document.querySelector('#side-panel');
const settingsButton = document.querySelector('#engine-settings');
const settingsElement = document.querySelector('#search-settings');
const searchTimeInput = document.querySelector('#search-time');
const searchDepthInput = document.querySelector('#search-depth');
const searchInfiniteInput = document.querySelector('#search-infinite');
const searchStatusElement = document.querySelector('#search-status');
const depthElement = document.querySelector('#depth');
const searchStatsElement = document.querySelector('#search-stats');
const playControlsElement = document.querySelector('#play-controls');
const playColorButtons = document.querySelectorAll('.play-color');
const editorColorButtons = document.querySelectorAll('.editor-color');
const castlingInputs = document.querySelectorAll('[data-castling]');
const analysisToggleButton = document.querySelector('#analysis-toggle');

let board = createStartingPosition();
let currentMode = 'play';
let playerColor = 'white';
let selectedSquare = null;
let selectedEditorPiece = 'P';
let legalMoves = [];
let positionLegalMoves = [];
let lastMove = null;
let currentPositionInCheck = false;
let gameResult = null;
let isFlipped = false;
let sideToMove = 'white';
let castlingRights = 'KQkq';
let enPassantSquare = '-';
let halfmoveClock = 0;
let fullmoveNumber = 1;
let moveRequestNumber = 0;
let moveHistory = [];
let positionHistory = [];
let historyIndex = 0;
let dragMoveRequest = null;
let dragMoveApplied = false;
let dragDropPending = false;
let materialEvaluation = 0;
let evaluationText = '+0.00';
let evaluationRequestNumber = 0;
let gameStateRequestNumber = 0;
let searchAbortController = null;
let analysisDepth = searchDepth;
let principalVariation = [];
let searchStatus = 'Idle';
let searchError = '';
let engineMovePending = false;
let analysisRunning = false;
let searchStats = {
  selectiveDepth: 0,
  nodes: 0,
  nps: 0,
  timeMs: 0,
  hashfull: 0
};

function createStartingPosition() {
  return STARTING_POSITION.map(row => [...row]);
}

function syncWorkspaceHeight() {
  const boardSize = boardElement.getBoundingClientRect().width;
  const useStackedLayout = window.innerWidth <= 900 || boardSize === 0;

  if (useStackedLayout) {
    evaluationElement.style.height = '';
    sidePanelElement.style.height = '';
    return;
  }

  evaluationElement.style.height = `${boardSize}px`;
  sidePanelElement.style.height = `${boardSize}px`;
}

function renderPiece(piece) {
  const color = piece === piece.toUpperCase() ? 'w' : 'b';
  const type = piece.toUpperCase();
  const source = `assets/cburnett/${color}${type}.svg`;

  return `<img class="piece" src="${source}" alt="" draggable="true">`;
}

function squareName(row, column) {
  return `${FILES[column]}${8 - row}`;
}

function squareCoordinates(name) {
  return {
    row: 8 - Number(name[1]),
    column: FILES.indexOf(name[0])
  };
}

function isSelected(row, column) {
  return selectedSquare?.row === row && selectedSquare?.column === column;
}

function moveToSquare(row, column) {
  const name = squareName(row, column);
  return legalMoves.find(move => move.to === name);
}

function castlingMoveForRook(row, column) {
  if (!selectedSquare || (column !== 0 && column !== 7)) {
    return null;
  }

  const king = board[selectedSquare.row][selectedSquare.column];
  const rook = board[row][column];
  const piecesHaveSameColor =
    (king === king.toUpperCase()) === (rook === rook.toUpperCase());

  if (king.toLowerCase() !== 'k' ||
      rook.toLowerCase() !== 'r' ||
      row !== selectedSquare.row ||
      !piecesHaveSameColor) {
    return null;
  }

  const castleFlag = column === 7
    ? MOVE_FLAG_CASTLE_KINGSIDE
    : MOVE_FLAG_CASTLE_QUEENSIDE;

  return legalMoves.find(move => move.flags & castleFlag);
}

function clearSelection() {
  selectedSquare = null;
  legalMoves = [];
  moveRequestNumber++;
  renderBoard();
}

function refreshSquareClasses() {
  for (const square of boardElement.querySelectorAll('.square')) {
    const row = Number(square.dataset.row);
    const column = Number(square.dataset.column);

    square.className = getSquareClass(row, column);
  }
}

function getSquareClass(row, column) {
  const color = (row + column) % 2 === 0 ? 'light' : 'dark';
  const classes = ['square', color];
  const move = moveToSquare(row, column);
  const square = squareName(row, column);
  const piece = board[row][column];

  if (lastMove && (lastMove.from === square || lastMove.to === square)) {
    classes.push('last-move');
  }

  if (currentPositionInCheck &&
      piece.toLowerCase() === 'k' &&
      (sideToMove === 'white' ? piece === 'K' : piece === 'k')) {
    classes.push('in-check');
  }

  if (isSelected(row, column)) {
    classes.push('selected');
  }

  if (move) {
    if (move.flags & MOVE_FLAG_CAPTURE) {
      classes.push('capture-target');
    } else {
      classes.push('move-target');
    }
  }

  return classes.join(' ');
}

function addCoordinates(square, row, column) {
  const showRank = (!isFlipped && column === 7) || (isFlipped && column === 0);
  const showFile = (!isFlipped && row === 7) || (isFlipped && row === 0);

  if (showRank) {
    square.insertAdjacentHTML(
      'beforeend',
      `<span class="coord rank">${8 - row}</span>`
    );
  }

  if (showFile) {
    square.insertAdjacentHTML(
      'beforeend',
      `<span class="coord file">${FILES[column]}</span>`
    );
  }
}

function renderBoard() {
  boardElement.innerHTML = '';

  const squareOrder = [...Array(64).keys()];
  if (isFlipped) {
    squareOrder.reverse();
  }

  for (const index of squareOrder) {
    const row = Math.floor(index / 8);
    const column = index % 8;
    const piece = board[row][column];
    const square = document.createElement('button');

    square.className = getSquareClass(row, column);
    square.dataset.row = row;
    square.dataset.column = column;
    square.setAttribute('aria-label', squareName(row, column));

    if (piece !== '.') {
      square.innerHTML = renderPiece(piece);
    }

    addCoordinates(square, row, column);
    square.addEventListener('click', handleSquareClick);
    square.addEventListener('dragover', handleSquareDragOver);
    square.addEventListener('drop', handleSquareDrop);

    const pieceElement = square.querySelector('.piece');
    if (pieceElement) {
      pieceElement.addEventListener('dragstart', handlePieceDragStart);
      pieceElement.addEventListener('dragend', handlePieceDragEnd);
    }

    boardElement.append(square);
  }
}

function boardPlacementFen() {
  return board.map(row => {
    let result = '';
    let emptyCount = 0;

    for (const piece of row) {
      if (piece === '.') {
        emptyCount++;
        continue;
      }

      if (emptyCount > 0) {
        result += emptyCount;
        emptyCount = 0;
      }

      result += piece;
    }

    if (emptyCount > 0) {
      result += emptyCount;
    }

    return result;
  }).join('/');
}

function currentFen() {
  const side = sideToMove === 'white' ? 'w' : 'b';
  const rights = castlingRights || '-';

  return [
    boardPlacementFen(),
    side,
    rights,
    enPassantSquare,
    halfmoveClock,
    fullmoveNumber
  ].join(' ');
}

function capturePosition() {
  return {
    board: board.map(row => [...row]),
    sideToMove,
    castlingRights,
    enPassantSquare,
    halfmoveClock,
    fullmoveNumber
  };
}

function restorePosition(position) {
  board = position.board.map(row => [...row]);
  sideToMove = position.sideToMove;
  castlingRights = position.castlingRights;
  enPassantSquare = position.enPassantSquare;
  halfmoveClock = position.halfmoveClock;
  fullmoveNumber = position.fullmoveNumber;
}

function moveNotation(move, piece, moves = []) {
  if (move.flags & MOVE_FLAG_CASTLE_KINGSIDE) {
    return 'O-O';
  }

  if (move.flags & MOVE_FLAG_CASTLE_QUEENSIDE) {
    return 'O-O-O';
  }

  const isPawn = piece.toLowerCase() === 'p';
  const isCapture = Boolean(move.flags & MOVE_FLAG_CAPTURE);
  let notation = '';

  if (isPawn) {
    if (isCapture) {
      notation += `${move.from[0]}x`;
    }
  } else {
    notation += piece.toUpperCase();

    const alternatives = moves.filter(candidate => {
      if (candidate.from === move.from || candidate.to !== move.to) {
        return false;
      }

      const coordinates = squareCoordinates(candidate.from);
      return board[coordinates.row][coordinates.column] === piece;
    });

    if (alternatives.length > 0) {
      const hasSameFile = alternatives.some(candidate =>
        candidate.from[0] === move.from[0]
      );
      const hasSameRank = alternatives.some(candidate =>
        candidate.from[1] === move.from[1]
      );

      notation += hasSameFile
        ? (hasSameRank ? move.from : move.from[1])
        : move.from[0];
    }

    if (isCapture) {
      notation += 'x';
    }
  }

  notation += move.to;

  if (move.flags & MOVE_FLAG_PROMOTION) {
    notation += `=${move.promotion.toUpperCase()}`;
  }

  return notation;
}

function renderMoveHistory() {
  const historyElement = document.querySelector('#move-history');
  const rows = [];

  for (let index = 0; index < moveHistory.length; index += 2) {
    const moveNumber = Math.floor(index / 2) + 1;
    const whiteMove = moveHistory[index];
    const blackMove = moveHistory[index + 1];

    rows.push([
      '<div class="move-row">',
      `<span class="move-number">${moveNumber}.</span>`,
      `<button class="history-move${historyIndex === index + 1 ? ' active' : ''}" data-history-index="${index + 1}">${whiteMove.notation}</button>`,
      blackMove
        ? `<button class="history-move${historyIndex === index + 2 ? ' active' : ''}" data-history-index="${index + 2}">${blackMove.notation}</button>`
        : '<span></span>',
      '</div>'
    ].join(''));
  }

  historyElement.innerHTML = rows.join('');

  for (const button of historyElement.querySelectorAll('.history-move')) {
    button.addEventListener('click', () => {
      goToHistory(Number(button.dataset.historyIndex));
    });
  }

  document.querySelector('#first-move').disabled = historyIndex === 0;
  document.querySelector('#previous-move').disabled = historyIndex === 0;
  document.querySelector('#next-move').disabled = historyIndex === moveHistory.length;
  document.querySelector('#last-move').disabled = historyIndex === moveHistory.length;

  const activeMove = historyElement.querySelector('.history-move.active');
  if (activeMove) {
    activeMove.scrollIntoView({ block: 'nearest' });
  }
}

function resetHistory() {
  moveHistory = [];
  positionHistory = [capturePosition()];
  historyIndex = 0;
  lastMove = null;
  currentPositionInCheck = false;
  gameResult = null;
  renderMoveHistory();
}

function goToHistory(index) {
  if (index < 0 || index >= positionHistory.length) {
    return;
  }

  evaluationRequestNumber++;
  if (searchAbortController) {
    searchAbortController.abort();
  }

  historyIndex = index;
  restorePosition(positionHistory[index]);
  lastMove = index === 0 ? null : moveHistory[index - 1];
  selectedSquare = null;
  legalMoves = [];
  moveRequestNumber++;
  renderBoard();
  renderMoveHistory();
  refreshAnalysis();

  refreshGameState();

  if (currentMode === 'analysis') {
    requestSearch();
  }
}

function canSelectPiece(piece) {
  if (piece === '.') {
    return false;
  }

  const isWhitePiece = piece === piece.toUpperCase();
  const matchesTurn = sideToMove === 'white' ? isWhitePiece : !isWhitePiece;

  if (currentMode === 'play' && sideToMove !== playerColor) {
    return false;
  }

  return matchesTurn;
}

async function fetchPositionMoves(fen = currentFen()) {
  const response = await fetch('/api/moves', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({ fen })
  });

  if (!response.ok) {
    throw new Error(`Move request failed with ${response.status}`);
  }

  return response.json();
}

async function requestLegalMoves(row, column, preserveBoard = false) {
  const requestNumber = ++moveRequestNumber;
  const from = squareName(row, column);

  selectedSquare = { row, column };
  legalMoves = [];
  if (preserveBoard) {
    refreshSquareClasses();
  } else {
    renderBoard();
  }

  try {
    const result = await fetchPositionMoves();
    if (requestNumber !== moveRequestNumber) {
      return;
    }

    positionLegalMoves = result.moves;
    currentPositionInCheck = Boolean(result.inCheck);
    legalMoves = result.moves.filter(move => move.from === from);
    if (preserveBoard) {
      refreshSquareClasses();
    } else {
      renderBoard();
    }
  } catch (error) {
    if (requestNumber === moveRequestNumber) {
      selectedSquare = null;
      legalMoves = [];

      if (preserveBoard) {
        refreshSquareClasses();
      } else {
        renderBoard();
      }
    }

    console.error(error);
  }
}

function legalMoveFromUci(moves, text) {
  return moves.find(move => {
    const promotion = move.promotion || '';

    return `${move.from}${move.to}${promotion}` === text;
  });
}

async function requestSearch({ infinite = searchInfinite } = {}) {
  const requestNumber = ++evaluationRequestNumber;
  let buffer = '';

  if (searchAbortController) {
    searchAbortController.abort();
  }

  searchAbortController = new AbortController();
  analysisRunning = true;
  searchStatus = 'Analyzing';
  searchError = '';
  refreshAnalysis();

  try {
    const response = await fetch('/api/search-stream', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      signal: searchAbortController.signal,
      body: JSON.stringify({
        fen: currentFen(),
        depth: searchDepth,
        timeMs: searchTimeMs,
        infinite
      })
    });

    if (!response.ok) {
      throw new Error(`Search request failed with ${response.status}`);
    }

    const reader = response.body.getReader();
    const decoder = new TextDecoder();

    for (;;) {
      const result = await reader.read();

      if (result.done) {
        break;
      }

      buffer += decoder.decode(result.value, { stream: true });

      for (;;) {
        const lineEnd = buffer.indexOf('\n');
        let line;
        let update;

        if (lineEnd === -1) {
          break;
        }

        line = buffer.slice(0, lineEnd);
        buffer = buffer.slice(lineEnd + 1);

        if (!line) {
          continue;
        }

        update = JSON.parse(line);
        if (update.error) {
          throw new Error(update.error);
        }

        if (update.bestMove) {
          continue;
        }

        if (requestNumber === evaluationRequestNumber) {
          searchStatus = 'Analyzing';
          searchStats = {
            selectiveDepth: update.selectiveDepth || update.depth,
            nodes: update.nodes || 0,
            nps: update.nps || 0,
            timeMs: update.timeMs || 0,
            hashfull: update.hashfull || 0
          };
          updateEvaluation(
            scoreFromWhitePerspective(update),
            formatSearchScore(update)
          );
          analysisDepth = update.depth;
          principalVariation = update.pv;
          refreshAnalysis();
        }
      }
    }

    if (requestNumber === evaluationRequestNumber) {
      searchStatus = 'Analysis complete';
      refreshAnalysis();
    }
  } catch (error) {
    if (error.name === 'AbortError') {
      if (requestNumber === evaluationRequestNumber) {
        searchStatus = 'Analysis stopped';
        refreshAnalysis();
      }
    } else {
      if (requestNumber === evaluationRequestNumber) {
        searchStatus = 'Error';
        searchError = error.message;
        refreshAnalysis();
      }
      console.error(error);
    }
  } finally {
    if (requestNumber === evaluationRequestNumber) {
      analysisRunning = false;
      searchAbortController = null;
      refreshAnalysis();
    }
  }
}

async function requestEngineMove() {
  if (currentMode !== 'play' ||
      sideToMove === playerColor ||
      gameResult ||
      engineMovePending) {
    return;
  }

  const requestNumber = ++evaluationRequestNumber;
  let buffer = '';
  let bestMoveText = null;

  if (searchAbortController) {
    searchAbortController.abort();
  }

  searchAbortController = new AbortController();
  engineMovePending = true;
  searchStatus = 'Thinking';
  searchError = '';
  refreshAnalysis();
  renderBoard();

  try {
    const response = await fetch('/api/search-stream', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      signal: searchAbortController.signal,
      body: JSON.stringify({
        fen: currentFen(),
        depth: searchDepth,
        timeMs: searchTimeMs,
        infinite: false
      })
    });

    if (!response.ok) {
      throw new Error(`Search request failed with ${response.status}`);
    }

    const reader = response.body.getReader();
    const decoder = new TextDecoder();

    for (;;) {
      const result = await reader.read();

      if (result.done) {
        break;
      }

      buffer += decoder.decode(result.value, { stream: true });

      for (;;) {
        const lineEnd = buffer.indexOf('\n');
        let line;
        let update;

        if (lineEnd === -1) {
          break;
        }

        line = buffer.slice(0, lineEnd);
        buffer = buffer.slice(lineEnd + 1);

        if (!line) {
          continue;
        }

        update = JSON.parse(line);
        if (update.error) {
          throw new Error(update.error);
        }

        if (requestNumber !== evaluationRequestNumber) {
          continue;
        }

        if (update.bestMove) {
          bestMoveText = update.bestMove;
          continue;
        }

        if (Array.isArray(update.pv) && update.pv.length > 0) {
          bestMoveText = update.pv[0];
        }

        searchStatus = 'Thinking';
        searchStats = {
          selectiveDepth: update.selectiveDepth || update.depth,
          nodes: update.nodes || 0,
          nps: update.nps || 0,
          timeMs: update.timeMs || 0,
          hashfull: update.hashfull || 0
        };
        updateEvaluation(
          scoreFromWhitePerspective(update),
          formatSearchScore(update)
        );
        analysisDepth = update.depth;
        principalVariation = update.pv;
        refreshAnalysis();
      }
    }

    if (requestNumber !== evaluationRequestNumber ||
        currentMode !== 'play' ||
        !bestMoveText ||
        bestMoveText === '0000') {
      if (requestNumber === evaluationRequestNumber) {
        searchStatus = 'Ready';
        refreshAnalysis();
      }
      return;
    }

    const position = await fetchPositionMoves();
    const move = legalMoveFromUci(position.moves, bestMoveText);

    if (!move) {
      throw new Error(`Engine returned illegal move ${bestMoveText}`);
    }

    applyLegalMove(move, {
      availableMoves: position.moves
    });
  } catch (error) {
    if (error.name === 'AbortError') {
      if (requestNumber === evaluationRequestNumber) {
        searchStatus = 'Cancelled';
        refreshAnalysis();
      }
    } else {
      if (requestNumber === evaluationRequestNumber) {
        searchStatus = 'Error';
        searchError = error.message;
        refreshAnalysis();
      }
      console.error(error);
    }
  } finally {
    if (requestNumber === evaluationRequestNumber) {
      engineMovePending = false;
      renderBoard();
    }
  }
}

function uciMoveToMove(
  text,
  positionBoard = board,
  positionEnPassant = enPassantSquare
) {
  const from = text.slice(0, 2);
  const to = text.slice(2, 4);
  const source = squareCoordinates(from);
  const target = squareCoordinates(to);
  const piece = positionBoard[source.row]?.[source.column] || '.';
  const targetPiece = positionBoard[target.row]?.[target.column] || '.';
  let flags = 0;

  if (piece.toLowerCase() === 'k' &&
      Math.abs(source.column - target.column) === 2) {
    flags = target.column > source.column
      ? MOVE_FLAG_CASTLE_KINGSIDE
      : MOVE_FLAG_CASTLE_QUEENSIDE;
  }

  if (text.length === 5) {
    flags |= MOVE_FLAG_PROMOTION;
  }

  if (targetPiece !== '.') {
    flags |= MOVE_FLAG_CAPTURE;
  }

  if (piece.toLowerCase() === 'p') {
    if (Math.abs(source.row - target.row) === 2) {
      flags |= MOVE_FLAG_DOUBLE_PAWN;
    }

    if (source.column !== target.column &&
        targetPiece === '.' &&
        to === positionEnPassant) {
      flags |= MOVE_FLAG_CAPTURE | MOVE_FLAG_EN_PASSANT;
    }
  }

  return {
    from,
    to,
    flags,
    promotion: text.length === 5 ? text[4] : null
  };
}

function handlePieceDragStart(event) {
  const square = event.currentTarget.closest('.square');
  const row = Number(square.dataset.row);
  const column = Number(square.dataset.column);
  const piece = board[row][column];

  if (currentMode === 'editor' || engineMovePending || !canSelectPiece(piece)) {
    event.preventDefault();
    return;
  }

  dragMoveApplied = false;
  dragDropPending = false;
  event.currentTarget.classList.add('dragging');
  event.dataTransfer.effectAllowed = 'move';
  event.dataTransfer.setData('text/plain', squareName(row, column));
  dragMoveRequest = requestLegalMoves(row, column, true);
}

function handlePieceDragEnd(event) {
  event.currentTarget.classList.remove('dragging');

  if (dragDropPending) {
    return;
  }

  if (!dragMoveApplied) {
    clearSelection();
  }

  dragMoveRequest = null;
  dragMoveApplied = false;
}

function handleSquareDragOver(event) {
  if (dragMoveRequest) {
    event.preventDefault();
    event.dataTransfer.dropEffect = 'move';
  }
}

async function handleSquareDrop(event) {
  if (!dragMoveRequest) {
    return;
  }

  event.preventDefault();
  dragDropPending = true;
  await dragMoveRequest;

  const row = Number(event.currentTarget.dataset.row);
  const column = Number(event.currentTarget.dataset.column);
  const targetMove = moveToSquare(row, column);
  const castlingMove = castlingMoveForRook(row, column);
  const move = targetMove || castlingMove;

  if (move) {
    dragMoveApplied = true;
    applyLegalMove(move);
  } else {
    clearSelection();
  }

  dragMoveRequest = null;
  dragMoveApplied = false;
  dragDropPending = false;
}

function removeCastlingRights(rights) {
  for (const right of rights) {
    castlingRights = castlingRights.replace(right, '');
  }
}

function updateCastlingRights(piece, from, to, capturedPiece) {
  if (piece === 'K') {
    removeCastlingRights('KQ');
  } else if (piece === 'k') {
    removeCastlingRights('kq');
  } else if (piece === 'R' && from === 'a1') {
    removeCastlingRights('Q');
  } else if (piece === 'R' && from === 'h1') {
    removeCastlingRights('K');
  } else if (piece === 'r' && from === 'a8') {
    removeCastlingRights('q');
  } else if (piece === 'r' && from === 'h8') {
    removeCastlingRights('k');
  }

  if (capturedPiece === 'R' && to === 'a1') {
    removeCastlingRights('Q');
  } else if (capturedPiece === 'R' && to === 'h1') {
    removeCastlingRights('K');
  } else if (capturedPiece === 'r' && to === 'a8') {
    removeCastlingRights('q');
  } else if (capturedPiece === 'r' && to === 'h8') {
    removeCastlingRights('k');
  }
}

function applyCastlingRook(move, row) {
  if (move.flags & MOVE_FLAG_CASTLE_KINGSIDE) {
    board[row][5] = board[row][7];
    board[row][7] = '.';
  } else if (move.flags & MOVE_FLAG_CASTLE_QUEENSIDE) {
    board[row][3] = board[row][0];
    board[row][0] = '.';
  }
}

function applyLegalMove(
  move,
  { availableMoves = positionLegalMoves } = {}
) {
  const source = squareCoordinates(move.from);
  const target = squareCoordinates(move.to);
  const piece = board[source.row][source.column];
  const capturedPiece = board[target.row][target.column];
  const notation = moveNotation(move, piece, availableMoves);

  if (historyIndex < moveHistory.length) {
    moveHistory = moveHistory.slice(0, historyIndex);
    positionHistory = positionHistory.slice(0, historyIndex + 1);
  }

  updateCastlingRights(piece, move.from, move.to, capturedPiece);

  board[source.row][source.column] = '.';

  if (move.flags & MOVE_FLAG_EN_PASSANT) {
    const capturedRow = sideToMove === 'white'
      ? target.row + 1
      : target.row - 1;
    board[capturedRow][target.column] = '.';
  }

  if ((move.flags & MOVE_FLAG_CASTLE_KINGSIDE) ||
      (move.flags & MOVE_FLAG_CASTLE_QUEENSIDE)) {
    applyCastlingRook(move, source.row);
  }

  if (move.flags & MOVE_FLAG_PROMOTION) {
    board[target.row][target.column] = sideToMove === 'white'
      ? move.promotion.toUpperCase()
      : move.promotion;
  } else {
    board[target.row][target.column] = piece;
  }

  enPassantSquare = '-';
  if (move.flags & MOVE_FLAG_DOUBLE_PAWN) {
    enPassantSquare = squareName(
      (source.row + target.row) / 2,
      source.column
    );
  }

  if (piece.toLowerCase() === 'p' || capturedPiece !== '.') {
    halfmoveClock = 0;
  } else {
    halfmoveClock++;
  }

  if (sideToMove === 'black') {
    fullmoveNumber++;
  }

  sideToMove = sideToMove === 'white' ? 'black' : 'white';
  selectedSquare = null;
  legalMoves = [];
  moveRequestNumber++;

  lastMove = { from: move.from, to: move.to };
  moveHistory.push({ notation, from: move.from, to: move.to });
  positionHistory.push(capturePosition());
  historyIndex = moveHistory.length;

  renderBoard();
  renderMoveHistory();
  refreshAnalysis();

  refreshGameState({ startEngine: currentMode === 'play' });

  if (currentMode === 'analysis') {
    requestSearch();
  }
}

function updateLastMoveSuffix(suffix) {
  if (historyIndex === 0 || !moveHistory[historyIndex - 1]) {
    return;
  }

  const move = moveHistory[historyIndex - 1];
  move.notation = move.notation.replace(/[+#]$/, '') + suffix;
}

async function refreshGameState({ startEngine = false } = {}) {
  const requestNumber = ++gameStateRequestNumber;
  const fen = currentFen();

  currentPositionInCheck = false;
  gameResult = null;
  renderBoard();

  try {
    const result = await fetchPositionMoves(fen);

    if (requestNumber !== gameStateRequestNumber || fen !== currentFen()) {
      return;
    }

    positionLegalMoves = result.moves;
    currentPositionInCheck = Boolean(result.inCheck);

    if (result.moves.length === 0) {
      if (currentPositionInCheck) {
        const winner = sideToMove === 'white' ? 'Black' : 'White';
        gameResult = `${winner} wins by checkmate`;
        updateLastMoveSuffix('#');
      } else {
        gameResult = 'Draw by stalemate';
      }
    } else if (currentPositionInCheck) {
      updateLastMoveSuffix('+');
    } else {
      updateLastMoveSuffix('');
    }

    if (currentMode === 'play') {
      searchStatus = gameResult ||
        (sideToMove === playerColor ? 'Your turn' : 'Theta to move');
    }

    renderBoard();
    renderMoveHistory();
    refreshAnalysis();

    if (startEngine &&
        currentMode === 'play' &&
        !gameResult &&
        sideToMove !== playerColor) {
      requestEngineMove();
    }
  } catch (error) {
    if (requestNumber === gameStateRequestNumber) {
      console.error(error);
    }
  }
}

function handleEditorClick(row, column) {
  board[row][column] = selectedEditorPiece || '.';
  selectedSquare = null;
  legalMoves = [];
  enPassantSquare = '-';
  resetHistory();
  renderBoard();
  updateEvaluation(0);
  analysisDepth = 0;
  principalVariation = [];
  refreshEditorControls();
  refreshAnalysis();
}

async function handleSquareClick(event) {
  const row = Number(event.currentTarget.dataset.row);
  const column = Number(event.currentTarget.dataset.column);

  if (currentMode === 'editor') {
    handleEditorClick(row, column);
    return;
  }

  if (engineMovePending) {
    return;
  }

  if (isSelected(row, column)) {
    clearSelection();
    return;
  }

  const targetMove = moveToSquare(row, column);
  if (selectedSquare && targetMove) {
    applyLegalMove(targetMove);
    return;
  }

  const castlingMove = castlingMoveForRook(row, column);
  if (castlingMove) {
    applyLegalMove(castlingMove);
    return;
  }

  const piece = board[row][column];
  if (canSelectPiece(piece)) {
    await requestLegalMoves(row, column);
  } else {
    clearSelection();
  }
}

function formatEvaluation(value) {
  return `${value >= 0 ? '+' : ''}${value.toFixed(2)}`;
}

function scoreFromWhitePerspective(update) {
  return sideToMove === 'white' ? update.evaluation : -update.evaluation;
}

function formatSearchScore(update) {
  if (update.scoreType === 'mate' && Number.isFinite(update.mate)) {
    const mate = sideToMove === 'white' ? update.mate : -update.mate;
    return `#${mate}`;
  }

  return formatEvaluation(scoreFromWhitePerspective(update) / 100);
}

function formatCompactNumber(value) {
  if (!Number.isFinite(value) || value <= 0) {
    return '0';
  }

  if (value >= 1000000) {
    return `${(value / 1000000).toFixed(1)}m`;
  }

  if (value >= 1000) {
    return `${(value / 1000).toFixed(1)}k`;
  }

  return String(Math.round(value));
}

function formatSearchTime(milliseconds) {
  if (!Number.isFinite(milliseconds) || milliseconds <= 0) {
    return '0ms';
  }

  if (milliseconds >= 1000) {
    return `${(milliseconds / 1000).toFixed(1)}s`;
  }

  return `${Math.round(milliseconds)}ms`;
}

function updateEvaluation(
  evaluation,
  labelText = formatEvaluation(evaluation / 100)
) {
  const bar = document.querySelector('.evaluation');
  const fill = document.querySelector('#eval-fill');
  const label = document.querySelector('#eval-label');
  const whitePercent = Math.max(0, Math.min(100, 50 + evaluation / 20));

  materialEvaluation = evaluation;
  evaluationText = labelText;
  label.textContent = evaluationText;
  fill.style.height = `${whitePercent}%`;
  bar.classList.toggle('flipped', isFlipped);
}

function refreshSearchSettings() {
  searchTimeInput.value = searchTimeMs;
  searchDepthInput.value = searchDepth;
  searchInfiniteInput.checked = searchInfinite;
  searchTimeInput.disabled = searchInfinite && currentMode !== 'play';
  searchDepthInput.disabled = searchInfinite && currentMode !== 'play';
  document.querySelector('#search-time-value').textContent =
    searchTimeInput.disabled ? 'Infinite' : `${(searchTimeMs / 1000).toFixed(1)}s`;
  document.querySelector('#search-depth-value').textContent =
    searchDepthInput.disabled ? 'Infinite' : searchDepth;
}

function updateSearchSettings() {
  searchTimeMs = Number(searchTimeInput.value);
  searchDepth = Number(searchDepthInput.value);
  searchInfinite = searchInfiniteInput.checked;
  refreshSearchSettings();
}

async function loadSearchSettings() {
  try {
    const response = await fetch('/api/config');
    const result = await response.json();
    const maximumDepth = Number(result.maxDepth);

    if (Number.isInteger(maximumDepth) && maximumDepth >= 1) {
      searchDepthInput.max = maximumDepth;
      searchDepth = Math.min(searchDepth, maximumDepth);
      refreshSearchSettings();
    }
  } catch (error) {
    console.error(error);
  }
}

function applyVariationMove(lineBoard, move, side) {
  const source = squareCoordinates(move.from);
  const target = squareCoordinates(move.to);
  const piece = lineBoard[source.row][source.column];

  lineBoard[source.row][source.column] = '.';

  if (move.flags & MOVE_FLAG_EN_PASSANT) {
    const capturedRow = side === 'white'
      ? target.row + 1
      : target.row - 1;

    lineBoard[capturedRow][target.column] = '.';
  }

  if (move.flags & MOVE_FLAG_CASTLE_KINGSIDE) {
    lineBoard[source.row][5] = lineBoard[source.row][7];
    lineBoard[source.row][7] = '.';
  } else if (move.flags & MOVE_FLAG_CASTLE_QUEENSIDE) {
    lineBoard[source.row][3] = lineBoard[source.row][0];
    lineBoard[source.row][0] = '.';
  }

  if (move.flags & MOVE_FLAG_PROMOTION) {
    lineBoard[target.row][target.column] = side === 'white'
      ? move.promotion.toUpperCase()
      : move.promotion;
  } else {
    lineBoard[target.row][target.column] = piece;
  }
}

function isInsideBoard(row, column) {
  return row >= 0 && row < 8 && column >= 0 && column < 8;
}

function isSquareAttacked(lineBoard, row, column, attackingSide) {
  const pawn = attackingSide === 'white' ? 'P' : 'p';
  const knight = attackingSide === 'white' ? 'N' : 'n';
  const bishop = attackingSide === 'white' ? 'B' : 'b';
  const rook = attackingSide === 'white' ? 'R' : 'r';
  const queen = attackingSide === 'white' ? 'Q' : 'q';
  const king = attackingSide === 'white' ? 'K' : 'k';
  const pawnRow = row + (attackingSide === 'white' ? 1 : -1);
  const knightOffsets = [
    [-2, -1], [-2, 1], [-1, -2], [-1, 2],
    [1, -2], [1, 2], [2, -1], [2, 1]
  ];
  const diagonalDirections = [[-1, -1], [-1, 1], [1, -1], [1, 1]];
  const straightDirections = [[-1, 0], [1, 0], [0, -1], [0, 1]];

  for (const pawnColumn of [column - 1, column + 1]) {
    if (isInsideBoard(pawnRow, pawnColumn) &&
        lineBoard[pawnRow][pawnColumn] === pawn) {
      return true;
    }
  }

  for (const [rowOffset, columnOffset] of knightOffsets) {
    const sourceRow = row + rowOffset;
    const sourceColumn = column + columnOffset;

    if (isInsideBoard(sourceRow, sourceColumn) &&
        lineBoard[sourceRow][sourceColumn] === knight) {
      return true;
    }
  }

  for (const [rowOffset, columnOffset] of diagonalDirections) {
    let sourceRow = row + rowOffset;
    let sourceColumn = column + columnOffset;

    while (isInsideBoard(sourceRow, sourceColumn)) {
      const piece = lineBoard[sourceRow][sourceColumn];

      if (piece !== '.') {
        if (piece === bishop || piece === queen) {
          return true;
        }
        break;
      }

      sourceRow += rowOffset;
      sourceColumn += columnOffset;
    }
  }

  for (const [rowOffset, columnOffset] of straightDirections) {
    let sourceRow = row + rowOffset;
    let sourceColumn = column + columnOffset;

    while (isInsideBoard(sourceRow, sourceColumn)) {
      const piece = lineBoard[sourceRow][sourceColumn];

      if (piece !== '.') {
        if (piece === rook || piece === queen) {
          return true;
        }
        break;
      }

      sourceRow += rowOffset;
      sourceColumn += columnOffset;
    }
  }

  for (const [rowOffset, columnOffset] of [
    [-1, -1], [-1, 0], [-1, 1],
    [0, -1], [0, 1],
    [1, -1], [1, 0], [1, 1]
  ]) {
    const sourceRow = row + rowOffset;
    const sourceColumn = column + columnOffset;

    if (isInsideBoard(sourceRow, sourceColumn) &&
        lineBoard[sourceRow][sourceColumn] === king) {
      return true;
    }
  }

  return false;
}

function isSideInCheck(lineBoard, side) {
  const king = side === 'white' ? 'K' : 'k';

  for (let row = 0; row < 8; row++) {
    for (let column = 0; column < 8; column++) {
      if (lineBoard[row][column] === king) {
        return isSquareAttacked(
          lineBoard,
          row,
          column,
          side === 'white' ? 'black' : 'white'
        );
      }
    }
  }

  return false;
}

function formatPrincipalVariation() {
  const lineBoard = board.map(row => [...row]);
  const notation = [];
  let lineSide = sideToMove;
  let moveNumber = fullmoveNumber;
  let lineEnPassant = enPassantSquare;

  for (const uciMove of principalVariation) {
    const move = uciMoveToMove(uciMove, lineBoard, lineEnPassant);
    const source = squareCoordinates(move.from);
    const piece = lineBoard[source.row][source.column];
    let text = moveNotation(move, piece);

    if (lineSide === 'white') {
      notation.push(`${moveNumber}. ${text}`);
    } else if (notation.length === 0) {
      notation.push(`${moveNumber}... ${text}`);
    } else {
      notation.push(text);
    }

    applyVariationMove(lineBoard, move, lineSide);

    const nextSide = lineSide === 'white' ? 'black' : 'white';
    if (isSideInCheck(lineBoard, nextSide)) {
      notation[notation.length - 1] += '+';
    }

    lineEnPassant = move.flags & MOVE_FLAG_DOUBLE_PAWN
      ? squareName((source.row + squareCoordinates(move.to).row) / 2, source.column)
      : '-';

    if (lineSide === 'black') {
      moveNumber++;
    }

    lineSide = nextSide;
  }

  return notation.join(' ');
}

function refreshAnalysis() {
  const selectiveDepth = searchStats.selectiveDepth || analysisDepth;

  analysisToggleButton.hidden = currentMode !== 'analysis';
  analysisToggleButton.textContent = analysisRunning
    ? 'Stop analysis'
    : 'Start analysis';
  analysisToggleButton.classList.toggle('stop', analysisRunning);

  searchStatusElement.textContent = searchStatus;
  depthElement.textContent = selectiveDepth > analysisDepth
    ? `Depth ${analysisDepth} / Sel ${selectiveDepth}`
    : `Depth ${analysisDepth}`;
  searchStatsElement.textContent = searchError || [
    `${formatCompactNumber(searchStats.nodes)} nodes`,
    `${formatCompactNumber(searchStats.nps)}/s`,
    formatSearchTime(searchStats.timeMs),
    `hash ${searchStats.hashfull}`
  ].join(' · ');

  if (principalVariation.length === 0) {
    linesElement.innerHTML = '';
    return;
  }

  linesElement.innerHTML = [
    '<li>',
    `<span class="line-eval">${evaluationText}</span>`,
    `<span class="line-moves">${formatPrincipalVariation()}</span>`,
    '</li>'
  ].join('');
}

function renderEditorTray() {
  const tray = document.querySelector('#piece-tray');
  const pieceButtons = [];

  function pieceButton(piece) {
    const active = piece === selectedEditorPiece ? ' active' : '';

    return [
      `<button class="tray-piece${active}" data-piece="${piece}" aria-label="Place ${piece}">`,
      renderPiece(piece),
      '</button>'
    ].join('');
  }

  function eraserButton() {
    const active = selectedEditorPiece === '' ? ' active' : '';

    return `<button class="tray-piece eraser${active}" data-piece="" aria-label="Erase piece">&times;</button>`;
  }

  pieceButtons.push(...[...'kqrbnp'].map(pieceButton), eraserButton());
  pieceButtons.push(...[...'KQRBNP'].map(pieceButton), eraserButton());
  tray.innerHTML = pieceButtons.join('');

  for (const button of tray.querySelectorAll('button')) {
    button.addEventListener('click', handleEditorPieceClick);
  }
}

function handleEditorPieceClick(event) {
  selectedEditorPiece = event.currentTarget.dataset.piece;
  renderEditorTray();
}

function refreshEditorControls() {
  for (const button of editorColorButtons) {
    const active = button.dataset.color === sideToMove;
    button.classList.toggle('active', active);
    button.setAttribute('aria-pressed', String(active));
  }

  for (const input of castlingInputs) {
    input.checked = castlingRights.includes(input.dataset.castling);
  }
}

function setEditorSide(color) {
  if ((color !== 'white' && color !== 'black') || sideToMove === color) {
    return;
  }

  sideToMove = color;
  halfmoveClock = 0;
  fullmoveNumber = 1;
  resetHistory();
  refreshEditorControls();
  renderBoard();
}

function updateEditorCastlingRights() {
  castlingRights = [...castlingInputs]
    .filter(input => input.checked)
    .map(input => input.dataset.castling)
    .join('');
  resetHistory();
}

function refreshPlayControls() {
  playControlsElement.hidden = currentMode !== 'play';

  for (const button of playColorButtons) {
    const active = button.dataset.color === playerColor;
    button.classList.toggle('active', active);
    button.setAttribute('aria-pressed', String(active));
  }
}

function setPlayerColor(color) {
  if (color !== 'white' && color !== 'black') {
    return;
  }

  playerColor = color;
  isFlipped = color === 'black';
  updateEvaluation(materialEvaluation, evaluationText);
  refreshPlayControls();
  renderBoard();
  refreshGameState({ startEngine: currentMode === 'play' });
}

function setMode(mode) {
  evaluationRequestNumber++;
  gameStateRequestNumber++;
  if (searchAbortController) {
    searchAbortController.abort();
  }

  engineMovePending = false;
  analysisRunning = false;
  currentMode = mode;
  selectedSquare = null;
  legalMoves = [];
  moveRequestNumber++;

  for (const button of modeButtons) {
    button.classList.toggle('active', button.dataset.mode === mode);
  }

  document.querySelector('#editor-tools').hidden = mode !== 'editor';
  document.querySelector('#side-panel').classList.toggle('editing', mode === 'editor');

  refreshPlayControls();
  refreshEditorControls();
  refreshSearchSettings();
  refreshAnalysis();
  renderBoard();

  if (mode === 'analysis') {
    requestSearch();
  } else if (mode === 'play') {
    refreshGameState({ startEngine: true });
  }
}

function resetBoard() {
  evaluationRequestNumber++;
  gameStateRequestNumber++;
  if (searchAbortController) {
    searchAbortController.abort();
  }

  engineMovePending = false;
  board = createStartingPosition();
  sideToMove = 'white';
  castlingRights = 'KQkq';
  enPassantSquare = '-';
  halfmoveClock = 0;
  fullmoveNumber = 1;
  selectedSquare = null;
  legalMoves = [];
  moveRequestNumber++;
  resetHistory();
  refreshEditorControls();
  renderBoard();
  refreshAnalysis();

  if (currentMode === 'analysis') {
    requestSearch();
  } else if (currentMode === 'play') {
    refreshGameState({ startEngine: true });
  }
}

function clearBoard() {
  evaluationRequestNumber++;
  gameStateRequestNumber++;
  if (searchAbortController) {
    searchAbortController.abort();
  }

  engineMovePending = false;
  board = Array.from({ length: 8 }, () => Array(8).fill('.'));
  castlingRights = '';
  enPassantSquare = '-';
  halfmoveClock = 0;
  fullmoveNumber = 1;
  selectedSquare = null;
  legalMoves = [];
  moveRequestNumber++;
  resetHistory();
  refreshEditorControls();
  renderBoard();
  updateEvaluation(0);
  analysisDepth = 0;
  principalVariation = [];
  refreshAnalysis();
}

for (const button of modeButtons) {
  button.addEventListener('click', () => setMode(button.dataset.mode));
}

for (const button of playColorButtons) {
  button.addEventListener('click', () => setPlayerColor(button.dataset.color));
}

for (const button of editorColorButtons) {
  button.addEventListener('click', () => setEditorSide(button.dataset.color));
}

for (const input of castlingInputs) {
  input.addEventListener('change', updateEditorCastlingRights);
}

document.querySelector('#flip').addEventListener('click', () => {
  isFlipped = !isFlipped;
  updateEvaluation(materialEvaluation, evaluationText);
  renderBoard();
});

document.querySelector('#clear').addEventListener('click', clearBoard);
document.querySelector('#starting-position').addEventListener('click', resetBoard);
document.querySelector('#analyze-position').addEventListener('click', () => {
  setMode('analysis');
});
analysisToggleButton.addEventListener('click', () => {
  if (analysisRunning && searchAbortController) {
    searchAbortController.abort();
  } else {
    requestSearch();
  }
});
settingsButton.addEventListener('click', () => {
  const isOpen = settingsButton.getAttribute('aria-expanded') === 'true';

  settingsButton.setAttribute('aria-expanded', String(!isOpen));
  settingsElement.hidden = isOpen;
});
searchTimeInput.addEventListener('input', updateSearchSettings);
searchDepthInput.addEventListener('input', updateSearchSettings);
searchTimeInput.addEventListener('change', () => {
  updateSearchSettings();
  if (currentMode === 'analysis') {
    requestSearch();
  }
});
searchDepthInput.addEventListener('change', () => {
  updateSearchSettings();
  if (currentMode === 'analysis') {
    requestSearch();
  }
});
searchInfiniteInput.addEventListener('change', () => {
  updateSearchSettings();
  if (currentMode === 'analysis') {
    requestSearch();
  }
});
document.querySelector('#first-move').addEventListener('click', () => {
  goToHistory(0);
});
document.querySelector('#previous-move').addEventListener('click', () => {
  goToHistory(historyIndex - 1);
});
document.querySelector('#next-move').addEventListener('click', () => {
  goToHistory(historyIndex + 1);
});
document.querySelector('#last-move').addEventListener('click', () => {
  goToHistory(moveHistory.length);
});

document.addEventListener('keydown', event => {
  if (event.key === 'ArrowLeft') {
    goToHistory(historyIndex - 1);
  } else if (event.key === 'ArrowRight') {
    goToHistory(historyIndex + 1);
  } else if (event.key === 'Home') {
    goToHistory(0);
  } else if (event.key === 'End') {
    goToHistory(moveHistory.length);
  }
});

window.addEventListener('resize', syncWorkspaceHeight);
new ResizeObserver(syncWorkspaceHeight).observe(boardElement);
renderEditorTray();
resetHistory();
refreshPlayControls();
refreshEditorControls();
refreshSearchSettings();
renderBoard();
syncWorkspaceHeight();
refreshAnalysis();
loadSearchSettings().then(() => {
  if (currentMode === 'analysis') {
    requestSearch();
  } else if (currentMode === 'play') {
    refreshGameState({ startEngine: true });
  }
});

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

const boardElement = document.querySelector('#board');
const linesElement = document.querySelector('#lines');
const modeButtons = document.querySelectorAll('.mode');
const evaluationElement = document.querySelector('.evaluation');
const sidePanelElement = document.querySelector('#side-panel');
const settingsButton = document.querySelector('#engine-settings');
const settingsElement = document.querySelector('#search-settings');
const searchTimeInput = document.querySelector('#search-time');
const searchDepthInput = document.querySelector('#search-depth');

let board = createStartingPosition();
let currentMode = 'play';
let selectedSquare = null;
let selectedEditorPiece = 'P';
let legalMoves = [];
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
let evaluationRequestNumber = 0;
let searchAbortController = null;
let analysisDepth = searchDepth;
let principalVariation = [];

function createStartingPosition() {
  return STARTING_POSITION.map(row => [...row]);
}

function syncWorkspaceHeight() {
  const boardSize = boardElement.getBoundingClientRect().width;

  if (window.innerWidth <= 780 || boardSize === 0) {
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

function moveNotation(move, piece) {
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
  renderMoveHistory();
}

function goToHistory(index) {
  if (index < 0 || index >= positionHistory.length) {
    return;
  }

  historyIndex = index;
  restorePosition(positionHistory[index]);
  selectedSquare = null;
  legalMoves = [];
  moveRequestNumber++;
  renderBoard();
  renderMoveHistory();
  refreshAnalysis();
  requestSearch();
}

function canSelectPiece(piece) {
  if (piece === '.') {
    return false;
  }

  const isWhitePiece = piece === piece.toUpperCase();
  return sideToMove === 'white' ? isWhitePiece : !isWhitePiece;
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
    const response = await fetch('/api/moves', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({ fen: currentFen() })
    });

    if (!response.ok) {
      throw new Error(`Move request failed with ${response.status}`);
    }

    const result = await response.json();
    if (requestNumber !== moveRequestNumber) {
      return;
    }

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

async function requestSearch() {
  const requestNumber = ++evaluationRequestNumber;
  let buffer = '';

  if (searchAbortController) {
    searchAbortController.abort();
  }

  searchAbortController = new AbortController();

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
        timeMs: searchTimeMs
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

        if (requestNumber === evaluationRequestNumber) {
          updateEvaluation(update.evaluation);
          analysisDepth = update.depth;
          principalVariation = update.pv.map(uciMoveToMove);
          refreshAnalysis();
        }
      }
    }
  } catch (error) {
    if (error.name !== 'AbortError') {
      console.error(error);
    }
  }
}

function uciMoveToMove(text) {
  const from = text.slice(0, 2);
  const to = text.slice(2, 4);
  const source = squareCoordinates(from);
  const target = squareCoordinates(to);
  const piece = board[source.row]?.[source.column] || '.';
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

  if (currentMode === 'editor' || !canSelectPiece(piece)) {
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

function applyLegalMove(move) {
  const source = squareCoordinates(move.from);
  const target = squareCoordinates(move.to);
  const piece = board[source.row][source.column];
  const capturedPiece = board[target.row][target.column];
  const notation = moveNotation(move, piece);

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

  moveHistory.push({ notation });
  positionHistory.push(capturePosition());
  historyIndex = moveHistory.length;

  renderBoard();
  renderMoveHistory();
  refreshAnalysis();
  requestSearch();
}

function handleEditorClick(row, column) {
  board[row][column] = selectedEditorPiece || '.';
  selectedSquare = null;
  legalMoves = [];
  castlingRights = '';
  enPassantSquare = '-';
  resetHistory();
  renderBoard();
  updateEvaluation(0);
  analysisDepth = 0;
  principalVariation = [];
  refreshAnalysis();
}

async function handleSquareClick(event) {
  const row = Number(event.currentTarget.dataset.row);
  const column = Number(event.currentTarget.dataset.column);

  if (currentMode === 'editor') {
    handleEditorClick(row, column);
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

function updateEvaluation(evaluation) {
  const bar = document.querySelector('.evaluation');
  const fill = document.querySelector('#eval-fill');
  const label = document.querySelector('#eval-label');
  const whitePercent = Math.max(0, Math.min(100, 50 + evaluation / 20));

  materialEvaluation = evaluation;
  label.textContent = formatEvaluation(evaluation / 100);
  fill.style.height = `${whitePercent}%`;
  bar.classList.toggle('flipped', isFlipped);
}

function refreshSearchSettings() {
  searchTimeInput.value = searchTimeMs;
  searchDepthInput.value = searchDepth;
  document.querySelector('#search-time-value').textContent =
    `${(searchTimeMs / 1000).toFixed(1)}s`;
  document.querySelector('#search-depth-value').textContent = searchDepth;
}

function updateSearchSettings() {
  searchTimeMs = Number(searchTimeInput.value);
  searchDepth = Number(searchDepthInput.value);
  refreshSearchSettings();
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

function formatPrincipalVariation() {
  const lineBoard = board.map(row => [...row]);
  const notation = [];
  let lineSide = sideToMove;
  let moveNumber = fullmoveNumber;

  for (const move of principalVariation) {
    const source = squareCoordinates(move.from);
    const piece = lineBoard[source.row][source.column];
    const text = moveNotation(move, piece);

    if (lineSide === 'white') {
      notation.push(`${moveNumber}. ${text}`);
    } else if (notation.length === 0) {
      notation.push(`${moveNumber}... ${text}`);
    } else {
      notation.push(text);
    }

    applyVariationMove(lineBoard, move, lineSide);

    if (lineSide === 'black') {
      moveNumber++;
    }

    lineSide = lineSide === 'white' ? 'black' : 'white';
  }

  return notation.join(' ');
}

function refreshAnalysis() {
  const evaluationLabel = formatEvaluation(materialEvaluation / 100);

  document.querySelector('#depth').textContent = `Depth ${analysisDepth}`;

  if (principalVariation.length === 0) {
    linesElement.innerHTML = '';
    return;
  }

  linesElement.innerHTML = [
    '<li>',
    `<span class="line-eval">${evaluationLabel}</span>`,
    `<span class="line-moves">${formatPrincipalVariation()}</span>`,
    '</li>'
  ].join('');
}

function renderEditorTray() {
  const tray = document.querySelector('#piece-tray');
  const pieces = [...'PNBRQKpnbrqk'];

  const pieceButtons = pieces.map(piece => {
    const active = piece === selectedEditorPiece ? ' active' : '';

    return [
      `<button class="tray-piece${active}" data-piece="${piece}">`,
      renderPiece(piece),
      '</button>'
    ].join('');
  });

  pieceButtons.push(
    '<button class="tray-piece" data-piece="" aria-label="Erase">&times;</button>'
  );
  tray.innerHTML = pieceButtons.join('');

  for (const button of tray.querySelectorAll('button')) {
    button.addEventListener('click', handleEditorPieceClick);
  }
}

function handleEditorPieceClick(event) {
  selectedEditorPiece = event.currentTarget.dataset.piece;
  renderEditorTray();
}

function setMode(mode) {
  currentMode = mode;
  selectedSquare = null;
  legalMoves = [];
  moveRequestNumber++;

  for (const button of modeButtons) {
    button.classList.toggle('active', button.dataset.mode === mode);
  }

  document.querySelector('#editor-tools').hidden = mode !== 'editor';
  document.querySelector('#side-panel').classList.toggle('editing', mode === 'editor');

  refreshAnalysis();
  renderBoard();
}

function resetBoard() {
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
  renderBoard();
  refreshAnalysis();
  requestSearch();
}

function clearBoard() {
  board = Array.from({ length: 8 }, () => Array(8).fill('.'));
  castlingRights = '';
  enPassantSquare = '-';
  halfmoveClock = 0;
  fullmoveNumber = 1;
  selectedSquare = null;
  legalMoves = [];
  moveRequestNumber++;
  resetHistory();
  renderBoard();
  updateEvaluation(0);
}

for (const button of modeButtons) {
  button.addEventListener('click', () => setMode(button.dataset.mode));
}

document.querySelector('#flip').addEventListener('click', () => {
  isFlipped = !isFlipped;
  updateEvaluation(materialEvaluation);
  renderBoard();
});

document.querySelector('#reset').addEventListener('click', resetBoard);
document.querySelector('#clear').addEventListener('click', clearBoard);
document.querySelector('#start').addEventListener('click', resetBoard);
settingsButton.addEventListener('click', () => {
  const isOpen = settingsButton.getAttribute('aria-expanded') === 'true';

  settingsButton.setAttribute('aria-expanded', String(!isOpen));
  settingsElement.hidden = isOpen;
});
searchTimeInput.addEventListener('input', updateSearchSettings);
searchDepthInput.addEventListener('input', updateSearchSettings);
searchTimeInput.addEventListener('change', () => {
  updateSearchSettings();
  requestSearch();
});
searchDepthInput.addEventListener('change', () => {
  updateSearchSettings();
  requestSearch();
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

new ResizeObserver(syncWorkspaceHeight).observe(boardElement);
renderEditorTray();
resetHistory();
refreshSearchSettings();
renderBoard();
syncWorkspaceHeight();
refreshAnalysis();
requestSearch();

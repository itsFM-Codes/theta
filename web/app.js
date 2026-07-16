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

const DEMO_LINES = [
  ['+0.18', '1. e4 e5 2. Nf3 Nc6 3. Bb5 a6'],
  ['+0.12', '1. d4 Nf6 2. c4 e6 3. Nc3 Bb4'],
  ['+0.08', '1. Nf3 d5 2. g3 c6 3. Bg2 Bg4']
];

const boardElement = document.querySelector('#board');
const linesElement = document.querySelector('#lines');
const modeButtons = document.querySelectorAll('.mode');

let board = createStartingPosition();
let currentMode = 'play';
let selectedSquare = null;
let selectedEditorPiece = 'P';
let isFlipped = false;
let sideToMove = 'white';

function createStartingPosition() {
  return STARTING_POSITION.map(row => [...row]);
}

function renderPiece(piece) {
  const color = piece === piece.toUpperCase() ? 'w' : 'b';
  const type = piece.toUpperCase();
  const source = `assets/cburnett/${color}${type}.svg`;

  return `<img class="piece" src="${source}" alt="" draggable="false">`;
}

function isSelected(row, column) {
  return selectedSquare?.row === row && selectedSquare?.column === column;
}

function getSquareClass(row, column) {
  const color = (row + column) % 2 === 0 ? 'light' : 'dark';
  const selected = isSelected(row, column) ? ' selected' : '';

  return `square ${color}${selected}`;
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
    square.setAttribute('aria-label', `${FILES[column]}${8 - row}`);

    if (piece !== '.') {
      square.innerHTML = renderPiece(piece);
    }

    addCoordinates(square, row, column);
    square.addEventListener('click', handleSquareClick);
    boardElement.append(square);
  }
}

function handleEditorClick(row, column) {
  board[row][column] = selectedEditorPiece || '.';
  renderBoard();
}

function canSelectPiece(piece) {
  if (piece === '.') {
    return false;
  }

  if (currentMode === 'analysis') {
    return true;
  }

  const isWhitePiece = piece === piece.toUpperCase();
  return sideToMove === 'white' && isWhitePiece;
}

function moveSelectedPiece(row, column) {
  const source = selectedSquare;

  board[row][column] = board[source.row][source.column];
  board[source.row][source.column] = '.';
  selectedSquare = null;
  sideToMove = sideToMove === 'white' ? 'black' : 'white';

  renderBoard();
  refreshAnalysis();
}

function handleSquareClick(event) {
  const row = Number(event.currentTarget.dataset.row);
  const column = Number(event.currentTarget.dataset.column);

  if (currentMode === 'editor') {
    handleEditorClick(row, column);
    return;
  }

  if (selectedSquare) {
    moveSelectedPiece(row, column);
    return;
  }

  const piece = board[row][column];
  if (canSelectPiece(piece)) {
    selectedSquare = { row, column };
    renderBoard();
  }
}

function formatEvaluation(value) {
  return `${value >= 0 ? '+' : ''}${value.toFixed(2)}`;
}

function refreshAnalysis() {
  const evaluation = Math.random() * 0.8 - 0.4;
  const evaluationLabel = formatEvaluation(evaluation);
  const depth = 12 + Math.floor(Math.random() * 8);

  document.querySelector('#eval-label').textContent = evaluationLabel;
  document.querySelector('#eval-fill').style.height = `${50 - evaluation * 12}%`;
  document.querySelector('#depth').textContent = `Depth ${depth}`;

  linesElement.innerHTML = DEMO_LINES
    .map(([score, moves], index) => {
      const displayedScore = index === 0 ? evaluationLabel : score;
      return `<li data-eval="${displayedScore}">${moves}</li>`;
    })
    .join('');
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
  selectedSquare = null;
  renderBoard();
  refreshAnalysis();
}

function clearBoard() {
  board = Array.from({ length: 8 }, () => Array(8).fill('.'));
  selectedSquare = null;
  renderBoard();
}

for (const button of modeButtons) {
  button.addEventListener('click', () => setMode(button.dataset.mode));
}

document.querySelector('#flip').addEventListener('click', () => {
  isFlipped = !isFlipped;
  renderBoard();
});

document.querySelector('#reset').addEventListener('click', resetBoard);
document.querySelector('#clear').addEventListener('click', clearBoard);
document.querySelector('#start').addEventListener('click', resetBoard);

renderEditorTray();
renderBoard();
refreshAnalysis();

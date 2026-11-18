/** Parse the checked-in DS-00 matrix without losing quoted commas, line breaks or doubled quotes. */
export function csvRows(text: string): string[][] {
  const rows: string[][] = [];
  let row: string[] = [];
  let cell = '';
  let quoted = false;
  for (let i = text.charCodeAt(0) === 0xfeff ? 1 : 0; i < text.length; i++) {
    const char = text[i];
    if (quoted) {
      if (char !== '"') cell += char;
      else if (text[i + 1] === '"') { cell += '"'; i++; }
      else quoted = false;
    } else if (char === '"' && cell === '') quoted = true;
    else if (char === ',') { row.push(cell); cell = ''; }
    else if (char === '\r' || char === '\n') {
      if (char === '\r' && text[i + 1] === '\n') i++;
      rows.push([...row, cell]); row = []; cell = '';
    } else cell += char;
  }
  if (quoted) throw new Error('Unclosed quoted field in DS-00 CSV');
  if (cell !== '' || row.length) rows.push([...row, cell]);
  return rows;
}

import { clamp, unit01 } from './parameters';
export const shapes = ['linear', 'exponential', 'smoothstep', 'hold'] as const;
export type CurveShape = typeof shapes[number];
export type Node = { id: number; x: number; y: number; shape: CurveShape };
export type Curve = Node[];
export const round = (n: number) => +n.toFixed(3);
export function evaluate(curve: readonly Node[], time: number): number {
  if (time <= curve[0].x) return curve[0].y;
  for (let i = 1; i < curve.length; i++) {
    const a = curve[i - 1]; const b = curve[i];
    if (time >= b.x) continue;
    const t = unit01((time - a.x) / (b.x - a.x));
    const pos = a.shape === 'hold' ? 0 : a.shape === 'exponential' ? t * t : a.shape === 'smoothstep' ? t * t * (3 - 2 * t) : t;
    return a.y + (b.y - a.y) * pos;
  }
  return curve[curve.length - 1].y;
}
export function createCurve(values: readonly (readonly [number, number])[]): Curve {
  return values.map(([x, y], id) => ({ id, x, y, shape: 'linear' }));
}
export function moveNodes(curve: Curve, selected: number[], dx: number, dy: number, snap = false): Curve {
  const chosen = new Set(selected);
  // Group translation is bounded by adjacent unselected nodes; endpoints stay at 0/1.
  const x = (node: Node, i: number) => {
    if (i === 0 || i === curve.length - 1) return node.x;
    let next = node.x + dx;
    if (snap && dx !== 0) next = Math.round(next * 16) / 16;
    return round(clamp(next, 0, 1));
  };
  const proposal = curve.map((node, i) => chosen.has(node.id) ? { ...node, x: x(node, i), y: round(unit01(node.y + dy)) } : node);
  const ordered = proposal.every((node, i) => i === 0 || node.x - proposal[i - 1].x >= .0099);
  const next = ordered ? proposal : proposal.map((node, i) => ({ ...node, x: curve[i].x }));
  return next.every((node, i) => node.x === curve[i].x && node.y === curve[i].y) ? curve : next;
}
export function insertNode(curve: Curve, x: number, y: number): Curve {
  x = round(unit01(x)); y = round(unit01(y));
  if (curve.length >= 16 || curve.some(node => Math.abs(node.x - x) < .0099)) return curve;
  return [...curve, { id: Math.max(...curve.map(node => node.id)) + 1, x, y, shape: 'linear' as const }].sort((a, b) => a.x - b.x);
}

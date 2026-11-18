export type Parameter = {
  id: string;
  label: string;
  unit?: string;
  min: number;
  max: number;
  defaultValue: number;
  step: number;
  keyStep?: number;
  scale?: 'linear' | 'log';
  format?: (plain: number) => string;
};
export type ParameterBinding = {
  spec: Parameter;
  value: number;
  onChange: (value: number) => void;
  onBegin?: () => void;
  onEnd?: () => void;
  effective?: number;
  automated?: boolean;
  disabled?: boolean;
  selected?: boolean;
  message?: { severity: 'warning' | 'error'; text: string };
};
export const clamp = (n: number, min: number, max: number) => Math.min(max, Math.max(min, n));
export const unit01 = (n: number) => clamp(n, 0, 1);
export const linear = (id: string, label: string, min: number, max: number, defaultValue: number, step: number, unit?: string): Parameter => ({ id, label, min, max, defaultValue, step, unit });
export function toPlain(spec: Parameter, normalized: number) {
  const n = unit01(normalized);
  return spec.scale === 'log' ? spec.min * (spec.max / spec.min) ** n : spec.min + n * (spec.max - spec.min);
}
export function toNormalized(spec: Parameter, plain: number) {
  const n = clamp(plain, spec.min, spec.max);
  return spec.scale === 'log' ? Math.log(n / spec.min) / Math.log(spec.max / spec.min) : (n - spec.min) / (spec.max - spec.min);
}
export function quantize(spec: Parameter, value: number) {
  if (!Number.isFinite(value)) return spec.defaultValue;
  if (value === spec.defaultValue) return value;
  return clamp(Number((spec.min + Math.round((value - spec.min) / spec.step) * spec.step).toFixed(8)), spec.min, spec.max);
}
/** Unit entry adapted from donor A controls.tsx. Convert only within this binding's dimension. */
export function parsePlain(spec: Parameter, text: string): number | null {
  const parts = /^([+-]?(?:\d+(?:[.,]\d*)?|[.,]\d+))(?:\s*([^\s]+))?$/.exec(text.trim());
  if (!parts) return null;
  const unit = spec.unit?.toLowerCase();
  const suffix = parts[2]?.toLowerCase();
  let n = Number(parts[1].replace(',', '.'));
  if (suffix && suffix !== unit) {
    if (unit === 'hz' && (suffix === 'khz' || suffix === 'k')) n *= 1000;
    else if (unit === 'khz' && suffix === 'hz') n /= 1000;
    else if (unit === 's' && suffix === 'ms') n /= 1000;
    else if (unit === 'ms' && suffix === 's') n *= 1000;
    else return null;
  }
  return Number.isFinite(n) ? n : null;
}
export function numberText(n: number) {
  return String(Number(n.toFixed(4)));
}
export function valueText(spec: Parameter, value: number) {
  if (spec.format) return spec.format(value);
  return `${numberText(value)}${spec.unit ? ` ${spec.unit}` : ''}`;
}
export function reset(binding: ParameterBinding) {
  if (binding.disabled || binding.value === binding.spec.defaultValue) return;
  binding.onBegin?.();
  binding.onChange(binding.spec.defaultValue);
  binding.onEnd?.();
}

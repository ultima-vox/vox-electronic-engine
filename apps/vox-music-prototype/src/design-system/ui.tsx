import { useId, useRef } from 'react';
import type { ButtonHTMLAttributes, CSSProperties, HTMLAttributes, KeyboardEvent, ReactNode, SelectHTMLAttributes } from 'react';
import { ChevronDown, Info, TriangleAlert } from 'lucide-react';
import { clamp } from './parameters';

export const cx = (...classes: (string | false | undefined)[]) => classes.filter(Boolean).join(' ');
export function Button({ className, children, variant = 'neutral', ...props }: ButtonHTMLAttributes<HTMLButtonElement> & { variant?: 'neutral' | 'primary' | 'ghost' | 'danger' }) {
  return <button type="button" className={cx('ds-button', `ds-${variant}`, className)} {...props}>{children}</button>;
}
export function IconButton({ label, children, ...props }: ButtonHTMLAttributes<HTMLButtonElement> & { label: string }) {
  return <Button variant="ghost" {...props} className={cx('ds-icon', props.className)} title={label} aria-label={label}>{children}</Button>;
}
export function Toggle({ checked, children, ...props }: ButtonHTMLAttributes<HTMLButtonElement> & { checked: boolean }) {
  return <Button {...props} aria-pressed={checked}>{children}</Button>;
}
export function Switch({ label, checked, onChange, disabled }: { label: string; checked: boolean; onChange: (next: boolean) => void; disabled?: boolean }) {
  return <button type="button" role="switch" className="ds-switch" aria-checked={checked} disabled={disabled} onClick={() => onChange(!checked)}><span aria-hidden="true" />{label}</button>;
}
export type Choice<T> = { value: T; label: ReactNode; name?: string; disabled?: boolean };
export function Segmented<T extends string | number>({ label, value, choices, onChange, className }: { label: string; value: T; choices: readonly Choice<T>[]; onChange: (value: T) => void; className?: string }) {
  return <div className={cx('ds-segment', className)} role="group" aria-label={label}>{choices.map(choice => <button type="button" key={choice.value} aria-label={choice.name} aria-pressed={choice.value === value} disabled={choice.disabled} onClick={() => onChange(choice.value)}>{choice.label}</button>)}</div>;
}
export function Select({ label, hideLabel, className, children, ...props }: SelectHTMLAttributes<HTMLSelectElement> & { label: string; hideLabel?: boolean }) {
  return <label className={cx('ds-select', className)}><span className={hideLabel ? 'sr-only' : 'eyebrow'}>{label}</span><span className="select-well"><select {...props}>{children}</select><ChevronDown size={12} aria-hidden="true" /></span></label>;
}
export function Field({ label, children, className }: { label: string; children: ReactNode; className?: string }) {
  return <label className={cx('ds-field', className)}><span className="eyebrow">{label}</span>{children}</label>;
}
export function Tabs<T extends string>({ label, value, choices, onChange, id, className }: { label: string; value: T; choices: readonly Choice<T>[]; onChange: (value: T) => void; id: string; className?: string }) {
  function keys(event: KeyboardEvent<HTMLButtonElement>, i: number) {
    const last = choices.length - 1;
    const order = Array.from({ length: choices.length }, (_, n) => n);
    const next = event.key === 'Home' ? order.find(n => !choices[n].disabled)
      : event.key === 'End' ? [...order].reverse().find(n => !choices[n].disabled)
      : event.key === 'ArrowRight' ? order.map(n => (i + n + 1) % choices.length).find(n => !choices[n].disabled)
      : event.key === 'ArrowLeft' ? order.map(n => (i - n - 1 + choices.length) % choices.length).find(n => !choices[n].disabled) : undefined;
    if (next === undefined || next < 0 || next > last) return;
    event.preventDefault();
    onChange(choices[next].value);
    document.getElementById(`${id}-${choices[next].value}-tab`)?.focus();
  }
  return <div role="tablist" aria-label={label} className={cx('ds-tabs', className)}>{choices.map((choice, i) => <button role="tab" type="button" aria-selected={choice.value === value} aria-label={choice.name} disabled={choice.disabled} tabIndex={choice.value === value ? 0 : -1} id={`${id}-${choice.value}-tab`} aria-controls={`${id}-panel`} key={choice.value} onClick={() => onChange(choice.value)} onKeyDown={event => keys(event, i)}>{choice.label}</button>)}</div>;
}
export function TabPanel({ id, value, children, className }: { id: string; value: string; children: ReactNode; className?: string }) {
  return <section role="tabpanel" id={`${id}-panel`} aria-labelledby={`${id}-${value}-tab`} tabIndex={0} className={className}>{children}</section>;
}
export function Badge({ children, tone = 'neutral', dot = false }: { children: ReactNode; tone?: 'neutral' | 'accent' | 'good' | 'warning' | 'error'; dot?: boolean }) {
  return <span className={`ds-badge tone-${tone}`}>{dot && <i aria-hidden="true" />}{children}</span>;
}
export function Notice({ children, tone = 'neutral', className }: { children: ReactNode; tone?: 'neutral' | 'warning' | 'error'; className?: string }) {
  const Icon = tone === 'neutral' ? Info : TriangleAlert;
  return <div className={cx('ds-notice', `tone-${tone}`, className)} role={tone === 'error' ? 'alert' : undefined}><Icon aria-hidden="true" size={15} /><div>{children}</div></div>;
}
export function Module({ title, icon, actions, children, className, ...props }: HTMLAttributes<HTMLElement> & { title: string; icon?: ReactNode; actions?: ReactNode; children: ReactNode }) {
  const id = useId();
  return <section aria-labelledby={id} className={cx('ds-panel', className)} {...props}><header className="panel-header">{icon}<h2 id={id}>{title}</h2><div className="spacer" />{actions}</header>{children}</section>;
}
export function Splitter({ label, direction, value, min, max, onChange, invert = false, percent = false, className }: { label: string; direction: 'horizontal' | 'vertical'; value: number; min: number; max: number; onChange: (next: number) => void; invert?: boolean; percent?: boolean; className?: string }) {
  const drag = useRef<{ initial: number; position: number; divisor: number } | null>(null);
  const vertical = direction === 'vertical';
  return <div className={cx('ds-splitter', `splitter-${direction}`, className)} role="separator" aria-label={label} aria-orientation={direction} aria-valuemin={min} aria-valuemax={max} aria-valuenow={value} tabIndex={0}
    onPointerDown={event => {
      if (event.button !== 0) return;
      const target = event.currentTarget;
      const parent = target.parentElement?.getBoundingClientRect();
      drag.current = { initial: value, position: vertical ? event.clientX : event.clientY, divisor: percent ? Math.max(1, (vertical ? parent?.width ?? 1 : parent?.height ?? 1) / 100) : 1 };
      target.focus(); target.setPointerCapture(event.pointerId);
    }} onPointerMove={event => {
      const start = drag.current;
      if (!start || !event.currentTarget.hasPointerCapture(event.pointerId)) return;
      const delta = ((vertical ? event.clientX : event.clientY) - start.position) / start.divisor;
      onChange(Math.round(clamp(start.initial + (invert ? -delta : delta), min, max)));
    }} onLostPointerCapture={() => { drag.current = null; }} onKeyDown={event => {
      const minus = vertical ? 'ArrowLeft' : 'ArrowUp'; const plus = vertical ? 'ArrowRight' : 'ArrowDown';
      const delta = event.key === minus ? -1 : event.key === plus ? 1 : 0;
      if (!delta && event.key !== 'Home' && event.key !== 'End') return;
      event.preventDefault(); const step = percent ? 2 : 12;
      onChange(clamp(event.key === 'Home' ? min : event.key === 'End' ? max : value + delta * step * (invert ? -1 : 1), min, max));
    }}><span /></div>;
}
export const prop = (name: `--${string}`, value: string | number): CSSProperties => ({ [name]: value });
